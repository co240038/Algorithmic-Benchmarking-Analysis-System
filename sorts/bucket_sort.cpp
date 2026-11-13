// ============================================================================
//  Bucket Sort
// ============================================================================
//
//  What the program does:
//    - Sorts a list of whole numbers from smallest to largest using Bucket Sort.
//    - Works on the list in place, so the sorted result ends up in the same list.
//
//  Terms:
//    - Bucket       : a small list that holds the numbers of one value range.
//    - Bucket index : the position of a bucket (0 = first bucket).
//    - Value range  : cfg::VALUE_MIN to cfg::VALUE_MAX (from metrics.h).
//
//  Steps used:
//    1. Make as many buckets as there are numbers. Each bucket covers an
//       equal slice of the value range, so smaller values go to earlier buckets.
//    2. Count how many numbers belong in each bucket, then size the buckets.
//    3. Put every number into the bucket its value belongs to.
//    4. Sort each bucket using insertion sort (each bucket is small).
//    5. Copy the buckets back into the list, from the first bucket to the last.
//
//  Counting rules used (see metrics.h):
//    * Comparison : every comparison of two data values, through metrics.gt().
//    * Write      : every store into a bucket or into the original list,
//                   through metrics.put().
//    * Auxiliary memory : the bucket list, the numbers stored in the buckets,
//                   and the list of bucket counts.
// ============================================================================

// ---------- Libraries ----------
#include "../sorts.h"   // gives us Metrics, cfg::VALUE_MIN/VALUE_MAX and vector

using namespace std;    // lets us write vector instead of std::vector

// ========================== HELPER FUNCTIONS ==========================
// 'static' keeps these helpers private to this file, so they cannot clash
// with helpers that have the same name in another member's sort file.

// Finds which bucket a value belongs to (0 = first bucket).
// long long is used so the multiplication cannot overflow an int.
static int findBucketIndex(int value, int totalBuckets)
{
    long long valueOffset = (long long)value - cfg::VALUE_MIN;               // distance from the smallest value
    long long rangeSize = (long long)cfg::VALUE_MAX - cfg::VALUE_MIN + 1;    // how many different values exist

    return (int)(valueOffset * totalBuckets / rangeSize);                    // always 0 to totalBuckets - 1
}

// Sorts one bucket with insertion sort.
static void sortOneBucket(vector<int> &bucket, Metrics &metrics)
{
    for(int i = 1; i < (int)bucket.size(); i++)
    {
        int currentValue = bucket[i];                 // the value to place in the sorted part
        int j = i - 1;                                // start checking the value on its left

        // Move larger values one place to the right to make room
        while(j >= 0 && metrics.gt(bucket[j], currentValue))
        {
            metrics.put(bucket, j + 1, bucket[j]);    // shift is a write, not a swap
            j--;
        }

        metrics.put(bucket, j + 1, currentValue);     // drop the value into its place
    }
}

// ========================== BUCKET SORT ==========================

void bucketSort(vector<int> &numberList, Metrics &metrics)
{
    int totalNumbers = (int)numberList.size();

    if(totalNumbers <= 1) return;                     // 0 or 1 numbers are already sorted

    int totalBuckets = totalNumbers;                  // one bucket for every number

    // ---------- Record the extra memory used ----------
    // bucket headers + numbers stored in the buckets + the list of bucket counts
    uint64_t auxiliaryBytes = sizeof(vector<int>) * totalBuckets
                            + sizeof(int) * totalNumbers
                            + sizeof(int) * totalBuckets;
    metrics.allocAux(auxiliaryBytes);

    // ---------- Step 1: count how many numbers go in each bucket ----------
    // Later this same list is reused to remember how many numbers were placed so far.
    vector<int> bucketCounts(totalBuckets, 0);

    for(int value : numberList)
    {
        bucketCounts[findBucketIndex(value, totalBuckets)]++;
    }

    // ---------- Step 2: make the buckets with the exact size they need ----------
    vector<vector<int>> buckets(totalBuckets);

    for(int bucketIndex = 0; bucketIndex < totalBuckets; bucketIndex++)
    {
        buckets[bucketIndex].resize(bucketCounts[bucketIndex]);
        bucketCounts[bucketIndex] = 0;                // now it counts the numbers placed so far
    }

    // ---------- Step 3: put every number into its bucket ----------
    for(int value : numberList)
    {
        int bucketIndex = findBucketIndex(value, totalBuckets);

        metrics.put(buckets[bucketIndex], bucketCounts[bucketIndex], value);
        bucketCounts[bucketIndex]++;                  // the next number goes to the next free slot
    }

    // ---------- Step 4: sort each bucket ----------
    for(int bucketIndex = 0; bucketIndex < totalBuckets; bucketIndex++)
    {
        sortOneBucket(buckets[bucketIndex], metrics);
    }

    // ---------- Step 5: copy the buckets back into the list, first to last ----------
    int writePosition = 0;                            // next position in the original list

    for(int bucketIndex = 0; bucketIndex < totalBuckets; bucketIndex++)
    {
        for(int i = 0; i < (int)buckets[bucketIndex].size(); i++)
        {
            metrics.put(numberList, writePosition, buckets[bucketIndex][i]);
            writePosition++;
        }
    }

    metrics.freeAux(auxiliaryBytes);                  // the buckets are released when the function ends
}