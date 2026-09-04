#ifndef _THREADS_H
#define _THREADS_H
#define _GNU_SOURCE 1
#define __USE_UNIX98 1 
#define __USE_XOPEN2K 1
#include <pthread.h>
#include <malloc.h>
#include <semaphore.h>
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


/* BENCHMARK_ENV — 0 for off 1 for on. Default is 0 since we assume there is no benchmarking environment */
#ifndef BENCHMARK_ENV
    #define BENCHMARK_ENV 0
#endif

/** Maximum number of worker threads managed by the allocator. */
#ifndef ALLOC_THREAD_POOL_SIZE
    #define ALLOC_THREAD_POOL_SIZE  10U
#endif

/* Maximum amount of semaphores to be used in threads.c */
#ifndef SEMAPHORES_T_SIZE
   #define SEMAPHORES_T_SIZE 1U
#endif

_Static_assert(ALLOC_THREAD_POOL_SIZE != 0 && ALLOC_THREAD_POOL_SIZE > 0, "ALLOC_THREAD_POOL_SIZE: It must be greater than 0 and not equal to it");
_Static_assert(!(SEMAPHORES_T_SIZE <= 0), "SEMAPHORES_T_SIZE must be greater than 1!\n");

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
    #ifdef THREADS_T_ENABLE_STACK
        #if (THREADS_T_ENABLE_STACK == 0X01)
            void**                         stackaddr;
        #endif 
    #endif
    pthread_mutexattr_t            mutex_attr;
    int_least16_t                  state;
    int_least16_t                  status;
} attr_t;

typedef struct lock_t {
    pthread_mutex_t                mutex;
    int                            state;
    int                            status;
} lock_t;

typedef struct lock_free_t {
    /// @brief: A Variable when the state is zero, it means the threads have been sync. 
    /// Anything greater are treated as warnings and anything less than zero are errors  
    atomic_int     status;
    /// @brief: A Variable used to be assign with the pool index.
    atomic_int     index;     
} lock_free_t;

typedef struct cond_t {
    pthread_cond_t                 cond_v;
    atomic_int_least16_t           status;
    atomic_int_least16_t           index;
    pthread_condattr_t             attr;
} cond_t;

#ifndef THREADS_T_MODE
    #if (THREAD_T_MODE != 0) || (THREADS_T_MODE != 1)
        #define THREADS_T_MODE 1
    #else 
        #define THREADS_T_MODE 1
    #endif
#endif

#ifndef THREADS_T_ENABLE_ATTRIBUTE
    #define THREADS_T_ENABLE_ATTRIBUTE 0
#endif 

#ifndef THREADS_T_ENABLE_STACK
    #define THREADS_T_ENABLE_STACK 0
#endif 

#ifndef THREADS_T_ENABLE_LOCK
    #define THREADS_T_ENABLE_LOCK 0
#endif 

#ifndef THREADS_T_ENABLE_CONDITION
    #define THREADS_T_ENABLE_CONDITION 0
#endif 
typedef struct threads_t {
    #if (THREADS_T_ENABLE_ATTRIBUTE == 1)
        attr_t                     attr;
    #endif
    #if (THREADS_T_ENABLE_CONDITION == 1)
        cond_t                     cond;
    #endif
    #if (THREADS_T_ENABLE_LOCK == 1) 
        lock_t                     lock;
    #endif
    lock_free_t                    lock_free;
    pthread_t                      thread_id;
    _Atomic(struct function_t*)    routine;
} threads_t;


/**
 * @brief Initializes a fresh threads_t instance with default values.
 * @note allocator.c calls this by default, since it allocates on the stack. 
*/
extern threads_t init_threads_t(void);

/**
 * @brief Allocates and configures a contiguous block of threads forming a pool.
 * @param tp a thread pool type of 'threads_t'
*/
extern void create_thread_pool(threads_t* tp, const size_t size);

/**
 * @brief: Function to allocate a pool of threads at a given spot. If pool is Null, it will be allocated with whatever end is
 * @param tp a thread pool of type 'threads_t'  
*/
extern void* create_thread_pool_range(threads_t* tp, size_t start, const size_t end);

/** 
 * @brief Function that checks to see if thread is joinable or not.
 *         If thread is not detachable, it is in a joinable state
 * @param t a pointer to type threads_t 
*/
extern unsigned char threads_t_is_detachable(const threads_t* t);

/**
 * @brief Finds the index of where t is located in tp. Otherwise it will return SIZE_MAX
 * @param tp a thread pool or a collection of type 'threads_t' decayed to a pointer
 * @param t a pointer of type threads_t, that is apart of 'tp'
*/
extern size_t thread_t_pool_index(threads_t* tp, threads_t* t);

/**
 * @brief Looks up a specific thread instance within a collection.
 * @param tp a thread pool type of 'threads_t'
 * @param size the size of the thread pool 
*/
extern threads_t* find_thread_t(threads_t* tp, const size_t size);

/**
 * @brief A stand alone function that spawns a single managed thread executing the target function. Requires tp->metadata to be initialized
 * @param t a pointer to type threads_t that will create the process
 * @param func raw memory address of a function that will be used with the process  
*/
extern void create_thread(threads_t* t, void* func);

/**
 * @brief A stand alone function that manages the thread pool by getting externally tagged/untagged by some other functions
 * @param tp a thread pool that will be managed.
 * @param size the size of the thread pool
 * @param func The raw memory address of a function to be threaded
 * @param state 0x01 is to stop/join if possible 'tp[0]', otherwise 0x01 is to be used    
*/
extern int threads_t_query(threads_t* tp, const size_t size, void* func, const unsigned char state);

/**
 * @brief A stand alone function that updates the pointer to 'threads_t' state
 * @param t a pointer to 'threads_t' 
 * @param state Usually a numerical value where the 'value's representation' can be unsigned or signed
 * @note if 'state' >= 0 't' is in a stable state and can be used.
 *       if 'state' <= -1 't' is not in a stable state and will not be used   
*/
extern void threads_t_set_status(threads_t* t, const int state, const int index);

/**
 * @brief A standalone function that blocks the caller until the specified thread terminates, capturing its return value.
 * @param t a pointer type of 'threads_t'
 * @param rtn the expected object to be returned from the function that was used in `create_thread` 
*/
extern void join_thread(threads_t* t, void** rtn);

/**
 * @brief A stand alone function that initializes or reuses a memory address that points to data field member 'function_t' in 'threads_t'
 *        by feeding it 'metadata' 
 * @param t a pointer type to 'threads_t'
 * @param length a numerical value the represents the total parameters of a function  
*/
extern void routine_metadata(threads_t* t, const size_t length, ...);

/**
 * @brief A stand alone function that extracts the arguments for the targeted function
 * @param meta a pointer to 'function_t' that contains metadata to describe a function.
*/
extern void** routine_metadata_arguments(struct function_t* meta);

/**
 * @brief A stand alone function that prints out debugging information to console
 * @param tp a thread pool of type 'threads_t'
*/
extern void debug_threads(const threads_t* tp);

/**
 * @brief A stand alone function that cleans up the allocated memory.
 * @param t a pointer to 'threads_t' 
*/
extern void clean_threads(threads_t* t);

// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off);
extern void* remap_address(void* addr, size_t old_len, size_t new_len);
extern void munmap_address(void* addr, size_t len, const char* file, const int line);

#endif