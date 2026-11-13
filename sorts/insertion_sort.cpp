// ============================================================================
// Insertion Sort
// ============================================================================
//
// Sorts the list in place from smallest to largest.
// Each shift and final placement is counted as a write.

#include "../sorts.h"

using namespace std;

void insertionSort(vector<int>& numberList, Metrics& metrics)
{
    int totalNumbers = (int)numberList.size();

    for(int i = 1; i < totalNumbers; i++)
    {
        int currentValue = numberList[i];
        int position = i;

        // Move larger values one position to the right
        while(position > 0 && metrics.gt(numberList[position - 1], currentValue))
        {
            metrics.put(numberList, position, numberList[position - 1]);
            position--;
        }

        // Put the current value in its correct position
        metrics.put(numberList, position, currentValue);
    }
}
