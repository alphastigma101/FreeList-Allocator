#include "bitmap.h"
#include "../logger/buffer.h"
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
    bitmap.bits[idx / 8] |= (1 << (idx % 8));         /* mark FREE: set the bit */
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
    unsigned long data   = *(unsigned long*)(bitmap.bits + start / 8);   /* FIX: unsigned long, not unsigned int */
    size_t    range      = end - start;
    size_t    bit_range  = range;                                        /* FIX: range is already in BITS now */
    unsigned long range_mask = (bit_range >= sizeof(unsigned long) * 8)
                         ? ~(unsigned long)0
                         : ((unsigned long)1 << bit_range) - 1;

    unsigned long target_bits = data & range_mask;
    if (target_bits == 0) return -1;

    return start + (__builtin_ffsl((long)target_bits) - 1);
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