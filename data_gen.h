#pragma once
#include <cstddef>
#include <random>
#include <vector>
#include "metrics.h"

enum class Dist {
    Random, Sorted, Reverse, NearlySorted, AllEqual,
    FewUnique, Alternating, OrganPipe, Sawtooth, Skewed
};

inline constexpr Dist REQUIRED_DISTRIBUTIONS[] = {
    Dist::Random, Dist::Sorted, Dist::Reverse
};
inline constexpr Dist EXTRA_DISTRIBUTIONS[] = {
    Dist::NearlySorted, Dist::AllEqual, Dist::FewUnique, Dist::Alternating,
    Dist::OrganPipe, Dist::Sawtooth, Dist::Skewed
};

inline const char* distName(Dist d) {
    switch (d) {
        case Dist::Random:       return "random";
        case Dist::Sorted:       return "sorted";
        case Dist::Reverse:      return "reverse";
        case Dist::NearlySorted: return "nearly_sorted";
        case Dist::AllEqual:     return "all_equal";
        case Dist::FewUnique:    return "few_unique";
        case Dist::Alternating:  return "alternating";
        case Dist::OrganPipe:    return "organ_pipe";
        case Dist::Sawtooth:     return "sawtooth";
        case Dist::Skewed:       return "skewed";
    }
    return "unknown";
}

inline std::vector<int> generate(std::size_t n, Dist d, unsigned seed = cfg::SEED) {
    std::vector<int> v(n);
    if (n == 0) return v;

    const long long span = static_cast<long long>(cfg::VALUE_MAX) - cfg::VALUE_MIN;
    std::mt19937 rng(seed + static_cast<unsigned>(n));
    std::uniform_int_distribution<int> uniform(cfg::VALUE_MIN, cfg::VALUE_MAX);

    switch (d) {
        case Dist::Random:
            for (int& x : v) x = uniform(rng);
            break;

        case Dist::Sorted:
        case Dist::Reverse:
        case Dist::NearlySorted:
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t index = (d == Dist::Reverse) ? n - 1 - i : i;
                const long long offset = (n > 1)
                    ? static_cast<long long>(index) * span / static_cast<long long>(n - 1) : 0;
                v[i] = static_cast<int>(cfg::VALUE_MIN + offset);
            }
            if (d == Dist::NearlySorted) {
                // Disjoint adjacent swaps prevent repeated swaps from undoing the perturbation.
                for (std::size_t start = 0; start + 1 < n; start += 20) {
                    const std::size_t width = (n - start < 20) ? n - start : 20;
                    std::uniform_int_distribution<std::size_t> pick(start, start + width - 2);
                    const std::size_t i = pick(rng);
                    const int temp = v[i]; v[i] = v[i + 1]; v[i + 1] = temp;
                }
            }
            break;

        case Dist::AllEqual:
            for (int& x : v) x = static_cast<int>(cfg::VALUE_MIN + span / 2);
            break;

        case Dist::FewUnique: {
            std::uniform_int_distribution<int> pick(0, 4);
            for (int& x : v) x = static_cast<int>(cfg::VALUE_MIN + pick(rng) * span / 4);
            break;
        }

        case Dist::Alternating:
            for (std::size_t i = 0; i < n; ++i)
                v[i] = (i % 2 == 0) ? cfg::VALUE_MIN : cfg::VALUE_MAX;
            break;

        case Dist::OrganPipe: {
            const std::size_t peak = (n - 1) / 2;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t distance = (i < n - 1 - i) ? i : n - 1 - i;
                const long long offset = (peak > 0)
                    ? static_cast<long long>(distance) * span / static_cast<long long>(peak) : 0;
                v[i] = static_cast<int>(cfg::VALUE_MIN + offset);
            }
            break;
        }

        case Dist::Sawtooth:
            for (std::size_t i = 0; i < n; ++i)
                v[i] = static_cast<int>(cfg::VALUE_MIN + static_cast<long long>(i % 10) * span / 9);
            break;

        case Dist::Skewed: {
            std::uniform_int_distribution<int> choose(0, 9);
            std::uniform_int_distribution<int> cluster(
                cfg::VALUE_MIN, static_cast<int>(cfg::VALUE_MIN + span / 100));
            for (int& x : v) x = (choose(rng) == 0) ? uniform(rng) : cluster(rng);
            break;
        }
    }
    return v;
}
