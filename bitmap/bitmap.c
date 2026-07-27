#include "bitmap.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline
FORCE_INLINE int bitmap_test(bitmap_t bitmap, int start, int end);

/**
    * @description: A Free function that uses a specific bucket index to update its state. 
    * @param idx: the specific parameter determined from an index from allocator->bucket.small, medium and or large. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE bitmap_t bitmap_set(bitmap_t bitmap, int index, int start, int end) {
    if (index < start || index > end) return bitmap;
    bitmap.bits[index / 8] &= ~(1 << (index % 8));   /* mark USED: clear the bit */
    return bitmap;
}

/**
    * @description: A Free function that uses a specific bucket index to mark the bucket as open. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE bitmap_t bitmap_clear(bitmap_t bitmap, int idx, int start, int end) {
    if (idx < start || idx > end) return bitmap;
    bitmap.bits[idx / 8] |= (1 << (idx % 8));
    return bitmap;
}

/**
    * @description: A Free function that uses a specific range to test and see if any index is open.
    * @param bitmap: A user defined object that is being tested to see if there is a slot open.  
    * @param start: Also known as the lower bound i.e [0, 1000/2)
    * @param end: Also known as the upper bound i.e [1000/2, 3000)  
    * @return: Returns -1 if not found, otherwise, return a valid index
*/
FORCE_INLINE int bitmap_test(bitmap_t bitmap, int start, int end) {
   int word0 = start / 64;
    int max_word = (bitmap.n_bytes / 8) - 1;

    unsigned long w0 = (word0 + 0 <= max_word) ? *(unsigned long*)(bitmap.bits + (word0 + 0) * 8) : 0UL;
    unsigned long w1 = (word0 + 1 <= max_word) ? *(unsigned long*)(bitmap.bits + (word0 + 1) * 8) : 0UL;
    unsigned long w2 = (word0 + 2 <= max_word) ? *(unsigned long*)(bitmap.bits + (word0 + 2) * 8) : 0UL;
    unsigned long w3 = (word0 + 3 <= max_word) ? *(unsigned long*)(bitmap.bits + (word0 + 3) * 8) : 0UL;

    int wlo0 = (word0 + 0) * 64, whi0 = wlo0 + 63;
    int wlo1 = (word0 + 1) * 64, whi1 = wlo1 + 63;
    int wlo2 = (word0 + 2) * 64, whi2 = wlo2 + 63;
    int wlo3 = (word0 + 3) * 64, whi3 = wlo3 + 63;

    int lo0 = (start > wlo0) ? (start - wlo0) : 0, hi0 = (end < whi0) ? (end - wlo0) : 63;
    int lo1 = (start > wlo1) ? (start - wlo1) : 0, hi1 = (end < whi1) ? (end - wlo1) : 63;
    int lo2 = (start > wlo2) ? (start - wlo2) : 0, hi2 = (end < whi2) ? (end - wlo2) : 63;
    int lo3 = (start > wlo3) ? (start - wlo3) : 0, hi3 = (end < whi3) ? (end - wlo3) : 63;

    unsigned long mask0 = (lo0 > hi0) ? 0UL : ((hi0 - lo0 + 1 >= 64) ? ~0UL : (((1UL << (hi0 - lo0 + 1)) - 1) << lo0));
    unsigned long mask1 = (lo1 > hi1) ? 0UL : ((hi1 - lo1 + 1 >= 64) ? ~0UL : (((1UL << (hi1 - lo1 + 1)) - 1) << lo1));
    unsigned long mask2 = (lo2 > hi2) ? 0UL : ((hi2 - lo2 + 1 >= 64) ? ~0UL : (((1UL << (hi2 - lo2 + 1)) - 1) << lo2));
    unsigned long mask3 = (lo3 > hi3) ? 0UL : ((hi3 - lo3 + 1 >= 64) ? ~0UL : (((1UL << (hi3 - lo3 + 1)) - 1) << lo3));

    unsigned long m0 = w0 & mask0, m1 = w1 & mask1, m2 = w2 & mask2, m3 = w3 & mask3;

    unsigned long chosen = (m0 != 0) ? m0 : (m1 != 0) ? m1 : (m2 != 0) ? m2 : m3;
    int which          = (m0 != 0) ? 0  : (m1 != 0) ? 1  : (m2 != 0) ? 2  : (m3 != 0) ? 3 : -1;

    if (which == -1) return -1;

    int result = (word0 + which) * 64 + __builtin_ctzl(chosen);
    if (result < start || result > end) return -1;
    return result;
}


inline bitmap_t init_bitmap_t(const int size) {
    bitmap_t bitmap;
    memset(&bitmap, -1, sizeof(bitmap_t));
    bitmap.bits = malloc(size);
    if (!bitmap.bits) return bitmap;
    memset(bitmap.bits, 0xFF, size);
    bitmap.bitmap_set = &bitmap_set;
    bitmap.bitmap_clear = &bitmap_clear;
    bitmap.bitmap_test = &bitmap_test;
    bitmap.n_bytes = size;
    return bitmap;
}

inline void clean_bitmap(bitmap_t bitmap) {
    //sizeof(bitmap_t) > ALLOC_THRESHOLD ? unmap_cstr(1, bitmap.bits) : reset_and_free_cstr(1, bitmap.bits);
    free(bitmap.bits);
    memset(&bitmap, 0, sizeof(bitmap_t));
}