// ============================================================================
// Shell Sort
// ============================================================================
//
// Sorts the list in place from smallest to largest.
// Shell Sort uses insertion sort with a decreasing gap.
// Each shift and final placement is counted as a write.

#include "../sorts.h"

using namespace std;

void shellSort(vector<int>& numberList, Metrics& metrics)
{
    int totalNumbers = (int)numberList.size();

    for(int gap = totalNumbers / 2; gap > 0; gap /= 2)
    {
        for(int i = gap; i < totalNumbers; i++)
        {
            int currentValue = numberList[i];
            int position = i;

            // Move larger values gap positions to the right
            while(position >= gap &&
                  metrics.gt(numberList[position - gap], currentValue))
            {
                metrics.put(numberList, position, numberList[position - gap]);
                position -= gap;
            }

            // Put the current value in its correct position
            metrics.put(numberList, position, currentValue);
        }
    }
}
