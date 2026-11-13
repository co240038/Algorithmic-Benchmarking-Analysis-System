// Usage: ./benchmark [maxN] [--extended], from a new output directory.
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "metrics.h"
#include "data_gen.h"
#include "searches.h"
#include "sorts.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using Clock = std::chrono::steady_clock;
static double usSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

struct SortEntry { const char* name; void (*fn)(std::vector<int>&, Metrics&); };
static const std::vector<SortEntry> SORTS = {
#ifdef HAVE_BUBBLE
    {"Bubble Sort", bubbleSort},
#endif
#ifdef HAVE_SELECTION
    {"Selection Sort", selectionSort},
#endif
#ifdef HAVE_INSERTION
    {"Insertion Sort", insertionSort},
#endif
#ifdef HAVE_SHELL
    {"Shell Sort", shellSort},
#endif
#ifdef HAVE_MERGE
    {"Merge Sort", mergeSort},
#endif
#ifdef HAVE_COUNTING
    {"Counting Sort", countingSort},
#endif
#ifdef HAVE_BUCKET
    {"Bucket Sort", bucketSort},
#endif
};

static bool sortedAsc(const std::vector<int>& v) {
    for (size_t i = 1; i < v.size(); ++i) if (v[i - 1] > v[i]) return false;
    return true;
}

static bool sameMultiset(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() != b.size()) return false;
    std::vector<int> counts(cfg::VALUE_MAX - cfg::VALUE_MIN + 1, 0);
    for (int x : b) {
        if (x < cfg::VALUE_MIN || x > cfg::VALUE_MAX) return false;
        ++counts[x - cfg::VALUE_MIN];
    }
    for (int x : a) {
        if (x < cfg::VALUE_MIN || x > cfg::VALUE_MAX) return false;
        if (--counts[x - cfg::VALUE_MIN] < 0) return false;
    }
    return true;
}

struct Trial { double time; Metrics metrics; };
static const Trial& medianTrial(std::vector<Trial>& trials) {
    for (size_t i = 1; i < trials.size(); ++i) {
        Trial value = trials[i];
        size_t j = i;
        while (j > 0 && trials[j - 1].time > value.time) {
            trials[j] = trials[j - 1];
            --j;
        }
        trials[j] = value;
    }
    return trials[trials.size() / 2];
}

static int repsFor(size_t n) { return n <= 1000 ? 15 : 3; }

static bool benchSorts(const std::vector<size_t>& sizes, const std::vector<Dist>& distributions,
                       FILE* csv, const std::vector<SortEntry>& sorts = SORTS) {
    std::fprintf(csv, "algorithm,N,distribution,time_us,min_time_us,max_time_us,comparisons,swaps,writes,aux_bytes,trials,correct,seed\n");
    bool allCorrect = true;
    for (const auto& s : sorts)
        for (size_t n : sizes)
            for (Dist d : distributions) {
                const std::vector<int> original = generate(n, d);
                std::vector<Trial> trials;
                bool correct = true;
                for (int r = 0; r < repsFor(n); ++r) {
                    std::vector<int> work = original;
                    Metrics m;
                    auto t0 = Clock::now();
                    s.fn(work, m);
                    const double time = usSince(t0);
                    const bool valid = sortedAsc(work) && sameMultiset(work, original) && m.curAux == 0;
                    correct = valid && correct;
                    trials.push_back({time, m});
                }
                const Trial& med = medianTrial(trials);
                const Metrics& m = med.metrics;
                allCorrect = correct && allCorrect;
                std::fprintf(csv, "%s,%zu,%s,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%zu,%s,%u\n",
                    s.name, n, distName(d), med.time, trials.front().time, trials.back().time,
                    static_cast<unsigned long long>(m.comparisons), static_cast<unsigned long long>(m.swaps),
                    static_cast<unsigned long long>(m.writes), static_cast<unsigned long long>(m.auxBytes),
                    trials.size(), correct ? "yes" : "NO", cfg::SEED);
                std::printf("[SORT] %s N=%zu %s median=%.6f us trials=%zu %s\n",
                            s.name, n, distName(d), med.time, trials.size(), correct ? "OK" : "WRONG OUTPUT OR METRICS");
                std::fflush(stdout);
            }
    return allCorrect;
}

using SearchFn = int (*)(const std::vector<int>&, int, Metrics&);
static constexpr int SEARCH_TRIALS = 5;

static bool validSearchResult(const std::vector<int>& a, int key, int result,
                              int firstMatch, bool requireFirstMatch) {
    if (firstMatch == NOT_FOUND || requireFirstMatch) return result == firstMatch;
    return result >= 0 && static_cast<size_t>(result) < a.size() && a[result] == key;
}

static bool runCase(FILE* csv, const char* alg, SearchFn fn, size_t n, Dist d, const char* cname,
                    const std::vector<int>& a, const std::vector<int>& keys, int reps,
                    bool requireFirstMatch, uint64_t datasetPrecheckComparisons = 0) {
    const size_t total = keys.size() * static_cast<size_t>(reps);
    std::vector<int> expected;
    for (int key : keys) {
        int first = NOT_FOUND;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] == key) { first = static_cast<int>(i); break; }
        expected.push_back(first);
    }
    std::vector<int> results(total);
    std::vector<Trial> trials;
    bool correct = true;
    for (int trial = 0; trial < SEARCH_TRIALS; ++trial) {
        Metrics m;
        size_t index = 0;
        // Capture every timed result so validation cannot miss an intermittent wrong return.
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r)
            for (int key : keys) results[index++] = fn(a, key, m);
        const double time = usSince(t0) / static_cast<double>(total);
        for (size_t i = 0; i < total; ++i) {
            const size_t query = i % keys.size();
            const bool valid = validSearchResult(a, keys[query], results[i], expected[query], requireFirstMatch);
            correct = valid && correct;
        }
        correct = (m.curAux == 0 && m.precheckComparisons == 0) && correct;
        trials.push_back({time, m});
    }
    const Trial& med = medianTrial(trials);
    const Metrics& m = med.metrics;
    const bool binary = !requireFirstMatch;
    std::fprintf(csv, "%s,%zu,%s,%s,%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%s,%zu,%zu,%u\n",
        alg, n, distName(d), cname, correct ? "OK" : "INCORRECT",
        binary ? "core_after_precheck" : "search",
        med.time, trials.front().time, trials.back().time,
        static_cast<double>(m.comparisons) / total, static_cast<double>(m.swaps) / total,
        static_cast<double>(m.writes) / total, static_cast<unsigned long long>(m.auxBytes),
        static_cast<unsigned long long>(datasetPrecheckComparisons),
        binary ? "once_per_dataset" : "not_applicable", trials.size(), total, cfg::SEED);
    std::printf("[SEARCH] %s N=%zu %s %s median=%.6f us/query cmp=%.6f precheck=%llu %s\n",
        alg, n, distName(d), cname, med.time, static_cast<double>(m.comparisons) / total,
        static_cast<unsigned long long>(datasetPrecheckComparisons), correct ? "OK" : "INCORRECT");
    return correct;
}

static bool benchSearches(const std::vector<size_t>& sizes, const std::vector<Dist>& distributions, FILE* csv) {
    std::fprintf(csv, "algorithm,N,distribution,case,status,scope,avg_time_us,min_time_us,max_time_us,avg_comparisons,avg_swaps,avg_writes,aux_bytes,precheck_comparisons,precheck_scope,trials,queries_per_trial,seed\n");
    const int absent = cfg::VALUE_MAX + 1;
    bool allCorrect = true;
    for (size_t n : sizes)
        for (Dist d : distributions) {
            const std::vector<int> a = generate(n, d);
            Metrics pm;
            const bool sorted = isSortedAscending(a, pm);
            if (a.empty()) {
                const bool linear = runCase(csv, "Linear Search", linearSearch, n, d, "empty", a, {absent}, 1000, true);
                const bool binary = runCase(csv, "Binary Search", binarySearchCore, n, d, "empty", a, {absent}, 1000, false, pm.precheckComparisons);
                allCorrect = linear && binary && allCorrect;
                continue;
            }
            std::mt19937 rng(cfg::SEED + 7);
            std::uniform_int_distribution<size_t> pick(0, n - 1);
            std::vector<int> avgKeys;
            for (int i = 0; i < 1000; ++i) avgKeys.push_back(a[pick(rng)]);

            const bool best = runCase(csv, "Linear Search", linearSearch, n, d, "best", a, {a[0]}, 100000, true);
            const bool average = runCase(csv, "Linear Search", linearSearch, n, d, "average", a, avgKeys, 10, true);
            const bool worst = runCase(csv, "Linear Search", linearSearch, n, d, "worst", a, {absent}, 1000, true);
            allCorrect = best && average && worst && allCorrect;
            if (!sorted) {
                std::fprintf(csv, "Binary Search,%zu,%s,all,REJECTED,not_executed,,,,,,,,%llu,once_per_dataset,0,0,%u\n",
                             n, distName(d), static_cast<unsigned long long>(pm.precheckComparisons), cfg::SEED);
                std::printf("[SEARCH] Binary Search N=%zu %s REJECTED precheck=%llu\n",
                            n, distName(d), static_cast<unsigned long long>(pm.precheckComparisons));
                continue;
            }
            const bool binaryBest = runCase(csv, "Binary Search", binarySearchCore, n, d, "best", a, {a[(n - 1) / 2]}, 100000, false, pm.precheckComparisons);
            const bool binaryAverage = runCase(csv, "Binary Search", binarySearchCore, n, d, "average", a, avgKeys, 10, false, pm.precheckComparisons);
            const bool binaryWorst = runCase(csv, "Binary Search", binarySearchCore, n, d, "worst", a, {absent}, 1000, false, pm.precheckComparisons);
            allCorrect = binaryBest && binaryAverage && binaryWorst && allCorrect;
        }
    return allCorrect;
}

static int runBenchmark(int argc, char** argv) {
    size_t maxN = 100000;
    bool extended = false, hasMaxN = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--extended") == 0 && !extended) {
            extended = true;
        } else {
            const char* end = argv[i] + std::strlen(argv[i]);
            const auto result = std::from_chars(argv[i], end, maxN);
            if (hasMaxN || result.ec != std::errc{} || result.ptr != end) {
                std::fprintf(stderr, "Usage: %s [nonnegative maxN] [--extended]\n", argv[0]);
                return 1;
            }
            hasMaxN = true;
        }
    }
    std::vector<size_t> sizes;
    if (extended)
        for (size_t n : {0u, 1u, 2u, 3u, 10u}) if (n <= maxN) sizes.push_back(n);
    for (size_t n : {100u, 1000u, 10000u, 100000u}) if (n <= maxN) sizes.push_back(n);
    if (sizes.empty()) {
        std::fprintf(stderr, "No required sizes selected. Use maxN >= 100, or --extended for small inputs.\n");
        return 1;
    }
    std::vector<Dist> distributions;
    for (Dist d : REQUIRED_DISTRIBUTIONS) distributions.push_back(d);
    if (extended)
        for (Dist d : EXTRA_DISTRIBUTIONS) distributions.push_back(d);

    for (const char* path : {"search_results.csv", "sort_results.csv"}) {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error || exists) {
            std::fprintf(stderr, "Cannot create %s: use a new writable output directory; existing results are never overwritten.\n", path);
            return 1;
        }
    }
    FILE* searchCsv = std::fopen("search_results.csv", "wx");
    if (!searchCsv) { std::perror("search_results.csv"); return 1; }
    FILE* sortCsv = std::fopen("sort_results.csv", "wx");
    if (!sortCsv) { std::perror("sort_results.csv"); std::fclose(searchCsv); return 1; }
    std::printf("=== Algorithms Benchmark | suite=%s | datasets=%zu | maxN=%zu | range=[%d,%d] | seed=%u | registered sorts=%zu | search trials=%d ===\n",
                extended ? "extended" : "required", sizes.size() * distributions.size(), maxN,
                cfg::VALUE_MIN, cfg::VALUE_MAX, cfg::SEED, SORTS.size(), SEARCH_TRIALS);
    std::printf("Timing includes instrumentation and search-result capture; generation, copies, validation and binary precheck are untimed.\n");
    if (SORTS.size() != 7)
        std::printf("INCOMPLETE TEAM REGISTRY: %zu/7 sorts available; search results remain usable.\n", SORTS.size());
    const bool searchesCorrect = benchSearches(sizes, distributions, searchCsv);
    const bool sortsCorrect = benchSorts(sizes, distributions, sortCsv);
    const bool searchWriteFailed = std::ferror(searchCsv) != 0;
    const bool sortWriteFailed = std::ferror(sortCsv) != 0;
    const int searchClose = std::fclose(searchCsv);
    const int sortClose = std::fclose(sortCsv);
    if (searchWriteFailed || sortWriteFailed || searchClose != 0 || sortClose != 0) {
        std::fprintf(stderr, "Failed to save complete benchmark CSV output.\n");
        return 1;
    }
    const bool correct = searchesCorrect && sortsCorrect;
    std::printf("=== %s: search_results.csv, sort_results.csv ===\n", correct ? "done" : "FAILED correctness checks");
    return correct ? 0 : 1;
}

#ifdef _WIN32
static std::filesystem::path createDesktopRunDirectory(const std::filesystem::path& executable) {
    const auto parent = executable.parent_path() / "results";
    std::filesystem::create_directories(parent);
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const std::string prefix = "desktop-" + std::to_string(stamp) + "-" + std::to_string(GetCurrentProcessId());
    for (unsigned attempt = 0; ; ++attempt) {
        const auto directory = parent / (prefix + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(directory)) return directory;
    }
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    DWORD processIds[2];
    // Only a newly opened console should change output location or wait for input.
    if (GetConsoleProcessList(processIds, 2) == 1) {
        int result = 1;
        try {
            std::wstring executable(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (length == 0 || length >= executable.size())
                throw std::runtime_error("Cannot locate benchmark.exe.");
            executable.resize(length);
            const auto directory = createDesktopRunDirectory(executable);
            std::filesystem::current_path(directory);
            std::printf("Results folder: %s\nExcel-readable CSV files (not .xlsx).\n", directory.u8string().c_str());
            result = runBenchmark(argc, argv);
            if (result == 0) {
                std::printf("\nCSV files saved in: %s\nOpen search_results.csv in Excel.\n", directory.u8string().c_str());
                const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                if (opened <= 32)
                    std::fprintf(stderr, "Could not open File Explorer. The CSV files are saved in the folder above.\n");
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Benchmark failed: %s\n", error.what());
        }
        std::printf("\n%s Press Enter to close this window.\n", result == 0 ? "Finished." : "FAILED. See the error above.");
        std::fflush(stdout);
        std::getchar();
        return result;
    }
#endif
    return runBenchmark(argc, argv);
}
