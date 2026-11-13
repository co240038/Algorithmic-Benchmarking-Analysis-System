#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "data_gen.h"
#include "searches.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool ascending(const std::vector<int>& a) {
    for (std::size_t i = 1; i < a.size(); ++i)
        if (a[i - 1] > a[i]) return false;
    return true;
}

static Metrics seededMetrics() {
    Metrics m;
    m.comparisons = 17; m.swaps = 3; m.writes = 5;
    m.auxBytes = 64; m.precheckComparisons = 11; m.curAux = 24;
    return m;
}

static void checkMetrics(const Metrics& actual, const Metrics& expected, const char* message) {
    check(actual.comparisons == expected.comparisons && actual.swaps == expected.swaps &&
          actual.writes == expected.writes && actual.auxBytes == expected.auxBytes &&
          actual.precheckComparisons == expected.precheckComparisons && actual.curAux == expected.curAux,
          message);
}

static void checkSearchMetrics(const Metrics& m, const Metrics& initial,
                               uint64_t comparisons, uint64_t precheckComparisons) {
    check(m.comparisons == initial.comparisons + comparisons, "exact accumulated search comparisons");
    check(m.precheckComparisons == initial.precheckComparisons + precheckComparisons,
          "exact accumulated first-descent precheck comparisons");
    check(m.swaps == initial.swaps && m.writes == initial.writes &&
          m.auxBytes == initial.auxBytes && m.curAux == initial.curAux,
          "search adds no swaps, writes, peak or current auxiliary bytes");
}

struct BinaryReference {
    int index;
    uint64_t probes;
};

// Independent recursive subtree lengths, rather than the production inclusive lo/hi loop.
static BinaryReference referenceBinary(const std::vector<int>& a, int key,
                                       std::size_t first, std::size_t count) {
    if (count == 0) return {NOT_FOUND, 0};
    const std::size_t leftCount = (count - 1) / 2;
    const std::size_t pivot = first + leftCount;
    if (key == a[pivot]) return {static_cast<int>(pivot), 1};
    BinaryReference result = key < a[pivot]
        ? referenceBinary(a, key, first, leftCount)
        : referenceBinary(a, key, pivot + 1, count - leftCount - 1);
    ++result.probes;
    return result;
}

static void checkSearches(const std::vector<int>& a, const std::vector<int>& keys) {
    const auto original = a;
    const bool sorted = ascending(a);
    std::size_t precheckCost = a.empty() ? 0 : a.size() - 1;
    for (std::size_t i = 1; i < a.size(); ++i) {
        if (a[i] < a[i - 1]) { precheckCost = i; break; }
    }
    for (const Metrics& initial : {Metrics(), seededMetrics()}) {
        Metrics m = initial;
        check(isSortedAscending(a, m) == sorted, "direct precheck accepts exactly nondecreasing input");
        checkSearchMetrics(m, initial, 0, precheckCost);
        check(a == original, "precheck leaves input unchanged");

        for (int key : keys) {
            int expected = NOT_FOUND;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (a[i] == key) { expected = static_cast<int>(i); break; }
            }
            m = initial;
            check(linearSearch(a, key, m) == expected, "linear search returns first match or NOT_FOUND");
            checkSearchMetrics(m, initial,
                               expected == NOT_FOUND ? a.size() : static_cast<std::size_t>(expected) + 1, 0);
            check(a == original, "linear search leaves input unchanged");

            m = initial;
            const int result = binarySearch(a, key, m);
            check(a == original, "binary search leaves input unchanged");
            if (!sorted) {
                check(result == PRECONDITION_FAILED, "binary search rejects unsorted input");
                checkSearchMetrics(m, initial, 0, precheckCost);
            } else {
                const auto reference = referenceBinary(a, key, 0, a.size());
                // Each failed probe costs equality + ordering; a hit costs only equality.
                const uint64_t comparisons = 2 * reference.probes - (reference.index != NOT_FOUND ? 1 : 0);
                check(result == reference.index, "binary search follows the lower-median probe path");
                checkSearchMetrics(m, initial, comparisons, precheckCost);
                if (expected == NOT_FOUND) {
                    check(result == NOT_FOUND, "binary search reports missing keys");
                } else {
                    check(result >= 0 && static_cast<std::size_t>(result) < a.size() && a[result] == key,
                          "binary search returns a matching index");
                }
                m = initial;
                check(binarySearchCore(a, key, m) == reference.index, "direct core returns the reference index");
                checkSearchMetrics(m, initial, comparisons, 0);
                check(a == original, "binary core leaves input unchanged");
            }
        }
    }
}

static void checkSearches(const std::vector<int>& a) {
    std::vector<int> keys = {cfg::VALUE_MIN - 1, cfg::VALUE_MAX + 1,
                             cfg::VALUE_MIN, cfg::VALUE_MAX};
    if (!a.empty()) {
        keys.push_back(a.front());
        keys.push_back(a[a.size() / 2]);
        keys.push_back(a.back());
    }
    checkSearches(a, keys);
}

static void checkDistribution(std::size_t n, Dist d, unsigned seed) {
    const auto a = generate(n, d, seed);
    check(a.size() == n, "generator preserves requested size");
    check(a == generate(n, d, seed), "same size/distribution/seed reproduces data");
    check(std::strcmp(distName(d), "unknown") != 0, "every distribution has a CSV name");
    for (int x : a)
        check(x >= cfg::VALUE_MIN && x <= cfg::VALUE_MAX, "all values stay within the shared range");
    checkSearches(a);
    if (a.empty()) return;

    const long long span = static_cast<long long>(cfg::VALUE_MAX) - cfg::VALUE_MIN;
    switch (d) {
        case Dist::Random: {
            std::mt19937 rng(seed + static_cast<unsigned>(n));
            std::uniform_int_distribution<int> uniform(cfg::VALUE_MIN, cfg::VALUE_MAX);
            for (int x : a) check(x == uniform(rng), "required random sequence remains unchanged");
            break;
        }
        case Dist::Sorted:
        case Dist::Reverse:
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t index = (d == Dist::Reverse) ? n - 1 - i : i;
                const long long offset = (n > 1)
                    ? static_cast<long long>(index) * span / static_cast<long long>(n - 1) : 0;
                check(a[i] == cfg::VALUE_MIN + offset, "required sorted/reverse values remain unchanged");
            }
            break;
        case Dist::NearlySorted: {
            const auto ordered = generate(n, Dist::Sorted, seed);
            std::vector<int> counts(static_cast<std::size_t>(span + 1), 0);
            for (int x : ordered) ++counts[x - cfg::VALUE_MIN];
            for (int x : a) --counts[x - cfg::VALUE_MIN];
            for (int count : counts) check(count == 0, "nearly sorted preserves the input multiset");
            std::size_t changed = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (a[i] != ordered[i]) ++changed;
                check(a[i] == ordered[i] || (i > 0 && a[i] == ordered[i - 1]) ||
                      (i + 1 < n && a[i] == ordered[i + 1]), "nearly sorted only moves adjacent values");
            }
            check(changed <= 2 * ((n + 19) / 20), "nearly sorted limits the number of changed positions");
            if (n > 1 && n <= static_cast<std::size_t>(span + 1))
                check(!ascending(a), "nearly sorted perturbs distinct ascending input");
            break;
        }
        case Dist::AllEqual:
            for (int x : a) check(x == cfg::VALUE_MIN + span / 2, "all-equal values use the midpoint");
            break;
        case Dist::FewUnique:
            for (int x : a) {
                bool allowed = false;
                for (int level = 0; level < 5; ++level)
                    if (x == cfg::VALUE_MIN + level * span / 4) allowed = true;
                check(allowed, "few-unique values belong to the five-value palette");
            }
            break;
        case Dist::Alternating:
            for (std::size_t i = 0; i < n; ++i)
                check(a[i] == (i % 2 == 0 ? cfg::VALUE_MIN : cfg::VALUE_MAX), "alternating endpoints");
            break;
        case Dist::OrganPipe:
            check(a.front() == cfg::VALUE_MIN && a.back() == cfg::VALUE_MIN, "organ-pipe endpoints");
            if (n > 2) check(a[(n - 1) / 2] == cfg::VALUE_MAX, "organ-pipe peak");
            for (std::size_t i = 0; i < n; ++i) {
                check(a[i] == a[n - 1 - i], "organ-pipe symmetry");
                if (i > 0 && i <= (n - 1) / 2) check(a[i - 1] <= a[i], "organ-pipe ascending half");
            }
            break;
        case Dist::Sawtooth:
            for (std::size_t i = 0; i < n; ++i) {
                if (i % 10 == 0) check(a[i] == cfg::VALUE_MIN, "sawtooth starts each run at minimum");
                if (i % 10 == 9) check(a[i] == cfg::VALUE_MAX, "sawtooth ends each run at maximum");
                if (i % 10 != 0) check(a[i - 1] <= a[i], "sawtooth runs ascend");
                if (i >= 10) check(a[i] == a[i - 10], "sawtooth period is ten");
            }
            break;
        case Dist::Skewed:
            if (n >= 10000) {
                std::size_t clustered = 0;
                for (int x : a) if (x <= cfg::VALUE_MIN + span / 100) ++clustered;
                check(clustered > n * 8 / 10 && clustered < n * 99 / 100,
                      "large skewed samples contain a dominant cluster and outliers");
            }
            break;
    }
}

static void checkMetricsHelpers() {
    Metrics m;
    const Metrics zero = {0, 0, 0, 0, 0, 0};
    checkMetrics(m, zero, "Metrics default construction zeroes all six fields");
    m = seededMetrics();
    Metrics expected = m;
    bool (Metrics::*const predicates[])(int, int) = {
        &Metrics::lt, &Metrics::le, &Metrics::gt, &Metrics::ge, &Metrics::eq
    };
    const int values[] = {std::numeric_limits<int>::min(), -1, 0, 1, std::numeric_limits<int>::max()};
    for (int a : values) {
        for (int b : values) {
            const bool results[] = {a < b, a <= b, a > b, a >= b, a == b};
            for (std::size_t i = 0; i < 5; ++i) {
                check((m.*predicates[i])(a, b) == results[i], "Metrics predicate returns the expected truth value");
                ++expected.comparisons;
                checkMetrics(m, expected, "each predicate adds exactly one comparison and no other counters");
            }
        }
    }

    std::vector<int> a = {4, 9};
    m.put(a, 0, -7);
    ++expected.writes;
    check(a == std::vector<int>({-7, 9}), "put changes only the requested slot");
    checkMetrics(m, expected, "put counts one write only");
    m.put(a, 0, -7);
    ++expected.writes;
    check(a == std::vector<int>({-7, 9}), "same-value put preserves values");
    checkMetrics(m, expected, "same-value put still counts one write");
    m.swapAt(a, 0, 1);
    ++expected.swaps; expected.writes += 2;
    check(a == std::vector<int>({9, -7}), "swap exchanges the two slots");
    checkMetrics(m, expected, "swap counts one swap and two writes only");
    m.swapAt(a, 1, 1);
    ++expected.swaps; expected.writes += 2;
    check(a == std::vector<int>({9, -7}), "self-swap preserves values");
    checkMetrics(m, expected, "self-swap still counts one swap and two writes");

    const struct { bool allocate; uint64_t bytes, current, peak; } allocations[] = {
        {true,  0, 24, 64},  // zero allocation
        {true, 16, 40, 64},  // below the existing peak
        {true, 40, 80, 80},  // overlapping allocations raise the peak
        {false, 16, 64, 80}, // partial free preserves the peak
        {true,  8, 72, 80},
        {false, 0, 72, 80},  // zero free
        {false, 72, 0, 80},  // balanced free
        {true,  8,  8, 80},
        {false, 9,  0, 80},  // over-free clamps a positive current count
        {false, 100, 0, 80}  // over-free also clamps an empty count
    };
    for (const auto& allocation : allocations) {
        if (allocation.allocate) m.allocAux(allocation.bytes);
        else                     m.freeAux(allocation.bytes);
        expected.curAux = allocation.current;
        expected.auxBytes = allocation.peak;
        checkMetrics(m, expected, "allocation/free preserves peak and clamps current without changing other counters");
    }
    m = seededMetrics();
    m.reset();
    checkMetrics(m, zero, "reset clears all six nonzero fields");
    m.reset();
    checkMetrics(m, zero, "reset is idempotent");
}

static void checkExhaustiveSearches() {
    const std::vector<int> keys = {std::numeric_limits<int>::min(), -2, -1, 0, 1, 2,
                                   std::numeric_limits<int>::max()};
    std::size_t vectors = 0, cases = 0, combinations = 1;
    for (std::size_t n = 0; n <= 9; ++n) {
        std::vector<int> a(n);
        for (std::size_t code = 0; code < combinations; ++code) {
            std::size_t digits = code;
            for (int& x : a) {
                x = static_cast<int>(digits % 3) - 1;
                digits /= 3;
            }
            checkSearches(a, keys);
            ++vectors;
            cases += keys.size();
        }
        combinations *= 3;
    }
    check(vectors == 29524 && cases == 206668, "exhaustive lengths 0..9 cover every ternary vector and seven keys");
}

static void checkTargetedSearches() {
    checkSearches({-10, -6, -2, 2, 6, 10, 14}, {-10, 2, 14, 0}); // first/middle/last hits, interior miss
    const int low = std::numeric_limits<int>::min(), high = std::numeric_limits<int>::max();
    checkSearches({low, low, -1, 0, high, high}, {low, -2, -1, 0, 1, 2, high});
    checkSearches({high, 0, low}, {low, -2, -1, 0, 1, 2, high});

    std::vector<int> a(100000);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = 2 * static_cast<int>(i);
    checkSearches(a, {0, 99998, 100000, 199998, -1, 99999, 200000});
    const auto original = a;
    Metrics m;
    check(binarySearch(a, 199998, m) == 99999, "N=100000 last hit returns the last index");
    checkSearchMetrics(m, Metrics(), 33, 99999);
    check(a == original, "N=100000 last hit leaves input unchanged");
    check(binarySearch(a, 200000, m) == NOT_FOUND, "N=100000 high miss returns NOT_FOUND");
    checkSearchMetrics(m, Metrics(), 33 + 34, 2 * 99999);
    check(a == original, "N=100000 consecutive search leaves input unchanged");

    a.back() = -1;
    checkSearches(a, {0, 100000, -1, 200000}); // reject only at the final adjacent pair
}

int main() {
    checkMetricsHelpers();
    checkExhaustiveSearches();
    checkTargetedSearches();

    std::vector<Dist> distributions;
    for (Dist d : REQUIRED_DISTRIBUTIONS) distributions.push_back(d);
    for (Dist d : EXTRA_DISTRIBUTIONS) distributions.push_back(d);
    for (std::size_t i = 0; i < distributions.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            check(std::strcmp(distName(distributions[i]), distName(distributions[j])) != 0,
                  "distribution names are unique");

    std::size_t cases = 0;
    for (Dist d : distributions)
        for (std::size_t n : {0u, 1u, 2u, 3u, 9u, 10u, 11u, 19u, 20u, 21u,
                             31u, 32u, 33u, 99u, 100u, 101u, 1000u, 10000u, 100000u, 100001u})
            for (unsigned seed : {0u, cfg::SEED, std::numeric_limits<unsigned>::max()}) {
                checkDistribution(n, d, seed);
                ++cases;
            }

    for (Dist d : {Dist::Random, Dist::NearlySorted, Dist::FewUnique, Dist::Skewed})
        check(generate(1000, d, 1) != generate(1000, d, 2), "different seeds vary randomized distributions");
    for (Dist d : {Dist::Sorted, Dist::Reverse, Dist::AllEqual, Dist::Alternating, Dist::OrganPipe, Dist::Sawtooth})
        check(generate(1000, d, 1) == generate(1000, d, 2), "fixed patterns do not depend on the seed");

    const std::vector<std::vector<int>> boundaries = {
        {}, {cfg::VALUE_MIN}, {cfg::VALUE_MAX}, {cfg::VALUE_MIN, cfg::VALUE_MAX},
        {cfg::VALUE_MAX, cfg::VALUE_MIN}, {cfg::VALUE_MIN, cfg::VALUE_MIN},
        {cfg::VALUE_MAX, cfg::VALUE_MAX}, {cfg::VALUE_MIN, cfg::VALUE_MAX, cfg::VALUE_MIN}
    };
    for (const auto& a : boundaries) checkSearches(a);

    check(cases == 600, "all existing generator cases remain covered");
    std::printf("PASS: %zu generator cases, seed variation, unique labels, and %zu explicit search edge cases; "
                "Metrics helpers, 29524 exhaustive vectors / 206668 key cases, and targeted N=100000 searches.\n",
                cases, boundaries.size());
    return 0;
}
