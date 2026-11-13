// Standalone regression suite: link this file with searches.cpp, without HAVE_* flags.
// g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror benchmark_tests.cpp searches.cpp -o <temporary-path>/benchmark_tests.exe
#define main benchmarkMainForTests
#include "benchmark.cpp"
#undef main

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace fs = std::filesystem;
using Row = std::vector<std::string>;
using Table = std::vector<Row>;
size_t checks = 0;
size_t failures = 0;

bool check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

void require(bool condition, const std::string& message) {
    if (!check(condition, message)) throw std::runtime_error(message);
}

bool equalMetrics(const Metrics& a, const Metrics& b) {
    return a.comparisons == b.comparisons && a.swaps == b.swaps &&
           a.writes == b.writes && a.auxBytes == b.auxBytes &&
           a.precheckComparisons == b.precheckComparisons && a.curAux == b.curAux;
}

// Only atomically claimed temporary directories may be removed by this suite.
class TempDirectory {
    fs::path path_;
public:
    TempDirectory() {
        const fs::path parent = fs::absolute(fs::temp_directory_path());
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const fs::path candidate = parent / ("benchmark-regression-" +
                std::to_string(Clock::now().time_since_epoch().count()) + "-" +
                std::to_string(random()) + "-" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error) throw fs::filesystem_error("create temporary directory", candidate, error);
        }
        throw std::runtime_error("could not claim a unique temporary directory");
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
        check(!error, "clean own temporary directory: " + path_.string());
    }
    const fs::path& path() const { return path_; }
};

class TemporaryCsv {
    std::unique_ptr<TempDirectory> fallback_;
    FILE* file_ = nullptr;
public:
    TemporaryCsv() : file_(std::tmpfile()) {
        // Some Windows CRT configurations cannot create tmpfile() at the drive root.
        if (!file_) {
            fallback_ = std::make_unique<TempDirectory>();
            file_ = std::fopen((fallback_->path() / "capture.csv").string().c_str(), "w+b");
        }
        require(file_ != nullptr, "open temporary CSV capture");
    }
    TemporaryCsv(const TemporaryCsv&) = delete;
    TemporaryCsv& operator=(const TemporaryCsv&) = delete;
    ~TemporaryCsv() { if (file_) check(std::fclose(file_) == 0, "close temporary CSV capture"); }
    FILE* get() const { return file_; }
    std::string contents() {
        require(std::fflush(file_) == 0, "flush temporary CSV");
        require(std::fseek(file_, 0, SEEK_SET) == 0, "rewind temporary CSV");
        std::string text;
        for (int ch = std::fgetc(file_); ch != EOF; ch = std::fgetc(file_))
            text.push_back(static_cast<char>(ch));
        require(std::ferror(file_) == 0, "read temporary CSV");
        return text;
    }
};

Row splitRow(const std::string& line) {
    Row fields;
    size_t start = 0;
    for (;;) {
        const size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(line.substr(start)); // Preserve trailing empty fields too.
            return fields;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
}

Table parseCsv(const std::string& text, size_t width) {
    Table rows;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        require(end != std::string::npos, "CSV row has a terminating newline");
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Row row = splitRow(line);
        require(row.size() == width, "CSV has exactly " + std::to_string(width) +
                " fields (got " + std::to_string(row.size()) + "): " + line);
        rows.push_back(std::move(row));
        start = end + 1;
    }
    return rows;
}

const Row sortHeader = splitRow("algorithm,N,distribution,time_us,min_time_us,max_time_us,comparisons,swaps,writes,aux_bytes,trials,correct,seed");
const Row searchHeader = splitRow("algorithm,N,distribution,case,status,scope,avg_time_us,min_time_us,max_time_us,avg_comparisons,avg_swaps,avg_writes,aux_bytes,precheck_comparisons,precheck_scope,trials,queries_per_trial,seed");

enum SearchColumn : size_t {
    Algorithm, N, Distribution, Case, Status, Scope, Time, MinTime, MaxTime,
    Comparisons, Swaps, Writes, Aux, Precheck, PrecheckScope, Trials, Queries, Seed
};

double number(const std::string& value) {
    size_t used = 0;
    const double result = std::stod(value, &used);
    require(used == value.size() && std::isfinite(result), "finite complete numeric CSV field: " + value);
    return result;
}

void checkNear(const std::string& value, double expected, const std::string& message) {
    check(std::fabs(number(value) - expected) <= 0.00000051, message);
}

void timing(const Row& row, size_t median, size_t minimum, size_t maximum) {
    const double med = number(row[median]);
    const double low = number(row[minimum]);
    const double high = number(row[maximum]);
    check(0 <= low && low <= med && med <= high, "nonnegative min <= median <= max timing");
}

Metrics taggedMetrics(uint64_t tag) {
    Metrics m;
    m.comparisons = tag;
    m.swaps = tag + 10;
    m.writes = tag + 20;
    m.auxBytes = tag + 30;
    m.precheckComparisons = tag + 40;
    m.curAux = tag + 50;
    return m;
}

void testHelpers() {
    check(sortedAsc({}), "empty array is sorted");
    check(sortedAsc({1}), "singleton is sorted");
    check(sortedAsc({0, 0, 1, 2, 2}), "ascending duplicates are sorted");
    check(!sortedAsc({2, 1}), "descending pair is unsorted");
    check(!sortedAsc({0, 2, 1, 3}), "interior inversion is unsorted");
    check(!sortedAsc({0, 1, 3, 2}), "last inversion is unsorted");
    check(sameMultiset({}, {}), "empty multisets match");
    check(sameMultiset({cfg::VALUE_MAX, 0, 0, 7}, {7, 0, cfg::VALUE_MAX, 0}), "multiset preserves duplicates and endpoints");
    check(!sameMultiset({0, 7}, {0, 0, 7}), "missing value fails multiset");
    check(!sameMultiset({0, 0, 7}, {0, 7}), "added value fails multiset");
    check(!sameMultiset({0, 7, 7}, {0, 0, 7}), "same-size wrong multiplicities fail");
    for (int invalid : {cfg::VALUE_MIN - 1, cfg::VALUE_MAX + 1}) {
        check(!sameMultiset({invalid}, {cfg::VALUE_MIN}), "range check on first multiset input");
        check(!sameMultiset({cfg::VALUE_MIN}, {invalid}), "range check on second multiset input");
        check(!sameMultiset({invalid}, {invalid}), "equal invalid inputs still fail range check");
    }
    check(repsFor(0) == 15 && repsFor(1) == 15 && repsFor(1000) == 15, "small-sort repetition boundary");
    check(repsFor(1001) == 3 && repsFor(100000) == 3, "large-sort repetition boundary");
    check(SEARCH_TRIALS == 5, "five search trials");
    check(usSince(Clock::now()) >= 0, "elapsed microseconds are nonnegative");

    std::vector<Trial> trials = {{9, taggedMetrics(9)}, {1, taggedMetrics(1)},
        {5, taggedMetrics(5)}, {3, taggedMetrics(3)}, {7, taggedMetrics(7)}};
    const Trial& median = medianTrial(trials);
    check(&median == &trials[2] && median.time == 5, "median is the chosen trial, not an average");
    for (size_t i = 0; i < trials.size(); ++i) {
        const uint64_t tag = 1 + 2 * i;
        check(trials[i].time == static_cast<double>(tag) && equalMetrics(trials[i].metrics, taggedMetrics(tag)),
              "all metrics remain attached to their timing");
    }
    trials = {{4, taggedMetrics(10)}, {2, taggedMetrics(20)}, {2, taggedMetrics(30)},
              {0, taggedMetrics(40)}, {2, taggedMetrics(50)}};
    const Trial& tied = medianTrial(trials);
    check(tied.time == 2 && equalMetrics(tied.metrics, taggedMetrics(30)), "tied median retains the selected trial's metrics");
    check(equalMetrics(trials[1].metrics, taggedMetrics(20)) &&
          equalMetrics(trials[3].metrics, taggedMetrics(50)), "equal-time trials retain stable order");
    trials = {{8, taggedMetrics(1)}, {8, taggedMetrics(2)}, {8, taggedMetrics(3)}};
    check(equalMetrics(medianTrial(trials).metrics, taggedMetrics(2)), "all-equal timing median");
    trials = {{4, taggedMetrics(4)}, {1, taggedMetrics(1)}, {3, taggedMetrics(3)}, {2, taggedMetrics(2)}};
    check(equalMetrics(medianTrial(trials).metrics, taggedMetrics(3)), "even trial count selects upper median");
    trials = {{0, taggedMetrics(77)}};
    check(equalMetrics(medianTrial(trials).metrics, taggedMetrics(77)), "singleton median");

    const std::vector<int> duplicates = {10, 20, 20, 30};
    check(validSearchResult(duplicates, 20, 1, 1, true), "linear first duplicate accepted");
    check(validSearchResult(duplicates, 20, 2, 1, false), "binary non-first duplicate accepted");
    check(!validSearchResult(duplicates, 20, 2, 1, true), "linear non-first duplicate rejected");
    check(!validSearchResult(duplicates, 20, 0, 1, false), "in-bounds wrong value rejected");
    for (int invalid : {NOT_FOUND, PRECONDITION_FAILED, std::numeric_limits<int>::min(),
                        static_cast<int>(duplicates.size()), std::numeric_limits<int>::max()})
        check(!validSearchResult(duplicates, 20, invalid, 1, false), "present key invalid index rejected");
    for (bool first : {false, true}) {
        check(validSearchResult(duplicates, 99, NOT_FOUND, NOT_FOUND, first), "absent key sentinel accepted");
        check(!validSearchResult(duplicates, 99, 0, NOT_FOUND, first), "absent false positive rejected");
        check(!validSearchResult(duplicates, 99, PRECONDITION_FAILED, NOT_FOUND, first), "absent wrong sentinel rejected");
        check(validSearchResult({}, 99, NOT_FOUND, NOT_FOUND, first), "empty search sentinel accepted");
        check(!validSearchResult({}, 99, 0, NOT_FOUND, first), "empty false positive rejected");
    }
}

enum class SortFault { None, FirstFourteen, OneTrial, Inversion, Missing, Added, Replaced, BelowRange, AboveRange, AuxLeak };
struct SortFixtureState {
    SortFault fault = SortFault::None;
    std::vector<int> original;
    size_t calls = 0;
    size_t badTrial = 0;
} sortState;

// These fixtures only preserve or damage existing data; none implements a sort.
void sortFixture(std::vector<int>& a, Metrics& m) {
    const size_t call = sortState.calls++;
    check(a == sortState.original, "sort trial " + std::to_string(call) + " starts with fresh input");
    check(equalMetrics(m, Metrics{}), "sort trial " + std::to_string(call) + " starts with fresh Metrics");
    m.comparisons = 11;
    m.swaps = 2;
    m.writes = 4;
    m.allocAux(64);
    m.freeAux(64);
    switch (sortState.fault) {
        case SortFault::None: break;
        case SortFault::FirstFourteen:
            if (call < 14) a[0] = cfg::VALUE_MIN + 1;
            break;
        case SortFault::OneTrial:
            if (call == sortState.badTrial) a[0] = cfg::VALUE_MIN + 1;
            break;
        case SortFault::Inversion: {
            const int first = a.front(); a.front() = a.back(); a.back() = first;
            break;
        }
        case SortFault::Missing: a.pop_back(); break;
        case SortFault::Added: a.push_back(cfg::VALUE_MAX); break;
        case SortFault::Replaced: a.front() = cfg::VALUE_MIN + 1; break;
        case SortFault::BelowRange: a.front() = cfg::VALUE_MIN - 1; break;
        case SortFault::AboveRange: a.back() = cfg::VALUE_MAX + 1; break;
        case SortFault::AuxLeak: m.allocAux(64); break;
    }
}

void noOpSort(std::vector<int>&, Metrics& m) {
    check(equalMetrics(m, Metrics{}), "no-op sort gets fresh Metrics");
}
void missingValueSort(std::vector<int>& a, Metrics&) { a.pop_back(); }

void sortCase(SortFault fault, size_t n = 10, size_t badTrial = 0) {
    sortState = {fault, generate(n, Dist::Sorted), 0, badTrial};
    TemporaryCsv csv;
    const bool expected = fault == SortFault::None;
    check(benchSorts({n}, {Dist::Sorted}, csv.get(), {{"Sort fixture", sortFixture}}) == expected,
          "sort fixture return status (fault=" + std::to_string(static_cast<int>(fault)) +
          ", bad trial=" + std::to_string(badTrial) + ")");
    check(sortState.calls == static_cast<size_t>(repsFor(n)), "all sort trials execute, including after failure");
    const Table rows = parseCsv(csv.contents(), 13);
    require(rows.size() == 2, "one sort header and one result");
    check(rows[0] == sortHeader, "exact 13-column sort header");
    const Row& row = rows[1];
    check(row[0] == "Sort fixture" && row[1] == std::to_string(n) && row[2] == "sorted", "sort CSV identity");
    timing(row, 3, 4, 5);
    check(row[6] == "11" && row[7] == "2" && row[8] == "4" && row[9] == "64", "sort CSV selected metrics and peak aux");
    check(row[10] == std::to_string(repsFor(n)) && row[11] == (expected ? "yes" : "NO") &&
          row[12] == std::to_string(cfg::SEED), "sort CSV trials, aggregate correctness and seed");
}

void testSortTrials() {
    for (size_t n : {0u, 1u, 2u, 3u, 10u, 1000u, 1001u}) sortCase(SortFault::None, n);
    // A valid final trial must not hide earlier corruption.
    sortCase(SortFault::FirstFourteen);
    for (size_t trial = 0; trial < 15; ++trial) sortCase(SortFault::OneTrial, 10, trial);
    for (size_t trial = 0; trial < 3; ++trial) sortCase(SortFault::OneTrial, 1001, trial);
    for (SortFault fault : {SortFault::Inversion, SortFault::Missing, SortFault::Added,
                           SortFault::Replaced, SortFault::BelowRange, SortFault::AboveRange, SortFault::AuxLeak})
        sortCase(fault);
    {
        TemporaryCsv csv;
        check(benchSorts({0, 1, 2, 3, 10}, {Dist::Sorted, Dist::AllEqual}, csv.get(), {{"No-op", noOpSort}}),
              "literal no-op is valid for already sorted distributions");
        const Table rows = parseCsv(csv.contents(), 13);
        require(rows.size() == 11, "sort size/distribution product");
        for (size_t i = 1; i < rows.size(); ++i) {
            check(rows[i][11] == "yes", "no-op CSV is correct");
            for (size_t c = 6; c <= 9; ++c) check(rows[i][c] == "0", "no-op zero metrics");
        }
    }
    {
        TemporaryCsv csv;
        check(!benchSorts({3, 10}, {Dist::Sorted}, csv.get(),
              {{"Missing", missingValueSort}, {"No-op", noOpSort}}), "one bad registry entry fails overall result");
        const Table rows = parseCsv(csv.contents(), 13);
        require(rows.size() == 5, "registry continues through every entry and size after failure");
        check(rows[1][11] == "NO" && rows[2][11] == "NO" &&
              rows[3][11] == "yes" && rows[4][11] == "yes", "sort correctness does not leak across entries");
    }
    {
        TemporaryCsv csv;
        check(benchSorts({10}, {Dist::Sorted}, csv.get(), {}), "explicit empty sort registry succeeds");
        const Table rows = parseCsv(csv.contents(), 13);
        check(rows.size() == 1 && rows[0] == sortHeader, "empty sort registry writes only header");
    }
    require(SORTS.empty(), "compile regression suite without teammate HAVE_* registry definitions");
    {
        TemporaryCsv csv;
        check(benchSorts({10}, {Dist::Sorted}, csv.get()), "default empty registry succeeds");
        const Table rows = parseCsv(csv.contents(), 13);
        check(rows.size() == 1 && rows[0] == sortHeader, "default registry writes only header");
    }
}

enum class SearchFault { None, AlwaysMiss, OneReturn, FixedReturn, Precheck, AuxLeak };
struct SearchFixtureState {
    SearchFault fault = SearchFault::None;
    std::vector<int> original;
    std::vector<int> keys;
    size_t total = 0;
    size_t calls = 0;
    size_t badCall = 0;
    int fixedReturn = NOT_FOUND;
    Metrics previous;
} searchState;

int searchFixture(const std::vector<int>& a, int key, Metrics& m) {
    const size_t call = searchState.calls++;
    const size_t position = call % searchState.total;
    check(a == searchState.original, "search fixture sees unchanged input");
    check(key == searchState.keys[position % searchState.keys.size()], "search batch key order");
    if (position == 0)
        check(equalMetrics(m, Metrics{}), "every search batch starts with fresh Metrics");
    else
        check(equalMetrics(m, searchState.previous), "Metrics accumulate within, not across, search batches");
    int result = linearSearch(a, key, m); // Pass through to the existing implementation.
    m.swaps += 3;
    m.writes += 4;
    m.allocAux(64);
    m.freeAux(64);
    if (searchState.fault == SearchFault::FixedReturn) result = searchState.fixedReturn;
    if (call == searchState.badCall) {
        if (searchState.fault == SearchFault::OneReturn) result = result == NOT_FOUND ? 0 : NOT_FOUND;
        if (searchState.fault == SearchFault::Precheck) ++m.precheckComparisons;
        if (searchState.fault == SearchFault::AuxLeak) m.allocAux(64);
    }
    searchState.previous = m;
    return result;
}

int wrongSearch(const std::vector<int>& a, int key, Metrics& m) {
    (void)searchFixture(a, key, m);
    return NOT_FOUND; // Deliberately misses even present keys, on every invocation.
}

void searchRow(const Row& row, const std::string& name, size_t n, Dist distribution,
               const std::string& caseName, bool correct, bool linear, uint64_t precheck, size_t queries) {
    check(row[Algorithm] == name && row[N] == std::to_string(n) &&
          row[Distribution] == distName(distribution) && row[Case] == caseName, "search CSV identity");
    check(row[Status] == (correct ? "OK" : "INCORRECT"), "search CSV correctness status");
    check(row[Scope] == (linear ? "search" : "core_after_precheck"), "search CSV timing scope");
    timing(row, Time, MinTime, MaxTime);
    check(number(row[Comparisons]) >= 0 && number(row[Swaps]) >= 0 &&
          number(row[Writes]) >= 0 && number(row[Aux]) >= 0, "nonnegative search metrics");
    check(row[Precheck] == std::to_string(precheck), "true dataset precheck cost is exported unchanged");
    check(row[PrecheckScope] == (linear ? "not_applicable" : "once_per_dataset"), "search CSV precheck scope");
    check(row[Trials] == "5" && row[Queries] == std::to_string(queries) &&
          row[Seed] == std::to_string(cfg::SEED), "search CSV trials, batch size and seed");
}

void searchCase(SearchFault fault, bool expected, bool linear = false, size_t badCall = 0,
                int fixedReturn = NOT_FOUND, const std::vector<int>& keys = {10, 20, 30, 99},
                const std::vector<int>& a = {10, 20, 20, 30}) {
    constexpr int reps = 3;
    const size_t total = keys.size() * reps;
    searchState = {fault, a, keys, total, 0, badCall, fixedReturn, {}};
    TemporaryCsv csv;
    const uint64_t precheck = linear ? 0 : 731;
    const SearchFn fn = fault == SearchFault::AlwaysMiss ? wrongSearch : searchFixture;
    check(runCase(csv.get(), "Search fixture", fn, a.size(), Dist::Sorted, "fixture", a, keys, reps,
                  linear, precheck) == expected,
          "runCase result (fault=" + std::to_string(static_cast<int>(fault)) +
          ", bad call=" + std::to_string(badCall) + ", linear=" + std::to_string(linear) + ")");
    check(searchState.calls == total * SEARCH_TRIALS, "every timed search executes; no hidden fixture preflight calls");
    check(searchState.original == a, "original search input remains unchanged");
    const Table rows = parseCsv(csv.contents(), 18);
    require(rows.size() == 1, "runCase emits exactly one 18-field row");
    const Row& row = rows[0];
    searchRow(row, "Search fixture", a.size(), Dist::Sorted, "fixture", expected, linear, precheck, total);
    Metrics expectedMetrics;
    for (int key : keys) (void)linearSearch(a, key, expectedMetrics);
    checkNear(row[Comparisons], static_cast<double>(expectedMetrics.comparisons) / keys.size(), "comparisons normalized per query, not per trial");
    checkNear(row[Swaps], 3, "swaps normalized per query");
    checkNear(row[Writes], 4, "writes normalized per query");
    check(row[Aux] == "64" || (fault == SearchFault::AuxLeak && row[Aux] == "128"), "peak auxiliary bytes are not divided by query count");
}

void testSearchTrials() {
    searchCase(SearchFault::None, true, false);
    searchCase(SearchFault::None, true, true);
    searchCase(SearchFault::AlwaysMiss, false, false);
    searchCase(SearchFault::AlwaysMiss, false, true);
    searchCase(SearchFault::AlwaysMiss, true, false, 0, NOT_FOUND, {99});
    // Every position must detect a lone bad return, including interior trials.
    for (bool linear : {false, true})
        for (size_t badCall = 0; badCall < 5 * 12; ++badCall)
            searchCase(SearchFault::OneReturn, false, linear, badCall);
    for (bool linear : {false, true}) {
        for (int invalid : {PRECONDITION_FAILED, -3, std::numeric_limits<int>::min(), 4,
                            std::numeric_limits<int>::max(), 0})
            searchCase(SearchFault::FixedReturn, false, linear, 0, invalid, {20});
        searchCase(SearchFault::FixedReturn, false, linear, 0, 0, {99});
        searchCase(SearchFault::FixedReturn, false, linear, 0, PRECONDITION_FAILED, {99});
        searchCase(SearchFault::None, true, linear, 0, NOT_FOUND, {99}, {});
        searchCase(SearchFault::FixedReturn, false, linear, 0, 0, {99}, {});
        for (size_t trial = 0; trial < 5; ++trial) {
            searchCase(SearchFault::Precheck, false, linear, trial * 12 + 5);
            searchCase(SearchFault::AuxLeak, false, linear, trial * 12 + 5);
        }
    }
    searchCase(SearchFault::FixedReturn, true, false, 0, 2, {20});
    searchCase(SearchFault::FixedReturn, false, true, 0, 2, {20});
    searchCase(SearchFault::FixedReturn, true, true, 0, 1, {20});
    {
        TemporaryCsv csv;
        check(!runCase(csv.get(), "Full binary wrapper", binarySearch, 4, Dist::Sorted, "contamination",
                       {10, 20, 20, 30}, {20, 99}, 3, false, 3), "full binary wrapper contaminates timed precheck metrics");
        const Table rows = parseCsv(csv.contents(), 18);
        require(rows.size() == 1, "full wrapper emits one row");
        searchRow(rows[0], "Full binary wrapper", 4, Dist::Sorted, "contamination", false, false, 3, 6);
    }
}

void testSearchCsv() {
    TemporaryCsv csv;
    check(benchSearches({0, 1, 2, 3, 10}, {Dist::Sorted, Dist::Reverse}, csv.get()),
          "real searches pass extended small sorted/reverse suite");
    const Table rows = parseCsv(csv.contents(), 18);
    require(rows.size() == 47, "46 data rows, including three rejected rows, plus search header");
    check(rows[0] == searchHeader, "exact 18-column search header");
    size_t index = 1;
    size_t rejected = 0;
    for (size_t n : {0u, 1u, 2u, 3u, 10u}) {
        std::mt19937 rng(cfg::SEED + 7);
        double averageLinearComparisons = 0;
        if (n != 0) {
            std::uniform_int_distribution<size_t> pick(0, n - 1);
            for (int k = 0; k < 1000; ++k) averageLinearComparisons += static_cast<double>(pick(rng) + 1) / 1000;
        }
        for (Dist distribution : {Dist::Sorted, Dist::Reverse}) {
            for (bool linear : {true, false}) {
                const uint64_t precheck = linear || n < 2 ? 0 : (distribution == Dist::Sorted ? n - 1 : 1);
                if (!linear && n > 1 && distribution == Dist::Reverse) {
                    const Row& row = rows.at(index++);
                    check(row[Algorithm] == "Binary Search" && row[N] == std::to_string(n) &&
                          row[Distribution] == "reverse" && row[Case] == "all", "rejected row identity");
                    check(row[Status] == "REJECTED" && row[Scope] == "not_executed", "rejected row status/scope");
                    for (size_t c = Time; c <= Aux; ++c) check(row[c].empty(), "rejected timing/metric field must be blank, not zero");
                    check(row[Precheck] == "1" && row[PrecheckScope] == "once_per_dataset", "rejection records early-exit precheck cost");
                    check(row[Trials] == "0" && row[Queries] == "0" && row[Seed] == std::to_string(cfg::SEED), "rejected row has no timed work");
                    ++rejected;
                    continue;
                }
                const std::vector<std::string> cases = n == 0 ? std::vector<std::string>{"empty"} :
                    std::vector<std::string>{"best", "average", "worst"};
                for (const std::string& caseName : cases) {
                    const Row& row = rows.at(index++);
                    const size_t queries = caseName == "best" ? 100000 : (caseName == "average" ? 10000 : 1000);
                    searchRow(row, linear ? "Linear Search" : "Binary Search", n, distribution, caseName,
                              true, linear, precheck, queries);
                    checkNear(row[Swaps], 0, "real searches have no swaps");
                    checkNear(row[Writes], 0, "real searches have no writes");
                    check(row[Aux] == "0", "real searches use no auxiliary arrays");
                    if (n == 0) checkNear(row[Comparisons], 0, "empty search makes no data comparisons");
                    else if (caseName == "best") checkNear(row[Comparisons], 1, "best search costs one comparison");
                    else if (linear && caseName == "worst") checkNear(row[Comparisons], static_cast<double>(n), "linear miss scans all n items");
                    else if (linear) checkNear(row[Comparisons], averageLinearComparisons, "linear average reports actual batch comparisons");
                    else if (caseName == "worst") checkNear(row[Comparisons], n == 1 ? 2 : (n == 10 ? 8 : 4), "binary miss core comparisons exclude precheck");
                    else check(number(row[Comparisons]) >= 1 && number(row[Comparisons]) <= 2 * n, "binary average core comparison bounds");
                }
            }
        }
    }
    check(index == rows.size() && rejected == 3, "all search rows consumed with exact rejection count");
}

class ScopedCwd {
    fs::path original_;
public:
    explicit ScopedCwd(const fs::path& path) : original_(fs::current_path()) { fs::current_path(path); }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;
    ~ScopedCwd() {
        std::error_code error;
        fs::current_path(original_, error);
        check(!error, "restore caller's working directory");
    }
};

int invokeMain(const TempDirectory& directory, const std::vector<std::string>& arguments) {
    std::vector<std::vector<char>> storage;
    storage.emplace_back();
    const std::string program = "benchmark-regression-cli";
    storage.back().assign(program.begin(), program.end());
    storage.back().push_back('\0');
    for (const std::string& arg : arguments) {
        storage.emplace_back(arg.begin(), arg.end());
        storage.back().push_back('\0');
    }
    std::vector<char*> argv;
    for (auto& arg : storage) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const ScopedCwd cwd(directory.path());
    return runBenchmark(static_cast<int>(storage.size()), argv.data());
}

std::string readFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.is_open(), "open test output: " + path.string());
    std::string result((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    require(!stream.bad(), "read complete test output");
    return result;
}

void writeSentinel(const fs::path& path, const std::string& contents) {
    require(!fs::exists(path), "sentinel only creates a new file in our temporary directory");
    std::ofstream stream(path, std::ios::binary);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.close();
    require(!stream.fail(), "write sentinel file");
}

std::set<std::string> directoryEntries(const TempDirectory& directory) {
    std::set<std::string> entries;
    for (const auto& entry : fs::directory_iterator(directory.path())) entries.insert(entry.path().filename().string());
    return entries;
}

#ifdef _WIN32
void testDesktopOutput() {
    TempDirectory directory;
    const auto executable = directory.path() / "benchmark.exe";
    const auto originalCwd = fs::current_path();
    const auto first = createDesktopRunDirectory(executable);
    std::ofstream(first / "search_results.csv") << "preserved";
    const auto second = createDesktopRunDirectory(executable);
    check(first != second, "desktop runs use distinct directories");
    check(first.parent_path() == directory.path() / "results", "desktop results are beside executable");
    check(fs::is_directory(first) && fs::is_directory(second), "both desktop directories exist");
    check(fs::is_empty(second), "new desktop directory starts empty");
    check(readFile(first / "search_results.csv") == "preserved", "desktop run preserves earlier CSV");
    check(fs::current_path() == originalCwd, "directory creation alone does not change cwd");

    TempDirectory blocked;
    std::ofstream(blocked.path() / "results") << "preserved";
    bool rejected = false;
    try { createDesktopRunDirectory(blocked.path() / "benchmark.exe"); }
    catch (const fs::filesystem_error&) { rejected = true; }
    check(rejected, "desktop reports unusable results directory");
    check(readFile(blocked.path() / "results") == "preserved", "desktop preserves blocking file");
}
#endif

void testCli() {
    const fs::path initialCwd = fs::current_path();
    const std::vector<std::vector<std::string>> malformed = {
        {"-1"}, {"-0"}, {"+100"}, {""}, {" 100"}, {"100 "}, {"100garbage"},
        {"1.5"}, {"1e3"}, {"--unknown"}, {"--extended=1"}, {std::string(256, '9')},
        {"100", "100"}, {"0", "--extended", "1"}, {"--extended", "--extended"},
        {"--extended", "0", "junk"}
    };
    for (size_t i = 0; i < malformed.size(); ++i) {
        TempDirectory directory;
        check(invokeMain(directory, malformed[i]) != 0, "malformed CLI arguments fail, case " + std::to_string(i));
        check(directoryEntries(directory).empty(), "malformed CLI creates no files");
        check(fs::current_path() == initialCwd, "malformed CLI restores cwd");
    }
    for (const std::string& maxN : {std::string("0"), std::string("1"), std::string("99")}) {
        TempDirectory directory;
        check(invokeMain(directory, {maxN}) != 0, "empty required suite fails for maxN=" + maxN);
        check(directoryEntries(directory).empty(), "empty required suite creates no files");
    }
    for (const std::vector<std::string>& arguments :
         std::vector<std::vector<std::string>>{{"0", "--extended"}, {"--extended", "000"}}) {
        TempDirectory directory;
        check(invokeMain(directory, arguments) == 0, "valid extended zero-size suite succeeds in either argument order");
        check(directoryEntries(directory) == std::set<std::string>{"search_results.csv", "sort_results.csv"}, "valid CLI creates exactly the two outputs");
        const std::string searchText = readFile(directory.path() / "search_results.csv");
        const std::string sortText = readFile(directory.path() / "sort_results.csv");
        const Table searches = parseCsv(searchText, 18);
        const Table sorts = parseCsv(sortText, 13);
        require(searches.size() == 21, "extended empty dataset covers all ten distributions and both searches");
        check(searches[0] == searchHeader, "CLI exact search header");
        check(sorts.size() == 1 && sorts[0] == sortHeader, "CLI empty registry emits sort header only");
        std::vector<Dist> distributions;
        for (Dist d : REQUIRED_DISTRIBUTIONS) distributions.push_back(d);
        for (Dist d : EXTRA_DISTRIBUTIONS) distributions.push_back(d);
        size_t index = 1;
        for (Dist d : distributions)
            for (bool linear : {true, false}) {
                const Row& row = searches.at(index++);
                searchRow(row, linear ? "Linear Search" : "Binary Search", 0, d, "empty", true, linear, 0, 1000);
                checkNear(row[Comparisons], 0, "CLI empty comparison cost");
            }
        check(invokeMain(directory, arguments) != 0, "rerunning CLI refuses to overwrite results");
        check(readFile(directory.path() / "search_results.csv") == searchText &&
              readFile(directory.path() / "sort_results.csv") == sortText, "rerun preserves both CSVs byte-for-byte");
    }
    // An existing second output must prevent creating a partial first output.
    for (unsigned mask : {1u, 2u, 3u}) {
        TempDirectory directory;
        const std::string searchSentinel("original search\0\r\n", 18);
        const std::string sortSentinel("original sort\0\r\n", 16);
        std::set<std::string> expected;
        if (mask & 1u) {
            writeSentinel(directory.path() / "search_results.csv", searchSentinel);
            expected.insert("search_results.csv");
        }
        if (mask & 2u) {
            writeSentinel(directory.path() / "sort_results.csv", sortSentinel);
            expected.insert("sort_results.csv");
        }
        check(invokeMain(directory, {"0", "--extended"}) != 0, "existing output prevents CLI execution");
        check(directoryEntries(directory) == expected, "existing output rejection creates no unexpected files");
        if (mask & 1u) check(readFile(directory.path() / "search_results.csv") == searchSentinel, "existing search CSV preserved byte-for-byte");
        if (mask & 2u) check(readFile(directory.path() / "sort_results.csv") == sortSentinel, "existing sort CSV preserved byte-for-byte");
    }
    {
        TempDirectory directory;
        require(fs::create_directory(directory.path() / "sort_results.csv"), "create second-output directory obstacle");
        check(invokeMain(directory, {"0", "--extended"}) != 0, "existing second output directory is rejected");
        check(directoryEntries(directory) == std::set<std::string>{"sort_results.csv"}, "second output directory does not cause partial first output");
        check(fs::is_directory(directory.path() / "sort_results.csv"), "existing directory is preserved");
    }
    check(fs::current_path() == initialCwd, "all CLI tests preserve caller cwd");
}

template <typename Function>
void group(const char* name, Function function) {
    const size_t before = failures;
    try { function(); }
    catch (const std::exception& error) { check(false, std::string(name) + ": " + error.what()); }
    catch (...) { check(false, std::string(name) + ": unexpected nonstandard exception"); }
    std::cout << "[TEST] " << name << ": " << (failures == before ? "PASS" : "FAIL") << '\n';
}
} // namespace

int main() {
    group("helpers and median/metric association", testHelpers);
    group("sort trial validation and 13-column CSV", testSortTrials);
    group("every timed search result and batch isolation", testSearchTrials);
    group("real search prechecks and 18-column CSV", testSearchCsv);
    group("CLI parsing and output preservation", testCli);
#ifdef _WIN32
    group("desktop output directories and preservation", testDesktopOutput);
#endif
    std::cout << "benchmark_tests: " << (failures == 0 ? "PASS" : "FAIL") << " | "
              << checks << " checks | " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
