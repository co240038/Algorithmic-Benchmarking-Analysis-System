#include "../sorts.h"

void countingSort(std::vector<int>& a, Metrics& m) {
    if (a.empty()) return;

    const int range = cfg::VALUE_MAX - cfg::VALUE_MIN + 1;

    std::vector<int> count(range, 0);
    std::vector<int> output(a.size());

    m.allocAux(sizeof(int) * count.size());
    m.allocAux(sizeof(int) * output.size());

    for (int value : a) {
        ++count[value - cfg::VALUE_MIN];
    }

    for (int i = 1; i < range; ++i) {
        count[i] += count[i - 1];
    }

    for (int i = static_cast<int>(a.size()) - 1; i >= 0; --i) {
        const int value = a[i];
        --count[value - cfg::VALUE_MIN];
        m.put(output, count[value - cfg::VALUE_MIN], value);
    }

    for (size_t i = 0; i < a.size(); ++i) {
        m.put(a, i, output[i]);
    }

    m.freeAux(sizeof(int) * output.size());
    m.freeAux(sizeof(int) * count.size());
}
