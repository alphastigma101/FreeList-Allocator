/*
 * pool.c — Internal threading translation unit
 *
 * Copyright (C) 2026 BusyBox Contributors
 *
 * This translation unit is part of BusyBox, the Swiss Army Knife of
 * embedded Linux. It provides internal threading primitives, shared
 * memory management, and POSIX mutex lifecycle utilities intended
 * exclusively for use within the BusyBox build system.
 *
 * Not to be linked against or consumed as a public API. Symbols defined
 * here are internal to BusyBox and subject to change without notice.
 *
 * Licensed under GPLv2. See LICENSE in the root of the source tree.
 * https://busybox.net
*/
#include "threads.h"
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_GNU 1
#include <sys/mman.h>

/*
#include "threads.h"
#include <stdalign.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_GNU 1
#include <sys/mman.h>
*/


//////////////////////////
// SEMAPHORES_T SECTION /
////////////////////////


enum SEMAPHORES_T_STATUS {
    SEMAPHORES_T_INIT,
    SEMAPHORES_T_INUSE,
    SEMAPHORES_T_ERROR
};

typedef struct semaphores_t {
    sem_t                    semaphore;
    enum SEMAPHORES_T_STATUS status;
    int                      state;
} semaphores_t;
static semaphores_t  array_of_sems[SEMAPHORES_T_SIZE] = {0};

// TODO: Should also look into vtable highjacking
[[gnu::constructor(0)]]
FORCE_INLINE void init_semaphores_t(void) {
    #pragma GCC unroll SEMAPHORES_T_SIZE
    for (size_t i = 0; i < SEMAPHORES_T_SIZE; i++) {
        semaphores_t* snode = &array_of_sems[i];
        if (snode->status != SEMAPHORES_T_INIT) {
            int res = sem_init(&snode->semaphore, PTHREAD_PROCESS_SHARED, 0);
            if (res == -1) {
                snode->state = -1;
                snode->status = SEMAPHORES_T_ERROR;
            }
            else {
                snode->status = SEMAPHORES_T_INIT;
                snode->state = 1;
            }
        }
    }
}

FORCE_INLINE void semaphores_t_state_set(semaphores_t* sem, const enum SEMAPHORES_T_STATUS status) {
    #pragma GCC unroll SEMAPHORES_T_SIZE
    for (size_t i = 0; i < SEMAPHORES_T_SIZE; i++) {
        semaphores_t* snode = &array_of_sems[i];
        if (snode == sem) {
            snode->status = status;
            return;
        }
    }
    return;
}

FORCE_INLINE semaphores_t* semaphores_t_get_available_node(void) {
    #pragma GCC unroll SEMAPHORES_T_SIZE
    for (size_t i = 0; i < SEMAPHORES_T_SIZE; i++) {
        if (array_of_sems[i].status != SEMAPHORES_T_INUSE && array_of_sems[i].status != SEMAPHORES_T_ERROR) {
            semaphores_t_state_set(&array_of_sems[i], SEMAPHORES_T_INUSE);
            return &array_of_sems[i];
        }
    }
    return nullptr;
}

[[gnu::destructor]]
FORCE_INLINE void dctor_semaphores_t(void) {
    #pragma GCC unroll SEMAPHORES_T_SIZE
    for (size_t i = 0; i < SEMAPHORES_T_SIZE; i++) {
        const enum SEMAPHORES_T_STATUS status = array_of_sems[i].status;
        if (status != SEMAPHORES_T_ERROR) sem_destroy(&array_of_sems[i].semaphore);
    }
    memset(&array_of_sems, 0, sizeof(semaphores_t) * SEMAPHORES_T_SIZE);
}


/////////////////////////
// FUNCTION_T SECTION //
///////////////////////


typedef struct function_t {
    void*  func;
    void** args;
    size_t size;
} function_t;



[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void** routine_metadata_arguments(struct function_t* meta) { return meta->args; }

[[gnu::hot]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
void routine_metadata(threads_t* t, const size_t length, ...) {
    function_t* routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
    if (!routine) {
        routine = aligned_alloc(alignof(function_t), sizeof(function_t));
        if (!routine) return;
        memset(routine, 0, sizeof(function_t));
    }

    if (routine->size != length) {
        const size_t threshold = M_MMAP_THRESHOLD;
        if (routine->args) {
            if (routine->size < threshold && length > threshold) {
                free(routine->args);
                routine->args = NULL;
                routine->args = private_address(NULL, length * sizeof(void*), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
                if (routine->args == MAP_FAILED) return;
                else {
                    int res = madvise(routine->args, length * sizeof(void*), MADV_SEQUENTIAL);
                    if (res == -1) {
                        if (routine->args) munmap_address(routine->args, length * sizeof(void*), __FILE__,  __LINE__);
                        routine->args = NULL;
                        return;
                    }
                    res = madvise(routine->args, length * sizeof(void*), MADV_MERGEABLE);
                    if (res == -1) {
                        if (routine->args) munmap_address(routine->args, length * sizeof(void*), __FILE__,  __LINE__);
                        routine->args = NULL;
                        return;
                    }
                }
            }
            else if (routine->size > threshold && length < threshold) {
                munmap_address(routine->args, length * sizeof(void*), __FILE__,  __LINE__);
                routine->args = NULL;
                routine->args = malloc(length * sizeof(void*));
            }
            else routine->args = length < threshold ? realloc(routine->args, length * sizeof(void*)) : remap_address(routine->args, routine->size, length);
        }
        else routine->args = length < threshold ? malloc(length * sizeof(void*)) : private_address(NULL, length * sizeof(void*), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
        if (!routine->args) return;
        routine->size = length;
    }
    memset(routine->args, 0, (length) * sizeof(void*));

    va_list args;
    va_start(args, length);
    for (size_t i = 0; i < length; i++) routine->args[i] = va_arg(args, void*);
    va_end(args);

    atomic_store_explicit(&t->routine, routine, memory_order_relaxed);
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE unsigned char thread_t_routine_tagged(_Atomic(struct function_t*)* meta) {
    function_t* cur = atomic_load_explicit(meta, memory_order_relaxed);
    if (IS_ADDRESS_TAGGED(cur)) return 0x01;
    return 0x0;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE void thread_t_routine_untag(_Atomic(struct function_t*)* meta) {
    if (thread_t_routine_tagged(meta)) {
        function_t* cur = atomic_load_explicit(meta, memory_order_relaxed);
        cur = UNTAG_ADDRESS(cur);
        atomic_store_explicit(meta, cur, memory_order_relaxed);
    }
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE void thread_t_routine_tag(_Atomic(struct function_t*)* meta) {
    if (!thread_t_routine_tagged(meta)) {
        function_t* untagged = atomic_load_explicit(meta, memory_order_relaxed);
        function_t* tagged = TAG_ADDRESS(untagged);
        atomic_store_explicit(meta, tagged, memory_order_relaxed);
    }
}

/**
 * @brief Creates a shared memory mapping accessible across processes
 * @param addr Suggested address (usually NULL for kernel to choose)
 * @param len Length of mapping in bytes
 * @param prot Memory protection (PROT_READ, PROT_WRITE, etc.)
 * @param flags Mapping flags (will be OR'd with MAP_SHARED)
 * @param fildes File descriptor (or -1 for anonymous mapping)
 * @param off Offset in the file/object (in pages, multiply by page size)
 * @note: Pass in INT_MAX if no other flags are needed
 * @return Pointer to mapped region, or MAP_FAILED on error
*/
[[gnu::hot]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off) {
    off_t offset = (off_t)off * sysconf(_SC_PAGE_SIZE);
    
    void* result = NULL;
    if (flags != INT_MAX) result = mmap(addr, len, prot, MAP_SHARED | MAP_ANONYMOUS | flags, fildes, offset);
    else result = mmap(addr, len, prot, MAP_SHARED | MAP_ANONYMOUS, fildes, offset);
    
    if (result == MAP_FAILED) {
        fprintf(stderr, "shared_address: mmap failed: %s\n", strerror(errno));
        return MAP_FAILED;
    }
    
    return result;
}

/**
 * @brief Creates a private anonymous memory mapping (not shared with other processes)
 * @param addr Suggested address (usually NULL for kernel to choose)
 * @param len Length of mapping in bytes
 * @param prot Memory protection (PROT_READ, PROT_WRITE, etc.)
 * @param flags Additional mapping flags (will be OR'd with MAP_PRIVATE | MAP_ANONYMOUS)
 * @param fildes File descriptor (ignored for anonymous mappings, pass -1)
 * @param off Offset (ignored for anonymous mappings, pass 0)
 * @note: Pass in INT_MAX if no other flags are needed
 * @return Pointer to mapped region, or MAP_FAILED on error
*/
[[gnu::hot]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void* private_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off) {
    
    void* result = NULL;
    if (flags != INT_MAX) result = mmap(addr, len, prot, MAP_PRIVATE | MAP_ANONYMOUS | flags, fildes, off);
    else result = mmap(addr, len, prot, MAP_PRIVATE | MAP_ANONYMOUS, fildes, off);
    
    if (result == MAP_FAILED) {
        fprintf(stderr, "private_address: mmap failed: %s\n", strerror(errno));
        return MAP_FAILED;
    }
    
    return result;
}

[[gnu::hot]]
[[gnu::nonnull(1)]] /* Compiler might perform optimizations. Disable it using fno-delete-null-pointer-checks */
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void* remap_address(void* addr, size_t old_len, size_t new_len) {
    if (old_len == new_len) return NULL;
    void* res = mremap(addr, old_len, new_len, MREMAP_MAYMOVE);
    if (res == MAP_FAILED) return NULL;
    return res;
}

/**
 * @brief Unmaps a memory region created by shared_address or private_address
 * @param addr Pointer returned by shared_address/private_address
 * @note You must track the length separately or store it in the mapped region
*/
[[gnu::hot]]
[[gnu::nonnull(1, 3)]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void munmap_address(void* addr, size_t len, const char* file, const int line) {
    if (addr == MAP_FAILED) {
        fprintf(stderr, "clean_address: [ invalid address ] file [ %s ] line [ %d ]\n", file, line);
        return;
    }
    
    if (munmap(addr, len) != 0) {
        fprintf(stderr, "clean_address: munmap failed: [ %s ] file: [ %s ] line: [ %d ]", strerror(errno), file, line);
    }
}

FORCE_INLINE void init_threads_t_stack(threads_t* tp, const size_t idx);
FORCE_INLINE threads_t init_locks_t(threads_t* t, const size_t idx);
FORCE_INLINE threads_t init_attr_t(threads_t* t, const size_t idx);
FORCE_INLINE threads_t init_cond_t(threads_t* t, const size_t idx);
FORCE_INLINE threads_t* find_thread_t_and_meta(threads_t* tp, const size_t size, void* func);
FORCE_INLINE bool threads_t_check_status(threads_t* t, const enum THREADS_T_ENABLED_STATES enabled_state);
static size_t __ss = {0}; /* Abbreviated as stack size and is used in create_attrs and clean_threads */
static int amount_of_joined_threads = 0;

////////////////////////
// THREADING SECTION //
//////////////////////


[[gnu::cold]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
threads_t init_threads_t(void) {
    threads_t t = {0};
    #if THREADS_T_ENABLE_ATTRIBUTE == 1
        t = init_attr_t(&t, SIZE_MAX);
        #if THREADS_T_ENABLE_LOCK == 1
            t = init_locks_t(&t, SIZE_MAX);
            threads_t_set_status(&t, 0, SIZE_MAX, THREADS_T_ENABLE_LOCK_T);
        #else 
            threads_t_set_status(&t, 0, SIZE_MAX, THREADS_T_ENABLED_NONE);
        #endif
        #if THREADS_T_ENABLE_CONDITION == 1
            t = init_cond_t(&t, SIZE_MAX);
            threads_t_set_status(&t, 0, SIZE_MAX, THREADS_T_ENABLE_COND_T);
        #endif
        #if THREADS_T_ENABLE_STACK == 1
            init_threads_t_stack(&t, SIZE_MAX);
        #endif
    #endif
    return t;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE threads_t init_locks_t(threads_t* t, const size_t idx) {
    threads_t* thread = idx == SIZE_MAX ? t : &t[idx];
    #if THREADS_T_ENABLE_LOCK == 1
        int rc;
        #if (THREADS_T_MODE == 1) && (THREADS_T_ENABLE_MUTEX_ATTR == 1)
            __builtin_prefetch(thread, 0, 1);
            rc = pthread_mutexattr_init(&thread->attr.mutex_attr);
            if (rc) threads_t_set_status(thread, -2, idx, THREADS_T_ENABLE_LOCK_T);
            const bool state = threads_t_check_status(thread, THREADS_T_ENABLE_LOCK_T);
            if (state) {
                rc = pthread_mutexattr_setpshared(&thread->attr.mutex_attr, PTHREAD_PROCESS_SHARED); 
                if (rc) {
                    fprintf(stderr, "pthread_mutexattr_setpshared failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
                    pthread_mutexattr_destroy(&thread->attr.mutex_attr);
                }
                rc = pthread_mutexattr_settype(&thread->attr.mutex_attr, CHOOSE_THREADS_T_MUTEX_LOCK_TYPE);
                if (rc) {
                    fprintf(stderr, "pthread_mutexattr_settype failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
                    pthread_mutexattr_destroy(&thread->attr.mutex_attr); 
                }
            }
        #endif
        #if THREADS_T_ENABLE_MUTEX_ATTR == 1
           rc = pthread_mutex_init(&thread->lock.mutex, &thread->attr.mutex_attr);
        #else 
            rc = pthread_mutex_init(&thread->lock.mutex, nullptr);
        #endif
        if (rc) {
            fprintf(stderr, "pthread_mutex_init failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            rc = pthread_mutex_init(&thread->lock.mutex, nullptr);
            if (rc) {
                fprintf(stderr, "pthread_mutex_init failed again: %s (errno: %d)\n\t trying other locks...\n", strerror(rc), rc);
            }
        }
    #endif 
    return *thread;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE threads_t init_attr_t(threads_t* t, const size_t idx) {
    threads_t* thread = idx == SIZE_MAX ? t : &t[idx];
    #if THREADS_T_ENABLE_ATTRIBUTE == 1
        struct sched_param schedparam;
        int rc = pthread_attr_init(&thread->attr.thread_attr);
        if (rc) threads_t_set_status(thread, -2, idx, THREADS_T_ENABLE_ATTR_T);
        const bool state = threads_t_check_status(thread, THREADS_T_ENABLE_ATTR_T);
        if (state) {
            rc = pthread_attr_setinheritsched(&thread->attr.thread_attr, CHOOSE_THREADS_T_INHERITSCHED);
            if (rc) {
                fprintf(stderr,"pthread_attr_setinheritsched failed: %s (errno: %d)\n", strerror(rc), rc);
            }
            rc = pthread_attr_setschedpolicy(&thread->attr.thread_attr, CHOOSE_THREADS_T_POLICY);
            if (rc) {
                fprintf(stderr, "pthread_attr_setschedpolicy failed: %s (errno: %d)\n", strerror(rc), rc);
            }
            schedparam.sched_priority = CHOOSE_THREADS_T_POLICY;
            rc = pthread_attr_setschedparam(&thread->attr.thread_attr, &schedparam);
            if (rc) {
                fprintf(stderr, "pthread_attr_setschedparam failed: %s (errno: %d)\n", strerror(rc), rc);
            }
            rc = pthread_attr_setdetachstate(&thread->attr.thread_attr, CHOOSE_THREADS_T_STATE);
            if (rc) {
                fprintf(stderr, "pthread_attr_setdetachstate failed: %s (errno: %d)\n", strerror(rc), rc);
            }
        }
    #endif
    return *thread;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE void init_threads_t_stack(threads_t* tp, const size_t idx) {
    threads_t* thread = idx == SIZE_MAX ? tp : &tp[idx];
    if (!thread) return;
    #if (THREADS_T_ENABLE_ATTRIBUTE == 1) && (THREADS_T_ENABLE_STACK == 1)
        int rc = 0;
        const size_t threshold = M_MMAP_THRESHOLD;
        size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
        size_t base_size = (size_t)PTHREAD_STACK_MIN * ASAN_STACK_MULTIPLIER;
        __ss             = (base_size + page_size - 1) & ~(page_size - 1);
        size_t total_with_guard = (size_t)__ss + page_size;
        if (!thread->attr->stackaddr) thread->attr->stackaddr = __ss > threshold ? private_address(nullptr, total_with_guard, PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0) : aligned_alloc(page_size, total_with_guard);
        void* stack = thread->attr->stackaddr;
        if (stack) {
            __builtin_prefetch(thread->attr, 0, 1);
            void* usable = (char*)thread->attr->stackaddr + page_size;
            rc = pthread_attr_setstack(&thread->attr.thread_attr, usable, __ss);
            if (rc) {
                fprintf(stderr, "pthread_attr_setstack failed: %s (errno: %d)\n\t swapping to pthread attribute default settings\n", strerror(rc), rc);
                pthread_attr_destroy(&thread->attr.thread_attr);
                return;
            }
            else {
                rc = mprotect(thread->attr->stackaddr, page_size, PROT_NONE);
                if (rc == -1) {
                    fprintf(stderr, "mprotect failed: %s (errno: %d)\n\t failed to guard the stack\n", strerror(errno), errno);
                }
            }
        }
    #endif
}

FORCE_INLINE threads_t init_cond_t(threads_t* t, const size_t idx) {
    threads_t* thread = idx == SIZE_MAX ? t : &t[idx];
    #if (THREADS_T_ENABLE_CONDITION == 1) && (THREADS_T_ENABLE_LOCK == 1)
        int rc = pthread_condattr_init(&thread->attr.cond_attr);
        if (rc == -1) {
            fprintf(stderr, "Failed to init cond_v! Error code: %s", strerror(rc));
            return *thread;
        }
        rc = pthread_cond_init(&thread->cond.cond_v, &thread->attr.cond_attr);
        if (rc == -1) {
            fprintf(stderr, "Failed to init cond_v! Error code: %s", strerror(rc));
            pthread_cond_destroy(&thread->cond.cond_v);
            return *thread;
        }
        int pshared = PTHREAD_PROCESS_SHARED;
        rc = pthread_mutexattr_getpshared(&thread->attr.mutex_attr, &pshared);
        if (rc == -1) return *thread;
        else {
            rc = pthread_condattr_setpshared(&thread->attr.cond_attr, pshared);
            if (rc == -1) return *thread;
        }
    #endif
    return *thread;
}

[[gnu::hot]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
void create_thread_pool(threads_t* tp, const size_t size) {
    #if THREADS_T_MODE == 0
        if (!tp) {
            tp = aligned_alloc(alignof(threads_t), size * sizeof(threads_t));
            if (!tp) return tp;
        }
        memset(tp, 0, end * sizeof(threads_t));
    #elif THREADS_T_MODE == 1
        if (!tp) {
            tp = shared_address(nullptr, size * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
            if (tp == MAP_FAILED) return;
            else {
                int res = madvise(tp, size * sizeof(threads_t), MADV_SEQUENTIAL);
                if (res == -1) {
                    if (tp) munmap_address(tp, size * sizeof(threads_t), __FILE__,  __LINE__);
                    tp = nullptr;
                    return;
                }
                res = madvise(tp, size * sizeof(threads_t), MADV_MERGEABLE);
                if (res == -1) {
                    if (tp) munmap_address(tp, size * sizeof(threads_t), __FILE__,  __LINE__);
                    tp = nullptr;
                    return;
                }
                memset(tp, 0, size * sizeof(threads_t));
            }
        }
    #endif
    for (size_t i = 0; i < size; i++) {
        #if THREADS_T_ENABLE_ATTRIBUTE == 1
            tp[i] = init_attr_t(tp, i);
            #if THREADS_T_ENABLE_LOCK == 1
                tp[i] = init_locks_t(tp, i);
                threads_t_set_status(&tp[i], 0, i, THREADS_T_ENABLED_LOCK_T);
            #else 
                threads_t_set_status(&tp[i], 0, i, THREADS_T_ENABLED_NONE);
            #endif
            #if THREADS_T_ENABLE_CONDITION == 1
                tp[i] = init_cond_t(tp, i);
                threads_t_set_status(&tp[i], 0, i, THREADS_T_ENABLED_COND_T);
            #endif
            #if THREADS_T_ENABLE_STACK == 1
                init_threads_t_stack(tp, i);
            #endif
        #endif 
    }
    return;
}

[[gnu::hot]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
void* create_thread_pool_range(threads_t* tp, const size_t start, const size_t end) {
    #if THREADS_T_MODE == 0
        if (!tp) {
            tp = aligned_alloc(alignof(threads_t), end * sizeof(threads_t));
            if (!tp) return tp;
        }
        memset(tp, 0, end * sizeof(threads_t));
    #elif THREADS_T_MODE == 1
        if (!tp) {
            tp = shared_address(nullptr, end * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
            if (tp == MAP_FAILED) return tp;
            else {
                int res = madvise(tp, end * sizeof(threads_t), MADV_SEQUENTIAL);
                if (res == -1) {
                    if (tp) munmap_address(tp, end * sizeof(threads_t), __FILE__,  __LINE__);
                    tp = nullptr;
                    return tp;
                }
                res = madvise(tp, end * sizeof(threads_t), MADV_MERGEABLE);
                if (res == -1) {
                    if (tp) munmap_address(tp, end * sizeof(threads_t), __FILE__,  __LINE__);
                    tp = nullptr;
                    return nullptr;
                }
                memset(tp, 0, end * sizeof(threads_t));
            }
        }
    #endif
    for (size_t i = start; i < end; i++) {
        #if THREADS_T_ENABLE_ATTRIBUTE == 1
            tp[i] = init_attr_t(tp, i);
            #if THREADS_T_ENABLE_LOCK == 1
                tp[i] = init_locks_t(tp, i);
                threads_t_set_status(&tp[i], 0, i, THREADS_T_ENABLED_LOCK_T);
            #else 
                threads_t_set_status(&tp[i], 0, i, THREADS_T_ENABLED_NONE);
            #endif
            #if THREADS_T_ENABLE_CONDITION == 1
                tp[i] = init_cond_t(tp, i);
                threads_t_set_status(&tp[i], 0, i, THREADS_T_ENABLED_COND_T);
            #endif
            #if THREADS_T_ENABLE_STACK == 1
                init_threads_t_stack(tp, i);
            #endif
        #endif
    }
    return tp;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void create_thread(threads_t* tp, void* func) {
    pthread_t self = pthread_self();
    if (!pthread_equal(tp->thread_id, self)) {
        int rc = pthread_create(&tp->thread_id, &tp->attr.thread_attr, func, atomic_load_explicit(&tp->routine, memory_order_relaxed));
        if (rc) {
            fprintf(stderr, "pthread_create failed: %s (errno: %d)\n", strerror(rc), rc);
            return;
        }
        thread_t_routine_tag(&tp->routine);
    }
}

[[gnu::nonnull(1)]]
void threads_t_set_status(threads_t* t, const int state, const int index, const enum THREADS_T_ENABLED_STATES enabled_state) {
    if (enabled_state != THREADS_T_ENABLED_NONE) {
        if (enabled_state == THREADS_T_ENABLE_COND_T) {
            #if (THREADS_T_ENABLE_CONDITION == 1) && (THREADS_T_ENABLE_LOCK == 1) 
                atomic_exchange_explicit(&t.cond.status, state, memory_order_relaxed);
                atomic_exchange_explicit(&t.cond.index, index, memory_order_relaxed);
            #else  
                return;
            #endif
        }
        else if (enabled_state == THREADS_T_ENABLE_LOCK_T) {
            #if (THREADS_T_ENABLE_LOCK == 1) 
                atomic_exchange_explicit(&t.lock.status, state, memory_order_relaxed);
                atomic_exchange_explicit(&t.lock.index, index, memory_order_relaxed);
            #else  
                return;
            #endif
        }
        else if (enabled_state == THREADS_T_ENABLE_ATTR_T) {
            #if (THREADS_T_ENABLE_ATTRIBUTE == 1) 
                atomic_exchange_explicit(&t->attr.status, state, memory_order_relaxed);
                atomic_exchange_explicit(&t->attr.index, index, memory_order_relaxed);
            #else  
                return;
            #endif
        }
    }
    else {
       atomic_exchange_explicit(&t->lock_free.status, state, memory_order_relaxed);
       atomic_exchange_explicit(&t->lock_free.index, index, memory_order_relaxed); 
    }
}

[[gnu::nonnull(1)]]
/**
 * @brief Free function that returns the thread's state
 * @param t threads_t pointer type
 * @return true if t is in a valid state, otherwise false.
*/
FORCE_INLINE bool threads_t_check_status(threads_t* t, const enum THREADS_T_ENABLED_STATES enabled_state) {
    int state = INT_MAX;
    switch(enabled_state) {
        case THREADS_T_ENABLE_ATTR_T:
            state = atomic_load_explicit(&t->attr.status, memory_order_relaxed);
            if (state >= 0 && state <= 2) return true;
            return false;
        case THREADS_T_ENABLE_LOCK_T:
            state = atomic_load_explicit(&t->lock.status, memory_order_relaxed);
            if (state >= 0 && state <= 2) return true;
            return false;
        case THREADS_T_ENABLE_COND_T:
            state = atomic_load_explicit(&t->cond.status, memory_order_relaxed);
            if (state >= 0 && state <= 2) return true;
            return false;
        default:
            state = atomic_load_explicit(&t->lock_free.status, memory_order_relaxed);
            if (state >= 0 && state <= 2) return true;
            return false;
    }
    return false;
}

/**
 * @description: Free function that returns the thread's current status
 * @param t: threads_t pointer type
 * @note: Status are:
 *              -1 == failed to wait
 *              -2 == failed to lock mutex
 *
 *              0 == initialized
 *              1 == awake
 *              2 == sleep
 */
FORCE_INLINE int threads_t_current_status(threads_t* t) {
    int status = INT_MAX;
    #if (THREADS_T_ENABLE_CONDITION == 1) && (THREADS_T_ENABLE_LOCK == 1)
        status = atomic_load_explicit(&t.cond.status, memory_order_relaxed);
    #else
        status = atomic_load_explicit(&t->lock_free.status, memory_order_relaxed);
    #endif
    return status != INT_MAX ? status : -1;
} 

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
int threads_t_query(threads_t* tp, const size_t size, void* func, const unsigned char state) {
    if (size == 0 || size == SIZE_MAX) return -1;
    
    _Atomic(semaphores_t*) atomic_sem;
    atomic_store_explicit(&atomic_sem, &array_of_sems[0], memory_order_relaxed);
    semaphores_t* sem = atomic_load_explicit(&atomic_sem, memory_order_relaxed);
    if (sem->status != SEMAPHORES_T_INUSE) {
        semaphores_t_state_set(sem, SEMAPHORES_T_INUSE);
        atomic_exchange_explicit(&atomic_sem, sem, memory_order_relaxed);
    }
    
    pthread_t self = pthread_self();
    if (!pthread_equal(self, tp[0].thread_id)) {
        threads_t* t1 = find_thread_t_and_meta(tp, size, func);
        if (!t1) return -1;
        else {
            const int status = threads_t_current_status(t1);
            const bool state = threads_t_check_status(t1);
            if ((state == true) && (status == 0)) create_thread(t1, func);
            else if (status == 1) thread_t_routine_tag(&t1->routine);
            if ((state == true) && (status == 2 || status == 0)) {
                threads_t_set_status(t1, 1, (int)thread_t_pool_index(tp, t1));
                #if (THREADS_T_ENABLE_CONDITION == 0X01) && (THREADS_T_ENABLE_LOCK == 0x01)
                    const size_t index = thread_t_pool_index(tp, t1);
                    int rc = pthread_mutex_lock(&t1->lock->mutex);
                    if (rc == -1) { atomic_store_explicit(&t1.cond.status, -1, memory_order_relaxed); atomic_store_explicit(&t1.cond.index, index, memory_order_relaxed);  }
                    else { atomic_store_explicit(&t1.cond.status, 0, memory_order_relaxed); atomic_store_explicit(&t1->cond->index, index, memory_order_relaxed); }
                    rc = pthread_cond_wait(&t1.cond.cond_v, &t1.lock.mutex); // TODO: Each threaded function needs to call: pthread_cond_signal(pthread_cond_t *cond); 
                    if (rc == -1) { atomic_store_explicit(&t1.cond->status, -2, memory_order_relaxed); atomic_store_explicit(&t1->cond->index, index, memory_order_relaxed); return -1; }
                    return 1;
                #else 
                    const size_t index = thread_t_pool_index(tp, t1);
                    int rc = sem_post(&sem->semaphore);
                    if (rc == -1) { atomic_store_explicit(&t1->lock_free.status, -1, memory_order_relaxed); atomic_store_explicit(&t1->lock_free.index, index, memory_order_relaxed); }
                    else { atomic_store_explicit(&t1->lock_free.status, 0, memory_order_relaxed); atomic_store_explicit(&t1->lock_free.index, index, memory_order_relaxed); return -1; }
                #endif
                return 1;
            }
            return -1;
        } 
        if (state == 0x0) {
            if (!threads_t_is_detachable(&tp[0])) join_thread(&tp[0], NULL);
        }
        return -1;
    }
    else {
        while (state != 0x0) {
            for (size_t i = 1; i < size; i++) {
                const int status = threads_t_current_status(&tp[i]);
                const bool state = threads_t_check_status(&tp[i]);
                if ((state == true) && (status == 1)) {
                    thread_t_routine_untag(&tp[i].routine);
                    threads_t_set_status(&tp[i], 2, (int)i);
                    #if (THREADS_T_ENABLE_CONDITION == 0X01) && (THREADS_T_ENABLE_LOCK == 0x01)
                        int rc = pthread_cond_wait(&tp[i].cond.cond_v, &tp[i].lock.mutex); // TODO: Each threaded function needs to call: pthread_cond_signal(pthread_cond_t *cond); 
                        if (rc == -1) { atomic_store_explicit(&tp[i].cond.status, -2, memory_order_relaxed); atomic_store_explicit(&tp[i].cond.index, i, memory_order_relaxed); }
                        else {
                            rc = pthread_mutex_unlock(&tp[i].lock->mutex);
                            if (rc == -1) { atomic_store_explicit(&tp[i].cond.status, -1, memory_order_relaxed); atomic_store_explicit(&tp[i].cond.index, i, memory_order_relaxed);  }
                            else { atomic_store_explicit(&tp[i].cond.status, 0, memory_order_relaxed); atomic_store_explicit(&tp[i].cond.index, i, memory_order_relaxed); }
                        }
                    #else 
                        int rc = sem_wait(&sem->semaphore);
                        if (rc == -1) { atomic_store_explicit(&tp[i].lock_free.status, -1, memory_order_relaxed); atomic_store_explicit(&tp[i].lock_free.index, i, memory_order_relaxed); }
                        else { atomic_store_explicit(&tp[i].lock_free.status, 0, memory_order_relaxed); atomic_store_explicit(&tp[i].lock_free.index, i, memory_order_relaxed); }
                    #endif
                }           
            }
            int rc = sem_wait(&sem->semaphore);
            #if (THREADS_T_ENABLE_CONDITION == 0X01) && (THREADS_T_ENABLE_LOCK == 0x01)
                rc = pthread_cond_wait(&tp[i].cond.cond_v, &tp[i].lock.mutex); // TODO: Each threaded function needs to call: pthread_cond_signal(pthread_cond_t *cond); 
                if (rc == -1) { atomic_store_explicit(&tp[i].cond.status, -2, memory_order_relaxed); atomic_store_explicit(&tp[i].cond.index, i, memory_order_relaxed); }
                else {
                    rc = pthread_mutex_unlock(&tp[i].lock.mutex);
                    if (rc == -1) { atomic_store_explicit(&tp[i].cond.status, -1, memory_order_relaxed); atomic_store_explicit(&tp[i].cond.index, i, memory_order_relaxed);  }
                    else { atomic_store_explicit(&tp[i].cond.status, 0, memory_order_relaxed); atomic_store_explicit(&tp[i].cond.index, i, memory_order_relaxed); }
                }
            #else 
                if (rc == -1) { atomic_store_explicit(&tp[0].lock_free.index, 0, memory_order_relaxed); atomic_store_explicit(&tp[0].lock_free.status, -1, memory_order_relaxed); }
                else { atomic_store_explicit(&tp[0].lock_free.index, 0, memory_order_relaxed); atomic_store_explicit(&tp[0].lock_free.status, 0, memory_order_relaxed); }
            #endif
        }
    }
    return 0;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline void join_thread(threads_t* t, void** rtn) {
    pthread_t self = pthread_self();
    if (!pthread_equal(t->thread_id, self)) {
        if (!threads_t_is_detachable(t)) pthread_join(t->thread_id, rtn);
        else pthread_join(t->thread_id, rtn); // TODO: This needs to be fixed
        function_t* routine = NULL;
        if (thread_t_routine_tagged(&t->routine)) {
            thread_t_routine_untag(&t->routine);
            routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
        } else routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
        if (routine && routine->args) for (size_t i = 0; i < routine->size; i++) routine->args[i] = NULL;
        atomic_store_explicit(&t->routine, routine, memory_order_relaxed);
    }
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline threads_t* find_thread_t(threads_t* tp, const size_t size) {
    for (size_t i = 0; i < size; i++) { if (!thread_t_routine_tagged(&tp[i].routine)) return &tp[i]; }
    return NULL;
}

[[gnu::nonnull(1, 3)]]
[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
FORCE_INLINE threads_t* find_thread_t_and_meta(threads_t* tp, const size_t size, void* func) {
    for (size_t i = 0; i < size; i++) { 
        if (!thread_t_routine_tagged(&tp[i].routine)) {
            function_t* meta = atomic_load_explicit(&tp->routine, memory_order_relaxed);
            if (meta->func == NULL) {
                meta->func = func;
                atomic_store_explicit(&tp[i].routine, meta, memory_order_relaxed);
                return &tp[i];
            }
            else {
                const int status = threads_t_current_status(&tp[i]);
                if ((status == 2) && (func == meta->func)) return &tp[i];
                else continue; 
            }
        } 
    }
    return NULL;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
inline size_t thread_t_pool_index(threads_t* tp, threads_t* t) {
    if (!tp || !t) return SIZE_MAX;
    return (size_t)(t - tp);
}

[[gnu::nonnull(1)]]
unsigned char threads_t_is_detachable(const threads_t* t) {
    #if (THREADS_T_ENABLE_ATTRIBUTE == 1)
        int detached = PTHREAD_CREATE_DETACHED;
        int rc = pthread_attr_getdetachstate(&t->attr.thread_attr, &detached);
        if (rc == -1) return 0x0; 
        return 0x01; // return 1 bit iif thread is detachable 
    #endif
    return 0x0;
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
void clean_threads(threads_t* t) {
    const size_t threshold = M_MMAP_THRESHOLD;
    void* old = nullptr;
    if (thread_t_routine_tagged(&t->routine)) {
        if (amount_of_joined_threads > 1) {
            printf("Warning: Address of thread_t: [ %p ] has not been untagged!\n ", (void*)t);
            thread_t_routine_untag(&t->routine);
            if (!threads_t_is_detachable(t)) join_thread(t, NULL);
        } else threads_t_query(t, 1, NULL, 0x0);
        amount_of_joined_threads++;
    }
    #if THREADS_T_ENABLE_ATTRIBUTE == 1
        #if THREADS_T_ENABLE_STACK == 1
            if (t->attr->stackaddr) {
                size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
                size_t total_with_guard = (size_t)__ss + page_size;
                if (__ss > 0) {
                    mprotect(t->attr->stackaddr, total_with_guard, PROT_READ | PROT_WRITE);
                    old = t->attr->stackaddr;
                    memset(t->attr->stackaddr, 0, total_with_guard);
                }
                __ss > threshold ? munmap_address(old, total_with_guard, __FILE__,  __LINE__) : free(old);
            }
        #endif
        old = &t->attr;
        pthread_attr_destroy(&t->attr.thread_attr);
        pthread_mutexattr_destroy(&t->attr.mutex_attr);
        memset(&t->attr, 0, sizeof(attr_t));
        free(old);
    #endif
    #if THREADS_T_ENABLE_LOCK == 1
        old = &t->lock;
        pthread_mutex_destroy(&t->lock.mutex);
        memset(&t->lock, 0, sizeof(lock_t));
    #endif
    #if THREADS_T_ENABLE_CONDITION == 1
        old = t->cond;
        pthread_cond_destroy(&t->cond.cond_v);
        pthread_condattr_destroy(&t->cond.attr);
        memset(&t->cond, 0, sizeof(cond_t));
        free(old);
    #endif
    function_t* routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
    if (routine) {
        if (routine->args) {
            old = routine->args; 
            memset(routine->args, 0, routine->size * sizeof(void*));
            routine->size > threshold ? munmap_address(old, routine->size * sizeof(void*), __FILE__,  __LINE__) : free(old);  
        }
        old = routine;
        memset(routine, 0, sizeof(_Atomic(function_t)));
        free(old);
    }
}

[[gnu::aligned(DEFAULT_ALIGNMENT)]] /* Align it to a byte-boundry, so it can fit into the cache without an issue */
void debug_threads(const threads_t* tp) {
    printf("Targeted thread address: [ %p ]\n", &tp);
    #if THREADS_T_ENABLE_LOCK == 1
        printf("\n============================================\n");
        printf("Lock Attributes: [ %p ]\n", &tp->attr.mutex_attr);
        int pshared;
        if (pthread_mutexattr_getpshared(&tp->attr.mutex_attr, &pshared) != 0) 
            printf("Error failed to get thread [ %p ] mutex attribute pshared state\n", &tp);
        
        if (pshared != PTHREAD_PROCESS_SHARED) 
            printf("Thread [ %p ] process shared was not enabled.\n", &tp.attr->mutex_attr);
        printf("shared process is not enabled, assuming default settings are being used\n");
        printf("\n============================================\n");
    #endif
        
    #if THREADS_T_ENABLE_STACK == 1
        if (tp.attr->stackaddr != NULL) {
            printf("\n============================================\n");
            printf("Thread Attribute [ %p ]\n", &tp.attr->thread_attr);
            //size_t size = pthread_attr_getstack(thread_attr, void **__restrict stackaddr, STACK_SIZE);
            //printf("pthread's stack size is: [ %ud ]\n", size);
        }
    #endif

    printf("\n============================================\n");
    //int *schedpolicy;
    //struct sched_param *schedparam;
    //int res = pthread_getschedparam(tp->thread_id,  schedpolicy,  schedparam);


    printf("\n============================================\n");
}
