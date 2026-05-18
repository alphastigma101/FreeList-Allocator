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

#define BUCKET_SMALL_MAX 64U
#define BUCKET_MEDIUM_MAX 512U

// TODO: This needs to be moved into allocator.c file
typedef struct bitmap_t {
    uint8_t*            bits;
    size_t              n_bytes;
} bitmap_t;

// TODO: This needs to be moved into allocator.c file
typedef struct entries_t {
    uint8_t*            bytes;
    uint8_t*            inuse; 
} entries_t;


// TODO: This needs to be moved into allocator.c file
typedef struct FORCE_COMPILER_ALIGNED(DEFAULT_ALIGNMENT) bucket_t {
    uint8_t             flag;
    uint8_t             _pad[7];
    arena_t*            arena;       
    void*               ua;
    uintptr_t           offset;
    entries_t           entries;
} bucket_t;

/**
   * @description: Free List allocator highly optimized.
   * @note: Buckets will store bytes based on size  
*/
typedef struct FORCE_COMPILER_ALIGNED(DEFAULT_ALIGNMENT) allocator_t {
    void*               (*allocate)(size_t);
    void                (*deallocate)(void*);
    arena_t*            arena;
    threads_t*          pool;  
    bitmap_t            map;
    struct FORCE_COMPILER_ALIGNED(DEFAULT_ALIGNMENT) bucket {
        bucket_t*       small;
        bucket_t*       medium;
        #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
            bucket_t*   large;
        #endif
    } bucket;
} allocator_t;

extern allocator_t* allocator;
extern void clear_allocator(); // TODO: This should be static and not external
extern void init_allocator_t();


/* =========================================================================
 * Arena helpers
 * ========================================================================= 
*/

/* =========================================================================
 * Bitmap helpers
 * ========================================================================= 
*/
#define SET_BITMAP(idx, start, end, res) \
    do { if (idx < start || idx > end) break;\
        allocator->map.bits[idx] = 0x0;\
        res = 1;\
    } while(0)


#define CLEAR_BITMAP(idx, start, end, res) \
    do { if (idx < start || idx > end) break;\
        allocator->map.bits[idx] = 0x01;\
        res = 1;\
    } while(0)

#define FIND_BITMAP_INDEX(slot, start) 0



#define PROBE_BITMAP(start, end) 0

/* =========================================================================
 * Bucket pool helpers
 * ========================================================================= 
*/

#define SYNC_BUCKETS(slot) 0

#define BUCKET_MARK_FREE(slot, start, end, abs_idx) 0

#define FIND_FREE_SLOT(sz) 0

// TODO: Combine both bucket_mark_free bucket_mark_full into one macro. We need to combine them first, test them as a function and then paste it here
#define MARK_IF_FULL(slot, start, end, abs_idx) 0

#define MARK_FULL(slot, start, end, abs_idx) 0

#define POP_FROM_BUCKET(slot, bytes) \
    do { bucket_t *p_slot = &slot; \
        size_t t_bytes = bytes; \
        if (!p_slot->ua || p_slot->ua == p_slot->arena->chunk) break; \
        uintptr_t uint_addr = (uintptr_t)p_slot->ua - (bytes & ~(bytes - 1)); \
        void* address = (void*)(uint_addr); \
        p_slot->ua = (void*)uint_addr; \
        size_t offset = (size_t)((uintptr_t)address - (uintptr_t)p_slot->arena->chunk); \
        p_slot->entries.inuse[offset] = 0x01; \
        p_slot->entries.bytes[offset] = 0; \
        p_slot->arena = push(slot->arena, bytes);\
        SYNC_BUCKETS(slot); \
        address = (address != NULL ? address : NULL); \
    } while(0)

#define PUSH_TO_BUCKET(slot, offset) 0


//#define ALLOC_ALLOCATOR_BUCKETS



/* =========================================================================
 * Thread pool helpers
 * ========================================================================= 
*/

/** Maximum number of worker threads managed by the allocator. */
#define ALLOC_THREAD_POOL_SIZE  10U

#define THREAD_POOL_CTOR_RANGE(next) 0

// Deprecated. Not using it since it generates bloat instructions 5/17/26
#define FIND_AVAILABLE_THREAD(alloc_ptr, out_ptr) \
    for (size_t _i = 0; _i < ALLOC_THREAD_POOL_SIZE; _i++) { \
        if (alloc_ptr->pool[_i].flag == 0x0) {\
            alloc_ptr->pool[_i].flag = 0x01;\
            (out_ptr) = &alloc_ptr->pool[_i];\
        }\
    }

#endif