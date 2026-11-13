#include "../sorts.h"

void bubbleSort(std::vector<int>& arr, Metrics& m) {
    const int n = static_cast<int>(arr.size());
    for (int i = 0; i < n - 1; ++i) {
        bool swapped = false;
        for (int j = 0; j < n - i - 1; ++j) {
            if (m.gt(arr[j], arr[j + 1])) {
                m.swapAt(arr, j, j + 1);
                swapped = true;
            }
        }
        if (!swapped) break;
    }
}