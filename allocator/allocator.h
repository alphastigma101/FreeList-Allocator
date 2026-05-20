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
#ifndef _ALLOCATOR_H_
#define _ALLOCATOR_H_
#include "../arena/arena.h"
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>

#define SMALL_BIT_START 0
#define SMALL_BIT_END 64

#define MEDIUM_BIT_START 64
#define MEDIUM_BIT_END 192

#define LARGE_BIT_START 192
#define LARGE_BIT_END 448

#define BUCKET_SMALL_CAP 64U
#define BUCKET_MEDIUM_CAP 128U
#define BUCKET_LARGE_CAP 256U

/**
   * @description: Free List allocator highly optimized.
   * @note: Buckets will store bytes based on size  
*/
typedef struct allocator_t {
    void*               (*allocate)(size_t);
    void                (*deallocate)(void*);
    arena_t*            arena;
    threads_t*          pool;  
    uint8_t*            bits;
    size_t              n_bytes;
    struct bucket {
        struct bucket_t*       small;
        struct bucket_t*       medium;
        #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
            struct bucket_t*   large;
        #endif
    } bucket;
} allocator_t;

extern allocator_t allocator;
extern void clear_allocator(); // TODO: This should be static and not external
extern void init_allocator_t();


/** Maximum number of worker threads managed by the allocator. */
#define ALLOC_THREAD_POOL_SIZE  10U


#endif