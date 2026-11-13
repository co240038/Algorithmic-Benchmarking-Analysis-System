#include "../sorts.h"

void selectionSort(std::vector<int>& arr, Metrics& m) {
    const int n = static_cast<int>(arr.size());
    for (int i = 0; i < n - 1; ++i) {
        int minIndex = i;
        for (int j = i + 1; j < n; ++j) {
            if (m.lt(arr[j], arr[minIndex])) {
                minIndex = j;
            }
        }
        if (minIndex != i) {
            m.swapAt(arr, i, minIndex);
        }
    }
}