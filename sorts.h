// sorts.h - agreed signatures for every sorting module.
// Each owner writes ONE file in sorts/ named exactly as shown and defines the function below.
// Sort IN PLACE on `a` (ascending). Report costs ONLY through `m` (see metrics.h).
#pragma once
#include <vector>
#include "metrics.h"

void bubbleSort   (std::vector<int>& a, Metrics& m);   // sorts/bubble_sort.cpp     (Yu)
void selectionSort(std::vector<int>& a, Metrics& m);   // sorts/selection_sort.cpp  (Yu)
void insertionSort(std::vector<int>& a, Metrics& m);   // sorts/insertion_sort.cpp  (Gler)
void shellSort    (std::vector<int>& a, Metrics& m);   // sorts/shell_sort.cpp      (Gler)
void mergeSort    (std::vector<int>& a, Metrics& m);   // sorts/merge_sort.cpp      (Epino)
void countingSort (std::vector<int>& a, Metrics& m);   // sorts/counting_sort.cpp   (Epino)
void bucketSort   (std::vector<int>& a, Metrics& m);   // sorts/bucket_sort.cpp     (Concepcion)
