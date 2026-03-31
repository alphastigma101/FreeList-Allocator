/**
 * @file  allocator.h
 * @brief Utility macros for the custom arena/bucket allocator.
 *
 * All macros operate on the global `allocator` instance unless an explicit
 * allocator pointer is supplied.  The naming convention follows the pattern:
 *
 *   ALLOC_<NOUN>_<VERB>   – operates on a structural member
 *   ALLOC_<VERB>          – top-level allocator operation
 *
 * Bucket tiers are sized as follows:
 *   small  : [1  – 64]   bytes   →  bucket.small[64]
 *   medium : [65 – 512]  bytes   →  bucket.medium[128]
 *   large  : [513 – ∞]   bytes   →  bucket.large[256]
*/
#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H
#include "../arena/arena.h"
#include <limits.h>
#include <stdalign.h>

#define SMALL_BIT_START 0
#define SMALL_BIT_END 64

#define MEDIUM_BIT_START 64
#define MEDIUM_BIT_END 192

#define LARGE_BIT_START 192
#define LARGE_BIT_END 448

#define BUCKET_SMALL_CAP 64U
#define BUCKET_MEDIUM_CAP 128U
#define BUCKET_LARGE_CAP 256U

#define BUCKET_SMALL_MAX 64U
#define BUCKET_MEDIUM_MAX 512U

typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ bitmap_t {
    uint8_t* bits;
    size_t n_bytes;
} bitmap_t;

typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ entries_t {
    uint8_t* bytes;
    uint8_t* inuse; 
} entries_t;

typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ bucket_t {
    size_t   flag;
    arena_t* arena;       
    uint8_t* unused_addresses;
    uintptr_t offset;
    entries_t entries;
} bucket_t;

/**
   * @description: Free List allocator highly optimized.
   * @note: Buckets will store bytes based on size  
*/
typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ allocator_t {
    void* (*allocate)(size_t);
    void  (*deallocate)(void*);
    arena_t* arena;
    threads_t* pool;  
    bitmap_t map;
    struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ bucket {
        bucket_t* small;
        bucket_t* medium;
        #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
            bucket_t* large;
        #endif
    } bucket;
} allocator_t;

extern allocator_t* allocator;
extern void clear_allocator();
extern void init_allocator_t();


/* =========================================================================
 * Thread pool helpers
 * ========================================================================= 
*/

/** Maximum number of worker threads managed by the allocator. */
#define ALLOC_THREAD_POOL_SIZE  10U

/**
 * ALLOC_FOREACH_THREAD(alloc_ptr, var)
 * Iterates over every non-NULL entry in the thread pool.
 *
 * @p var is a threads_t* declared by the caller; it is set to each live
 * thread pointer in turn.
 *
 * Example:
 *   threads_t *t;
 *   ALLOC_FOREACH_THREAD(allocator, t) { thread_pause(t); }
*/
#define ALLOC_FOREACH_THREAD(alloc_ptr)\
    for (size_t _ti = 0; _ti < ALLOC_THREAD_POOL_SIZE; ++_ti)\
        (alloc_ptr)->pool[_ti] = *(init_threads_t());


#define FIND_AVAILABLE_THREAD(alloc_ptr, out_ptr) \
    for (size_t _i = 0; _i < ALLOC_THREAD_POOL_SIZE; _i++) { \
        if (alloc_ptr->pool[_i].flag == 0x0) {\
            alloc_ptr->pool[_i].flag = 0x01;\
            (out_ptr) = &alloc_ptr->pool[_i];\
        }\
    }

#endif