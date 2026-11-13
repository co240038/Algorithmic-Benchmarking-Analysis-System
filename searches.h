// searches.h - Linear Search and Binary Search (Member 1: Aquino)
#pragma once
#include <vector>
#include "metrics.h"

constexpr int NOT_FOUND = -1;
constexpr int PRECONDITION_FAILED = -2;   // Binary Search: input is not sorted ascending

// N must fit int; counters accumulate until the caller resets them.
int  linearSearch(const std::vector<int>& a, int key, Metrics& m);

// Sortedness check. Comparisons are recorded in m.precheckComparisons (not m.comparisons).
bool isSortedAscending(const std::vector<int>& a, Metrics& m);

// Binary search WITHOUT the check (used by the engine so timing measures only the search).
int  binarySearchCore(const std::vector<int>& a, int key, Metrics& m);

// Full version required by the guidelines: checks the precondition, then searches.
int  binarySearch(const std::vector<int>& a, int key, Metrics& m);
