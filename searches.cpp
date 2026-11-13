#include "searches.h"

int linearSearch(const std::vector<int>& a, int key, Metrics& m) {
    for (size_t i = 0; i < a.size(); ++i)
        if (m.eq(a[i], key)) return (int)i;
    return NOT_FOUND;
}

bool isSortedAscending(const std::vector<int>& a, Metrics& m) {
    for (size_t i = 1; i < a.size(); ++i) {
        ++m.precheckComparisons;
        if (a[i - 1] > a[i]) return false;
    }
    return true;
}

int binarySearchCore(const std::vector<int>& a, int key, Metrics& m) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (m.eq(a[mid], key)) return mid;
        if (m.lt(a[mid], key)) lo = mid + 1;
        else                   hi = mid - 1;
    }
    return NOT_FOUND;
}

int binarySearch(const std::vector<int>& a, int key, Metrics& m) {
    if (!isSortedAscending(a, m)) return PRECONDITION_FAILED;
    return binarySearchCore(a, key, m);
}
