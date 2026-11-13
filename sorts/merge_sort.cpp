#include "../sorts.h"

namespace {

void mergeRange(std::vector<int>& a, std::vector<int>& temp,
                int left, int mid, int right, Metrics& m) {
    int i = left;
    int j = mid + 1;
    int k = left;

    while (i <= mid && j <= right) {
        if (m.le(a[i], a[j])) {
            m.put(temp, k, a[i]);
            ++i;
        } else {
            m.put(temp, k, a[j]);
            ++j;
        }
        ++k;
    }

    while (i <= mid) {
        m.put(temp, k, a[i]);
        ++i;
        ++k;
    }

    while (j <= right) {
        m.put(temp, k, a[j]);
        ++j;
        ++k;
    }

    for (int p = left; p <= right; ++p) {
        m.put(a, p, temp[p]);
    }
}

void sortRange(std::vector<int>& a, std::vector<int>& temp,
               int left, int right, Metrics& m) {
    if (left >= right) return;

    int mid = left + (right - left) / 2;
    sortRange(a, temp, left, mid, m);
    sortRange(a, temp, mid + 1, right, m);
    mergeRange(a, temp, left, mid, right, m);
}

}

void mergeSort(std::vector<int>& a, Metrics& m) {
    if (a.size() < 2) return;

    std::vector<int> temp(a.size());
    m.allocAux(sizeof(int) * temp.size());

    sortRange(a, temp, 0, static_cast<int>(a.size()) - 1, m);

    m.freeAux(sizeof(int) * temp.size());
}
