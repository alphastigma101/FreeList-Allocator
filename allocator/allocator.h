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

#define BUCKET_SMALL_CAP 64U
#define BUCKET_MEDIUM_CAP 128U
#define BUCKET_LARGE_CAP 256U
#define ALLOCATOR_MODE 0x01

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


/**
   * @description: Free List allocator highly optimized.
   * @note: Buckets will store bytes based on size  
*/
typedef struct allocator_t {
    bitmap_t            bitmap;
    struct bucket {
        struct bucket_t*       small;
        struct bucket_t*       medium;
        struct bucket_t*       large;
    } bucket;
    void*               (*allocate)(size_t);
    void                (*deallocate)(void*);
    struct huge_block_allocator_t* huge;
    arena_t*            arena;
    threads_t*          pool;  /* internal allocator threads */
} allocator_t;

extern allocator_t allocator;
extern void init_allocator_t();


/** Maximum number of worker threads managed by the allocator. */
#define ALLOC_THREAD_POOL_SIZE  10U
#define HUGE_THREAD_POOL_START (ALLOC_THREAD_POOL_SIZE / 2)


#endif