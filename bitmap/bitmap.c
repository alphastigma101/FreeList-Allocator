#include "bitmap.h"
#include <malloc.h>
#include <stdalign.h>
#include <stddef.h>
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
[[gnu::hot]]
[[gnu::pure]]
size_t bitmap_test(bitmap_t bitmap, size_t start, size_t end) {
    if (__builtin_expect(bitmap.n_bytes == 0, 0)) return (size_t)-1;

    const size_t max_word  = bitmap.n_bytes / sizeof(unsigned long) - 1;
    const size_t word_last = (end / 64 < max_word) ? end / 64 : max_word;
    const unsigned long* restrict words = __builtin_assume_aligned(bitmap.bits, alignof(unsigned long));

    #pragma GCC unroll 4
    for (size_t word_idx = start / 64; word_idx <= word_last; word_idx++) {
        if (word_idx + 1 <= word_last) __builtin_prefetch(&words[word_idx + 1], 0, 1);

        const unsigned long w  = words[word_idx];
        const size_t wlo = word_idx * 64;
        const size_t lo  = (start > wlo) ? (start - wlo) : 0;
        const size_t hi  = (end   < wlo + 63) ? (end - wlo) : 63;
        const unsigned long m  = (hi - lo + 1 >= 64) ? ~0UL : (((1UL << (hi - lo + 1)) - 1) << lo);
        const unsigned long a  = w & m;

        if (__builtin_expect(a != 0, 0)) return wlo + (size_t)__builtin_ctzl(a);
    }
    return (size_t)-1;
}


inline void init_bitmap_t(bitmap_t* bitmap, const size_t size) {
    int init = 0;
    const size_t threshold = M_MMAP_THRESHOLD;
    if (!bitmap) {
        bitmap = aligned_alloc(alignof(bitmap_t), sizeof(bitmap_t));
        if (!bitmap) return;
        memset(bitmap, 0, sizeof(bitmap_t));
        init = 1;
    }

    bitmap->bits = size > threshold ? mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : malloc(size) ;
    
    if (!bitmap->bits) {
        if (init) {
            memset(bitmap, 0, sizeof(bitmap_t));
            free(bitmap);
            bitmap = NULL;
        }
        return;
    }

    if (size > threshold) {
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

inline void bitmap_t_resize(bitmap_t* bitmap, const size_t size) {
    if (!bitmap) return;
    else if (bitmap->n_bytes > size) return;
    const size_t threshold = M_MMAP_THRESHOLD;

    void* old_bits = bitmap->bits;
    void* new_bits = bitmap->n_bytes > threshold ? mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : malloc(size);
    if (size > threshold) {
        int res = madvise(new_bits, size, MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) {
            munmap(new_bits, size);
            return;
        }
    }

    memset(new_bits, 0xFF, size);
    memcpy(new_bits, old_bits, bitmap->n_bytes);
    bitmap->n_bytes > threshold ? munmap(old_bits, bitmap->n_bytes) :  free(bitmap->bits);
    bitmap->bits = new_bits;
    bitmap->n_bytes = size;
}

inline void clean_bitmap(bitmap_t* bitmap) {
    if (!bitmap || !bitmap->bits) return;
    const size_t threshold = M_MMAP_THRESHOLD;
    void* bits = bitmap->bits;
    const size_t size = bitmap->n_bytes;
    memset(bitmap, 0, sizeof(bitmap_t));
    size > threshold ? munmap(bits, size) : free(bits);
}