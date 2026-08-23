#ifndef _ALLOCATOR_H_
#define _ALLOCATOR_H_
#include "../arena/arena.h"

#ifndef BUCKET_SMALL_CAP 
    #define BUCKET_SMALL_CAP 64U
#endif

#ifndef  BUCKET_MEDIUM_CAP
    #define BUCKET_MEDIUM_CAP 128U
#endif

#ifndef BUCKET_LARGE_CAP
    #define BUCKET_LARGE_CAP 256U
#endif

_Static_assert(((BUCKET_SMALL_CAP  & (BUCKET_SMALL_CAP  - 1)) == 0), "BUCKET_SMALL_CAP: It must be a power of 2");
_Static_assert(((BUCKET_MEDIUM_CAP & (BUCKET_MEDIUM_CAP - 1)) == 0), "BUCKET_MEDIUM_CAP: It must be a power of 2");
_Static_assert(((BUCKET_LARGE_CAP  & (BUCKET_LARGE_CAP  - 1)) == 0), "BUCKET_LARGE_CAP: It must be a power of 2");

/* RAM tier heuristic based on arch/vendor macros -- a best-effort
 * classification, NOT a real memory measurement. Three tiers:
 *   ULTRA_CONSTRAINED : classic 8/16-bit microcontrollers, DOS-era 16-bit
 *                       x86 -- typically bytes to a few KB of RAM
 *   EMBEDDED          : 32-bit ARM/x86/RISC-V -- ranges from tens of KB
 *                       (Cortex-M) to hundreds of MB (Cortex-A);
 *                       genuinely ambiguous, treated conservatively
 *   (fallthrough)     : 64-bit x86/ARM/RISC-V -- gaming/desktop or
 *                       capable embedded (e.g. 64-bit Raspberry Pi),
 *                       typically GB-range RAM
*/
#ifndef RAM_TIER_ULTRA_CONSTRAINED
    #if defined(__AVR__) || defined(__MSP430__) || defined(__18CXX) || defined(__XC8) || defined(_M_I86) || defined(__I86__)
        #define RAM_TIER_ULTRA_CONSTRAINED 1
    #else
        #define RAM_TIER_ULTRA_CONSTRAINED 0
    #endif
#endif

#ifndef RAM_TIER_EMBEDDED
    #if !RAM_TIER_ULTRA_CONSTRAINED && \
        ((defined(__arm__)  && !defined(__aarch64__)) || \
         (defined(__i386__) && !defined(__x86_64__))  || \
         (defined(__riscv)  && __riscv_xlen == 32))
        #define RAM_TIER_EMBEDDED 1
    #else
        #define RAM_TIER_EMBEDDED 0
    #endif
#endif

#ifndef BITMAP_SIZE
    #if RAM_TIER_ULTRA_CONSTRAINED
        #define BITMAP_SIZE BUCKET_SMALL_CAP
    #elif RAM_TIER_EMBEDDED
        #define BITMAP_SIZE (BUCKET_SMALL_CAP + BUCKET_MEDIUM_CAP)
    #else
        #define BITMAP_SIZE (BUCKET_SMALL_CAP + BUCKET_MEDIUM_CAP + BUCKET_LARGE_CAP)
    #endif
#endif

/* Set the exact size of the page you need. Will be used in conjunction with MAP_HUGETLB */
#ifndef HUGE_PAGE_SIZE
    #define HUGE_PAGE_SIZE (2UL * 1024 * 1024)
#endif 

/* Set the dynamic array size of huge slots, if allocator.huge is ever to be used */
#ifndef MAX_HUGE_SLOTS
    #define MAX_HUGE_SLOTS 4096U
#endif

_Static_assert(((MAX_HUGE_SLOTS    & (MAX_HUGE_SLOTS    - 1)) == 0), "MAX_HUGE_SLOTS: It must be a power of 2");
_Static_assert(MAX_HUGE_SLOTS > BUCKET_LARGE_CAP,                    "MAX_HUGE_SLOTS: It must be greater than BUCKET_LARGE_CAP");
_Static_assert(((HUGE_PAGE_SIZE    & (HUGE_PAGE_SIZE    - 1)) == 0), "HUGE_PAGE_SIZE: It must be a power of 2");

/**
   * @description: Free List allocator highly optimized.
   * @note: Buckets will store bytes based on size  
*/
typedef struct allocator_t {
    bitmap_t            bitmap;
    struct bucket {
        #if RAM_TIER_ULTRA_CONSTRAINED
            struct bucket_t*       small;
        #else
            struct bucket_t*       small;
            #if RAM_TIER_EMBEDDED
                struct bucket_t*       medium;
            #else 
                struct bucket_t*       medium;
                #if !RAM_TIER_ULTRA_CONSTRAINED && !RAM_TIER_EMBEDDED
                    struct bucket_t*       large;
                #endif
            #endif
        #endif
    } bucket;
    void*               (*allocate)(size_t);
    void                (*deallocate)(void*);
    #if !RAM_TIER_ULTRA_CONSTRAINED && !RAM_TIER_EMBEDDED
        struct huge_block_allocator_t* huge;
    #endif
    arena_t*            arena;
    threads_t*          pool;
} allocator_t;

extern allocator_t allocator;
#if BENCHMARK_ENV == 1
    struct entry_table_t;
    struct byte_entries_t;
    struct offset_entries_t;
    struct blocks_t;
    struct bucket_t;

    typedef struct benchmark_allocator_t {
        /* Entry functions */
        struct byte_entries_t* (*get_entry_t_by_bytes)(struct entry_table_t* table, const size_t idx, const size_t bytes, const unsigned char inuse);
        struct offset_entries_t* (*get_entry_t_by_offset)(struct entry_table_t* table, const size_t idx, const size_t offset, const unsigned char inuse);
        void (*update)(struct entry_table_t* table, const size_t idx, const size_t offset, const size_t bytes, const unsigned char inuse);
        void (*destroy)(struct entry_table_t* table, const size_t idx, const size_t offset, const size_t bytes);
        void (*clean)(struct entry_table_t *table);

        unsigned char (*is_mergeable)(struct entry_table_t* table, const size_t idx, const size_t bytes);
        void (*merge)(struct blocks_t* blocks, struct entry_table_t* table, const size_t idx, const size_t bytes);
        void (*update_block_t_by_offset)(struct blocks_t* blocks, const size_t idx, const size_t offset, const unsigned char inuse);
        struct blocks_t* (*get_block_t_by_offset)(struct blocks_t* blocks, const size_t idx, const size_t offset, const unsigned char inuse);
        void (*block_t_dctor)(struct blocks_t* blocks);

        /* coalescing */
        void* (*coalescing)(const size_t bytes);

        /* bucket functions */
        struct bucket_t* (*find_slot)(void* ptr);
        void (*push_to_bucket)(struct bucket_t* slot, size_t offset);
        void* (*pop_from_bucket)(struct bucket_t* slot, size_t bytes);
        void (*bucket_t_dctor)();
        void (*__rewind)(struct bucket_t* slot);

    } benchmark_allocator_t;
    extern benchmark_allocator_t benchmark_allocator;
#endif

extern void init_allocator_t();


/** Maximum number of worker threads managed by the allocator. */
#ifndef ALLOC_THREAD_POOL_SIZE
    #define ALLOC_THREAD_POOL_SIZE  10U
#endif

_Static_assert(ALLOC_THREAD_POOL_SIZE != 0 && ALLOC_THREAD_POOL_SIZE > 0, "ALLOC_THREAD_POOL_SIZE: It must be greater than 0 and not equal to it");

#endif