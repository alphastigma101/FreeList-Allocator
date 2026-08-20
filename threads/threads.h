#ifndef _THREADS_H
#define _THREADS_H
#include <stddef.h>
#define _GNU_SOURCE 1
#define __USE_UNIX98 1 
#define __USE_XOPEN2K 1
#include <pthread.h>
#include <sys/sem.h>
#include <stdatomic.h>
#include <unistd.h> 
#include <stdint.h>
#include <limits.h>
#include "../logger/logger.h"

/* increase the space for ubsan/asan instrumentations */
#ifndef ASAN_STACK_MULTIPLIER
    #if __SANITIZE_THREAD__ == 1
        #define ASAN_STACK_MULTIPLIER 16
    #else 
        #define ASAN_STACK_MULTIPLIER 1
    #endif
#endif

/* INHERITSCHED — Thread scheduling inheritance 0 PTHREAD_INHERIT_SCHED 1 PTHREAD_EXPLICIT_SCHED */
#ifndef INHERITSCHED
    #define INHERITSCHED -1
#endif

/* MUTEX_ATTR — Mutex type attribute */
#ifndef MUTEX_ATTR
    #define MUTEX_ATTR -1
#endif

/* THREAD_STATE — Choose either to join or detach */
#ifndef THREAD_STATE
    #define THREAD_STATE -1
#endif

/* USTP — User Space Thread Policy */
#ifndef USTP
    #define USTP -1
#endif

#ifndef ENABLE_SHARED_MEMORY 
    #define ENABLE_SHARED_MEMORY 1
#endif

/* BENCHMARK_ENV — 0 for off 1 for on. Default is 0 since we assume there is no benchmarking environment */
#ifndef BENCHMARK_ENV
    #define BENCHMARK_ENV 0
#endif

/* ============================================================
 * FLA_ARCH_WIDE_CACHELINE: detects whether the target architecture
 * family is one that's overwhelmingly known, in real-world hardware,
 * to use 64-byte L1 cache lines (x86-64, aarch64, and the common
 * 64-bit server/desktop RISC families) -- as opposed to embedded,
 * 32-bit, or otherwise unrecognized targets, where cache geometry is
 * far less predictable and a smaller, more memory-conscious default
 * is the safer choice. Renamed from the original MODERN_ARCH: that
 * name didn't actually describe what the macro checks (architecture
 * *family*, not "modern" in any meaningful sense -- a brand new
 * embedded Cortex-M chip is exactly as "modern" as a new x86-64 one),
 * which is very likely how the polarity bug below happened in the
 * first place.
 * ============================================================ */
#ifndef FLA_ARCH_WIDE_CACHELINE
    #if defined(__x86_64__) || defined(_M_X64) || \
        defined(__aarch64__) || defined(_M_ARM64) || \
        defined(__powerpc64__) || defined(__PPC64__) || \
        (defined(__riscv) && __riscv_xlen == 64)
        #define FLA_ARCH_WIDE_CACHELINE 1
    #else
        #define FLA_ARCH_WIDE_CACHELINE 0
    #endif
#endif

/* ============================================================
 * DEFAULT_ALIGNMENT: the default alignment this library uses for
 * cache-line-sensitive data -- per-thread state, lock-free structures,
 * anything shared or contended across threads -- to avoid false
 * sharing (two unrelated, independently-accessed objects landing on
 * the same cache line and generating needless cache-coherency
 * traffic between cores).
 *
 * FIXED A REAL BUG, not just a naming one: the original had this
 * backward. Its #else branch (MODERN_ARCH != 0, i.e. x86-64/aarch64)
 * got the SMALLER alignment (16), while non-x86-64/aarch64 got the
 * LARGER one (64) -- exactly the wrong way around for a threading
 * library. The dead, commented-out #warning on that branch even said
 * "most likely an embedded system", which directly contradicts
 * __x86_64__/__aarch64__ being desktop/server architectures -- a good
 * sign the original branches were simply swapped by mistake.
 *
 * Preference order:
 *   1. C++17's std::hardware_destructive_interference_size, when the
 *      standard library provides it -- purpose-built for exactly this
 *      (sizing to avoid false sharing between concurrently-accessed
 *      objects), so the standard library's own target-specific
 *      knowledge drives the answer instead of a hand-maintained
 *      guess. Confirmed directly: 64 on x86-64, matching the real L1
 *      line size.
 *   2. FLA_ARCH_WIDE_CACHELINE, when (1) isn't available: 64 for the
 *      well-established wide-cache-line family, 16 (a conservative,
 *      still-useful SIMD/max_align_t-class default) otherwise.
 *      __BIGGEST_ALIGNMENT__ is deliberately NOT used here despite
 *      being GNU-provided and directly relevant-sounding: it answers
 *      a different question -- the strictest alignment any
 *      fundamental type on this target needs (16 on x86-64, matching
 *      SSE vector alignment) -- rather than cache geometry (64 on the
 *      same target, confirmed directly). Using it here would silently
 *      under-align exactly the data this constant exists to protect.
 * ============================================================ */
#ifndef DEFAULT_ALIGNMENT
    #if defined(__cplusplus)
        #include <new>
        #ifdef __cpp_lib_hardware_interference_size
            /* GCC specifically warns on using this value (-Winterference-size)
               because it isn't guaranteed stable across compiler versions or
               -mtune/-mcpu flags, in case it ends up baked into a public ABI.
               That's a real, well-founded warning in general -- confirmed
               directly, it re-fires at every USE of the raw expression, not
               just where it's first written, since the diagnostic is tied to
               the token itself wherever it's expanded. Doesn't apply to this
               specific use: DEFAULT_ALIGNMENT is documented above as an
               internal default, not a promised ABI. Following GCC's own
               suggested fix (its note literally says "change it to instead
               use a constant variable you define"): write the raw expression
               exactly once, inside this pragma-protected constexpr variable,
               then have the macro expand to a reference to THAT variable --
               confirmed this way the warning triggers only here, once, and
               every other use site (e.g. inside a later static_assert) is
               warning-free, since it never re-mentions the raw expression. */
            namespace fla_detail {
                #if defined(__GNUC__)
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Winterference-size"
                #endif
                inline constexpr decltype(std::hardware_destructive_interference_size)
                    default_alignment_value = std::hardware_destructive_interference_size;
                #if defined(__GNUC__)
                    #pragma GCC diagnostic pop
                #endif
            }
            #define DEFAULT_ALIGNMENT fla_detail::default_alignment_value
        #endif
    #endif

    #ifndef DEFAULT_ALIGNMENT
        #if FLA_ARCH_WIDE_CACHELINE
            #define DEFAULT_ALIGNMENT 64
        #else
            #define DEFAULT_ALIGNMENT 16
        #endif
    #endif
#endif

/* DEFAULT_ALIGNMENT must be a power of two -- true by construction
   above, but this catches a bad manual override (-DDEFAULT_ALIGNMENT=...)
   at compile time with a clear message instead of a mysterious runtime
   failure in whatever alignas()/aligned_alloc() call uses it. */
#if defined(__cplusplus) && __cplusplus >= 201103L
    static_assert((DEFAULT_ALIGNMENT & (DEFAULT_ALIGNMENT - 1)) == 0,
                  "DEFAULT_ALIGNMENT must be a power of two");
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    _Static_assert((DEFAULT_ALIGNMENT & (DEFAULT_ALIGNMENT - 1)) == 0,
                   "DEFAULT_ALIGNMENT must be a power of two");
#endif

#define FORCE_COMPILER_ALIGNED(n) __attribute__((aligned(n)))
#define FORCE_PACK __attribute__((packed))
#define FORCE_INLINE __attribute__((always_inline)) static inline

#if defined(__GNUC__) && !defined(__clang__)
    #define GCC_OPTIMIZE_O0 __attribute__((optimize("O0")))
#else
    #define GCC_OPTIMIZE_O0
#endif


#define TAG_ADDRESS(p) \
    ((__typeof__(p))((uintptr_t)(p) | (uintptr_t)0x1))

#define UNTAG_ADDRESS(p) \
    ((__typeof__(p))((uintptr_t)(p) & ~((uintptr_t)0x1)))

#define IS_ADDRESS_TAGGED(p) \
    (((uintptr_t)(p) & (uintptr_t)0x1) != 0)


typedef struct attr_t {
    pthread_attr_t                 thread_attr;
    void**                         stackaddr; 
    pthread_mutexattr_t            mutex_attr;
} attr_t;

typedef struct lock_t {
    pthread_mutex_t                mutex;
    unsigned char                  type;
} lock_t;

typedef struct threads_t {
    attr_t*                        attr; 
    lock_t*                        lock;
    pthread_t                      thread_id;
    _Atomic(struct function_t*)    routine;                                
} threads_t;

typedef struct semaphores_t {
    struct sembuf** semaphores;
    struct sembuf*  next;
    int (*semget)(key_t __key, int __nsems, int __semflg);  /* Create a standard or get a standard semaphore*/
    int  (*semop)(int __semid, struct sembuf *__sops, size_t __nsops); /* Define and create a semaphore with special flags */
    size_t bucket_count;
} semaphores_t;


/**
 * @brief Initializes a fresh threads_t instance with default values.
*/
extern threads_t init_threads_t(const unsigned char mode, const unsigned char attr, const unsigned char locked, const unsigned char stack);

/**
 * @brief Allocates and configures a contiguous block of threads forming a pool.
*/
extern void create_thread_pool(threads_t* tp, const size_t size, const unsigned char mode, const unsigned char attr, const unsigned char locked, const unsigned char stack);

/**
    * @brief: Function to allocate a pool of threads at a given spot. If pool is Null, it will be allocated with whatever end is  
*/
extern void create_thread_pool_range(threads_t* tp, const unsigned char mode, const unsigned char attr, const unsigned char locked, const unsigned char stack, const size_t start, const size_t end);

/**
    * @brief: Finds the index of where t is located in tp. Otherwise it will return SIZE_MAX 
*/
extern size_t thread_pool_index(threads_t* tp, threads_t* t);

/**
 * @brief Dynamically resizes or reconfigures an existing thread pool.
*/
extern void update_thread_pool(threads_t *tp, const size_t size);


extern void* threads_t_query(void* param); /* used with conditional variables to sleep the threads */

/**
 * @brief Spawns a single managed thread executing the target function. Requires tp->metadata to be initialized
*/
extern void create_thread(threads_t* tp, void* func);

/**
 * @brief Blocks the caller until the specified thread terminates, capturing its return value.
*/
extern void join_thread(threads_t* tp, void** rtn);

/** 
    * @brief: Function that can make the thread state detachable or not detachable
*/
extern void thread_t_detachable(threads_t* t, const unsigned char mode);

/**
 * @brief Looks up a specific thread instance within a collection.
*/
extern threads_t* find_thread_t(threads_t* tp, const size_t size);

/**
 * @brief Registers or mutates internal runtime configuration and metadata for a thread context.
*/
extern void routine_metadata(threads_t* t, const size_t length, ...);

/**
 * @brief Extracts the raw argument vector packed within a function configuration structure.
*/
extern void** routine_metadata_arguments(struct function_t* meta);

/**
    * @brief: Tags threads_t field data member routine
*/
extern void thread_t_routine_tag(_Atomic(struct function_t*)* meta);

/**
    * @brief: Tags threads_t field data member routine
*/
extern void thread_t_routine_untag(_Atomic(struct function_t*)* meta);

/**
 * @brief Prints state diagnostics and metrics for the specified thread context.
*/
extern void debug_threads(const threads_t tp);


/**
 * @brief Releases heap memory and system resources bound to a thread context.
*/
extern void clean_threads(threads_t* t);


// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off);
extern void* remap_address(void* addr, size_t old_len, size_t new_len);
extern void munmap_address(void* addr, size_t len, const char* file, const int line);

#endif