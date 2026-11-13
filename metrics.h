// metrics.h  -  SHARED HEADER for the Algorithms Final Project (Member 1: Aquino)
// Everyone codes against this. Do not change it without telling the group.
//
// COUNTING CONVENTIONS (so everyone's numbers are comparable)
//  1. Comparison : one evaluation of ==, <, <=, >, >= between two DATA values
//                  (element vs element, or element vs key). Use m.lt/m.le/m.gt/m.ge/m.eq.
//                  Loop-index tests (i < n) are NOT counted.
//  2. Swap       : a full exchange of two array slots. Use m.swapAt(v, i, j).
//                  A swap counts 1 swap AND 2 writes.
//  3. Write      : every store into the ARRAY being sorted/output. Use m.put(v, i, x).
//                  Insertion/Shell shifts are writes, NOT swaps.
//                  Merge Sort: count the writes into the merged output, and the copies
//                  into temp buffers too (be consistent and say so in your Ch. III).
//  4. Aux memory : call m.allocAux(bytes) when you allocate extra memory (temp arrays,
//                  count array, buckets) and m.freeAux(bytes) when you release it.
//                  The engine reports the PEAK. Use sizeof(int) * count.
//                  O(1) scalars (i, j, key, temp) are not counted.
//  5. Allowed    : no std::sort, std::stable_sort, std::binary_search, std::find, etc. anywhere in the project.
//                  std::vector, std::swap-free manual code, and <cstring> are fine.
//
// VALUE RANGE (Counting Sort and Bucket Sort): every generated value is an int in
// [VALUE_MIN, VALUE_MAX]. Counting sort's k = VALUE_MAX - VALUE_MIN + 1.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace cfg {
constexpr int      VALUE_MIN = 0;
constexpr int      VALUE_MAX = 100000;
constexpr unsigned SEED      = 2026;   // fixed seed -> reproducible runs
}

struct Metrics {
    uint64_t comparisons = 0;
    uint64_t swaps = 0;
    uint64_t writes = 0;
    uint64_t auxBytes = 0;             // PEAK auxiliary bytes
    uint64_t precheckComparisons = 0;  // Binary Search's sortedness check (kept separate)
    uint64_t curAux = 0;

    void reset() { *this = Metrics(); }

    // --- counted comparisons ---
    bool lt(int a, int b) { ++comparisons; return a <  b; }
    bool le(int a, int b) { ++comparisons; return a <= b; }
    bool gt(int a, int b) { ++comparisons; return a >  b; }
    bool ge(int a, int b) { ++comparisons; return a >= b; }
    bool eq(int a, int b) { ++comparisons; return a == b; }

    // --- counted writes / swaps ---
    void put(std::vector<int>& v, size_t i, int x) { v[i] = x; ++writes; }
    void swapAt(std::vector<int>& v, size_t i, size_t j) {
        int t = v[i]; v[i] = v[j]; v[j] = t;
        ++swaps; writes += 2;
    }

    // --- auxiliary memory ---
    void allocAux(uint64_t bytes) { curAux += bytes; if (curAux > auxBytes) auxBytes = curAux; }
    void freeAux(uint64_t bytes)  { curAux = (bytes > curAux) ? 0 : curAux - bytes; }
};
