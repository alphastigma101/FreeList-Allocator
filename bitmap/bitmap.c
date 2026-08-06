#include "bitmap.h"
#include "../logger/buffer.h"
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline
FORCE_INLINE size_t bitmap_test(bitmap_t bitmap, size_t start, size_t end);

/**
    * @description: A Free function that uses a specific bucket index to update its state. 
    * @param idx: the specific parameter determined from an index from allocator->bucket.small, medium and or large. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE bitmap_t bitmap_set(bitmap_t bitmap, size_t index, size_t start, size_t end) {
    if (index < start || index > end) return bitmap;
    bitmap.bits[index / CHAR_BIT] &= (unsigned char)~(1 << (index % CHAR_BIT));
    return bitmap;
}

/**
    * @description: A Free function that uses a specific bucket index to mark the bucket as open. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE bitmap_t bitmap_clear(bitmap_t bitmap, size_t idx, size_t start, size_t end) {
    if (idx < start || idx > end) return bitmap;
    bitmap.bits[idx / CHAR_BIT] |= (unsigned char)(1 << (idx % CHAR_BIT));
    return bitmap;
}

/**
    * @description: A Free function that uses a specific range to test and see if any index is open.
    * @param bitmap: A user defined object that is being tested to see if there is a slot open.  
    * @param start: Also known as the lower bound i.e [0, 1000/2)
    * @param end: Also known as the upper bound i.e [1000/2, 3000)  
    * @return: Returns -1 if not found, otherwise, return a valid index
*/
size_t bitmap_test(bitmap_t bitmap, size_t start, size_t end) {
    /* an empty bitmap has no free bits by definition -- guarded
       explicitly rather than letting bitmap.n_bytes/8 - 1 underflow to
       SIZE_MAX in unsigned arithmetic, which would make every
       word-in-range check below pass and read past whatever (possibly
       NULL) bitmap.bits actually points to */
    if (bitmap.n_bytes == 0) return (size_t)-1;

    size_t word0 = start / 64;
    size_t max_word = bitmap.n_bytes / CHAR_BIT - 1;

    unsigned long w0 = (word0+0<=max_word)?*(unsigned long*)(bitmap.bits+(word0+0)*8):0UL;
    unsigned long w1 = (word0+1<=max_word)?*(unsigned long*)(bitmap.bits+(word0+1)*8):0UL;
    unsigned long w2 = (word0+2<=max_word)?*(unsigned long*)(bitmap.bits+(word0+2)*8):0UL;
    unsigned long w3 = (word0+3<=max_word)?*(unsigned long*)(bitmap.bits+(word0+3)*8):0UL;

    /* word0/wloN/whiN are size_t throughout now, matching start/end --
       every comparison and subtraction below is same-type, so none of
       the explicit (size_t)/(int) casts the previous int-based version
       needed are required anymore. */
    size_t wlo0=(word0+0)*64, wlo1=(word0+1)*64, wlo2=(word0+2)*64, wlo3=(word0+3)*64;
    size_t whi0=wlo0+63, whi1=wlo1+63, whi2=wlo2+63, whi3=wlo3+63;

    size_t lo0 = (start > wlo0) ? (start - wlo0) : 0;
    size_t hi0 = (end   < whi0) ? (end   - wlo0) : 63;
    size_t lo1 = (start > wlo1) ? (start - wlo1) : 0;
    size_t hi1 = (end   < whi1) ? (end   - wlo1) : 63;
    size_t lo2 = (start > wlo2) ? (start - wlo2) : 0;
    size_t hi2 = (end   < whi2) ? (end   - wlo2) : 63;
    size_t lo3 = (start > wlo3) ? (start - wlo3) : 0;
    size_t hi3 = (end   < whi3) ? (end   - wlo3) : 63;

    unsigned long m0 = (lo0 > hi0) ? 0 : (( hi0 - lo0 + 1 >= 64) ? ~0UL : (((1UL << (hi0 - lo0 + 1)) - 1) << lo0));
    unsigned long m1=(lo1 > hi1)? 0: ((hi1- lo1 + 1 >= 64) ? ~0UL : (((1UL << (hi1- lo1 + 1)) - 1) << lo1));
    unsigned long m2=(lo2 > hi2)? 0: ((hi2 - lo2 + 1 >= 64) ? ~0UL : (((1UL << (hi2 - lo2 + 1)) - 1) << lo2));
    unsigned long m3=(lo3 > hi3)? 0: ((hi3 - lo3 + 1 >= 64) ? ~0UL : (((1UL <<( hi3 - lo3 + 1)) - 1) << lo3));

    unsigned long a0=w0&m0,a1=w1&m1,a2=w2&m2,a3=w3&m3;
    unsigned long chosen=(a0!=0)?a0:(a1!=0)?a1:(a2!=0)?a2:a3;
    int which=(a0!=0)?0:(a1!=0)?1:(a2!=0)?2:(a3!=0)?3:-1;
    if (which == -1) return (size_t)-1;

    size_t result=(word0+(size_t)which)*64+(size_t)__builtin_ctzl(chosen);
    /* which is 0-3 and word0 >= 0, so result is guaranteed non-negative
       and this addition can't wrap in any physically-realizable case */
    if (result < start || result > end) return (size_t)-1;
    return result;
}


inline void init_bitmap_t(bitmap_t* bitmap, const size_t size) {
    int init = 0;
    if (!bitmap) {
        bitmap = aligned_alloc(alignof(bitmap_t), sizeof(bitmap_t));
        if (!bitmap) return;
        memset(bitmap, 0, sizeof(bitmap_t));
        init = 1;
    }

    bitmap->bits = size < ALLOC_THRESHOLD ? malloc(size) : mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    
    if (!bitmap->bits) {
        if (init) {
            memset(bitmap, 0, sizeof(bitmap_t));
            free(bitmap);
            bitmap = NULL;
        }
        return;
    }

    if (size > ALLOC_THRESHOLD) {
        int res = madvise(bitmap->bits, size, MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) {
            munmap(bitmap->bits, size);
            bitmap->bits = NULL;
            return;
        }
    }
    
    memset(bitmap->bits, 0xFF, size);
    bitmap->bitmap_set = &bitmap_set;
    bitmap->bitmap_clear = &bitmap_clear;
    bitmap->bitmap_test = &bitmap_test;
    bitmap->n_bytes = size;
    return;
}

inline void clean_bitmap(bitmap_t* bitmap) {
    if (!bitmap || !bitmap->bits) return;

    memset(bitmap, 0, sizeof(bitmap_t));
    bitmap->n_bytes > ALLOC_THRESHOLD ? munmap(bitmap->bits, bitmap->n_bytes) :  free(bitmap->bits);
}