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
#include "../logger/buffer.h"
#include <stdalign.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_GNU 1
#include <sys/mman.h>

/*
#include "threads.h"
#include "../logger/buffer.h"
#include <stdalign.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_GNU 1
#include <sys/mman.h>
*/

typedef struct function_t {
    void** args;
    size_t size;
} function_t;


/////////////////////////
// FUNCTION_T SECTION //
///////////////////////


inline void** routine_metadata_arguments(struct function_t* meta) { return meta->args; }

[[gnu::hot]]
inline void routine_metadata(threads_t* t, const size_t length, ...) {
    function_t* routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
    if (!routine) {
        routine = aligned_alloc(alignof(function_t), sizeof(function_t));
        if (!routine) return;
        memset(routine, 0, sizeof(function_t));
    }

    if (routine->size != length) {
        if (routine->args) {
            if (routine->size < ALLOC_THRESHOLD && length > ALLOC_THRESHOLD) {
                memset(routine->args, 0, length * sizeof(void*));
                free(routine->args);
                routine->args = NULL;
                routine->args = private_address(NULL, length * sizeof(void*), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
                if (routine->args == MAP_FAILED) return;
                else {
                    int res = madvise(routine->args, length * sizeof(void*), MADV_SEQUENTIAL | MADV_MERGEABLE);
                    if (res == -1) {
                        memset(routine->args, 0, length * sizeof(void*));
                        if (routine->args) munmap_address(routine->args, length * sizeof(void*));
                        routine->args = NULL;
                        return;
                    }
                }
            }
            else if (routine->size > ALLOC_THRESHOLD && length < ALLOC_THRESHOLD) {
                munmap_address(routine->args, length * sizeof(void*));
                routine->args = NULL;
                routine->args = malloc(length * sizeof(void*));
            }
            else routine->args = length < ALLOC_THRESHOLD ? realloc(routine->args, length * sizeof(void*)) : remap_address(routine->args, routine->size, length);
        }
        else routine->args = length < ALLOC_THRESHOLD ? malloc(length * sizeof(void*)) : private_address(NULL, length * sizeof(void*), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
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

[[gnu::hot]]
FORCE_INLINE unsigned char thread_t_routine_tagged(_Atomic(struct function_t*)* meta) {
    function_t* cur = atomic_load_explicit(meta, memory_order_relaxed);
    if (IS_ADDRESS_TAGGED(cur)) return 0x01;
    return 0x0;
}

[[gnu::hot]]
inline void thread_t_routine_untag(_Atomic(struct function_t*)* meta) {
    if (thread_t_routine_tagged(meta)) {
        function_t* cur = atomic_load_explicit(meta, memory_order_relaxed);
        cur = UNTAG_ADDRESS(cur);
        atomic_store_explicit(meta, cur, memory_order_relaxed);
    }
}

[[gnu::hot]]
inline void thread_t_routine_tag(_Atomic(struct function_t*)* meta) {
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
inline void munmap_address(void* addr, size_t len) {
    if (addr == NULL || addr == MAP_FAILED) {
        fprintf(stderr, "clean_address: invalid address\n");
        return;
    }
    
    if (munmap(addr, len) != 0) {
        fprintf(stderr, "clean_address: munmap failed: %s\n", strerror(errno));
    }
}

FORCE_INLINE void init_threads_t_stack(threads_t* tp, const size_t idx);
FORCE_INLINE threads_t init_locks_t(threads_t* t, const size_t idx, const unsigned char mode);
FORCE_INLINE threads_t init_attr_t(threads_t* t, const size_t idx, const unsigned char attr);
FORCE_INLINE threads_t threads_t_state(threads_t* t, const size_t idx, const unsigned char state);
size_t __ss = {0}; /* Abbreviated as stack size and is used in create_attrs and clean_threads */


////////////////////////
// THREADING SECTION //
//////////////////////


/**
 * @description: Free function that creates a thread_t object on stack based on the parameters
 * @param mode: 0x01 is for enabling shared process. This will also effect the mutex 0x02 turns it off.
 * @param attr: If mode is 0x01 and attr is also 0x01, then mutex will become a shared lock
                If mode is 0x0 and attr is 0x01, attr for thread will be initialized, but mutex will be a independent lock
                If mode is 0x0 and attr is 0x0, attr will not be initialized
*/
[[gnu::hot]]
threads_t init_threads_t(const unsigned char mode, const unsigned char attr, const unsigned char locked, const unsigned char stack) {
    threads_t t = {0};
    if (mode == 0x0 && attr == 0x01) {
        t = init_attr_t(&t, SIZE_MAX, attr);
        if (locked == 0x01) t = init_locks_t(&t, SIZE_MAX, mode);
        if (stack == 0x01) init_threads_t_stack(&t, SIZE_MAX);
    }
    if ((mode == 0x01) && (attr == 0x01)) {
        t = init_attr_t(&t, SIZE_MAX, attr);
        if (locked == 0x01) t = init_locks_t(&t, SIZE_MAX, mode);
        if (stack == 0x01) init_threads_t_stack(&t, SIZE_MAX);
    }
    if ((mode == 0x02) && (attr == 0x01)) {
        if (locked == 0x01) {
            t = init_attr_t(&t, SIZE_MAX, attr);
            t = init_locks_t(&t, SIZE_MAX, mode);
        } 
        if (stack == 0x01) {
            if (!t.attr) t = init_attr_t(&t, SIZE_MAX, attr);
            init_threads_t_stack(&t, SIZE_MAX);
        }
    }
    return t;
}

FORCE_INLINE threads_t init_locks_t(threads_t* t, const size_t idx, const unsigned char mode) {
    int rc;

    if (idx != SIZE_MAX && !t[idx].lock) {
        t[idx].lock = aligned_alloc(alignof(lock_t), sizeof(lock_t));
        if (!t[idx].lock) return t[idx];
        memset(t[idx].lock, 0, sizeof(lock_t));
    }
    else if (idx == SIZE_MAX && !t->lock) {
        t->lock = aligned_alloc(alignof(lock_t), sizeof(lock_t));
        if (!t->lock) return *t;
        memset(t->lock, 0, sizeof(lock_t));
    }

    const void* ptr = idx != SIZE_MAX ? t[idx].attr : t->attr;
    if (ptr) {
        if (mode == 0x01) {
            rc = pthread_mutexattr_init(idx != SIZE_MAX ? &t[idx].attr->mutex_attr : &t->attr->mutex_attr);
            if (rc) {
                printf("pthread_mutexattr_init failed: %s (errno: %d)\n", strerror(rc), rc);  
            }
            rc = pthread_mutexattr_setpshared(idx != SIZE_MAX ? &t[idx].attr->mutex_attr : &t->attr->mutex_attr, PTHREAD_PROCESS_SHARED); 
            if (rc) {
                printf("pthread_mutexattr_setpshared failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
                pthread_mutexattr_destroy(idx != SIZE_MAX ? &t[idx].attr->mutex_attr : &t->attr->mutex_attr);
            }
            int kind = MUTEX_ATTR == 0 ? PTHREAD_MUTEX_DEFAULT : MUTEX_ATTR == 1 ? PTHREAD_MUTEX_ERRORCHECK : MUTEX_ATTR == 2 ? PTHREAD_MUTEX_RECURSIVE : -1;
            rc = pthread_mutexattr_settype(idx != SIZE_MAX ? &t[idx].attr->mutex_attr : &t->attr->mutex_attr, kind);
            if (rc) {
                printf("pthread_mutexattr_settype failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
                printf("kind value is: [ %d ]\n\t MUTEX_ATTR macro numerical values are: (0, 1, 2)\n", kind);
                printf("\n\t Where 0 == PTHREAD_MUTEX_DEFAULT, 1 == PTHREAD_MUTEX_ERRORCHECK, and 2 == PTHREAD_MUTEX_RECURSIVE\n");
                pthread_mutexattr_destroy(idx != SIZE_MAX ? &t[idx].attr->mutex_attr : &t->attr->mutex_attr); 
            }
        }
    }

    rc = pthread_mutex_init(idx != SIZE_MAX ? &t[idx].lock->mutex : &t->lock->mutex, idx != SIZE_MAX ? &t[idx].attr->mutex_attr : &t->attr->mutex_attr);
    if (rc) {
        printf("pthread_mutex_init failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
        rc = pthread_mutex_init(idx != SIZE_MAX ? &t[idx].lock->mutex : &t->lock->mutex, NULL);
        if (rc) {
            printf("pthread_mutex_init failed again: %s (errno: %d)\n\t trying other locks...\n", strerror(rc), rc);
        }
    } 
    return idx != SIZE_MAX ? t[idx] : *t;
}

FORCE_INLINE threads_t init_attr_t(threads_t* t, const size_t idx, const unsigned char attr) {
    struct sched_param schedparam;
    int rc;

    if (idx != SIZE_MAX && !t[idx].attr && attr) {
        t[idx].attr = aligned_alloc(alignof(attr_t), sizeof(attr_t));
        if (!t[idx].attr) return t[idx];
        memset(t[idx].attr, 0, sizeof(attr_t));
    }
    else if (idx == SIZE_MAX && !t->attr && attr) {
        t->attr = aligned_alloc(alignof(attr_t), sizeof(attr_t));
        if (!t->attr) return *t;
        memset(t->attr, 0, sizeof(attr_t));
    }

    rc = pthread_attr_init(idx != SIZE_MAX ? &t[idx].attr->thread_attr : &t->attr->thread_attr);
    if (rc) {
        printf("pthread_attr_init failed: %s (errno: %d)\n\t will pass NULL into pthread_create later on\n", strerror(rc), rc);
    }

    int inherit = INHERITSCHED == 1 ? PTHREAD_EXPLICIT_SCHED : INHERITSCHED == 0 ? PTHREAD_INHERIT_SCHED : -1;
    rc = pthread_attr_setinheritsched(idx != SIZE_MAX ? &t[idx].attr->thread_attr : &t->attr->thread_attr, inherit);
    if (rc) {
        printf("pthread_attr_setinheritsched failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("inherit value is: [ %d ]\n\t INHERITSCHED macro numerical values are: (0, 1)\n", inherit);
        printf("\n\t Where 0 == PTHREAD_INHERIT_SCHED, 1 == PTHREAD_EXPLICIT_SCHED\n");
    }

    int policy = USTP == 0 ? SCHED_FIFO : USTP == 1 ? SCHED_RR : USTP == 2 ? SCHED_OTHER : -1;
    rc = pthread_attr_setschedpolicy(idx != SIZE_MAX ? &t[idx].attr->thread_attr : &t->attr->thread_attr, policy);
    if (rc) {
        printf("pthread_attr_setschedpolicy failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("policy value is: [ %d ]\n\t User space thread pool policy i.e USTP macro numerical values are: (0, 1, 2)\n", policy);
        printf("\n\t Where 0 == SCHED_FIFO, 1 == SCHED_RR, and 2 == SCHED_OTHER\n");
    }

    schedparam.sched_priority = USTP == 0 ? 1 : USTP == 1 ? 1 : USTP == 2 ? 0 : -1;
    rc = pthread_attr_setschedparam(idx != SIZE_MAX ? &t[idx].attr->thread_attr : &t->attr->thread_attr, &schedparam);
    if (rc) {
        printf("pthread_attr_setschedparam failed: %s (errno: %d)\n", strerror(rc), rc);
    }

    int detachstate = THREAD_STATE == 1 ? PTHREAD_CREATE_JOINABLE : THREAD_STATE == 0 ? PTHREAD_CREATE_DETACHED : -1;
    rc = pthread_attr_setdetachstate(idx != SIZE_MAX ? &t[idx].attr->thread_attr : &t->attr->thread_attr, detachstate);
    if (rc) {
        printf("pthread_attr_setdetachstate failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("detachstate value is: [ %d ]\n\t THREAD_STATE Macro numerical values are: (0, 1)", detachstate);
        printf("\n\t Where 0 == PTHREAD_CREATE_DETACHED, and 1 == PTHREAD_CREATE_JOINABLE\n");
    }
    
    return idx != SIZE_MAX ? t[idx] : *t;
}

FORCE_INLINE void init_threads_t_stack(threads_t* tp, const size_t idx) {
    int rc;
    size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    size_t base_size = (size_t)PTHREAD_STACK_MIN * ASAN_STACK_MULTIPLIER;
    __ss             = (base_size + page_size - 1) & ~(page_size - 1);

    size_t total_with_guard = (size_t)__ss + page_size;
    if (idx == SIZE_MAX) tp->attr->stackaddr = __ss > ALLOC_THRESHOLD ? private_address(NULL, total_with_guard, PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0) : aligned_alloc(page_size, total_with_guard);
    else tp[idx].attr->stackaddr = __ss > ALLOC_THRESHOLD ? private_address(NULL, total_with_guard, PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0) : aligned_alloc(page_size, total_with_guard);
    
    void* res = idx == SIZE_MAX ? tp->attr->stackaddr : tp[idx].attr->stackaddr;
    if (!res) {
        if (idx == SIZE_MAX) tp->attr->stackaddr = NULL;
        else tp[idx].attr->stackaddr = NULL;
        return;
    }

    // TODO: use MAP_GROWSDOWN for architectures that are modern
    void* stack_ptr = idx == SIZE_MAX ? tp->attr->stackaddr : tp[idx].attr->stackaddr;
    if (stack_ptr) {
        /* stacks grow DOWNWARD on x86_64/most architectures, so the
            * guard page goes at the LOW end; the usable stack starts
            * right after it */
        void* usable_stack = idx == SIZE_MAX ? (char*)tp->attr->stackaddr + page_size : (char*)tp[idx].attr->stackaddr + page_size;
        rc = pthread_attr_setstack(idx == SIZE_MAX ? &tp->attr->thread_attr : &tp[idx].attr->thread_attr, usable_stack, __ss);
        if (rc) {
            printf("pthread_attr_setstack failed: %s (errno: %d)\n\t swapping to pthread attribute default settings\n", strerror(rc), rc);
            pthread_attr_destroy(idx == SIZE_MAX ? &tp->attr->thread_attr : &tp[idx].attr->thread_attr);
        }
        else {
            rc = mprotect(idx == SIZE_MAX ? tp->attr->stackaddr : tp[idx].attr->stackaddr, page_size, PROT_NONE);
            if (rc == -1) {
                printf("mprotect failed: %s (errno: %d)\n\t failed to guard the stack\n", strerror(errno), errno);
            }
        }
    }
}

FORCE_INLINE threads_t threads_t_state(threads_t* t, const size_t idx, const unsigned char state) {
    if (state) {
        printf("Still under development\n");
        
    }
    int detachstate = THREAD_STATE == 1 ? PTHREAD_CREATE_JOINABLE : THREAD_STATE == 0 ? PTHREAD_CREATE_DETACHED : -1;
    int rc = pthread_attr_setdetachstate(idx != SIZE_MAX ? &t[idx].attr->thread_attr : &t->attr->thread_attr, detachstate);
    if (rc) {
        printf("pthread_attr_setdetachstate failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("detachstate value is: [ %d ]\n\t THREAD_STATE Macro numerical values are: (0, 1)", detachstate);
        printf("\n\t Where 0 == PTHREAD_CREATE_DETACHED, and 1 == PTHREAD_CREATE_JOINABLE\n");
    }

    return idx != SIZE_MAX ? t[idx] : *t;
}

/**
    * @description: A Free function that creates a dynamic array of threads based on the arguments during runtime
    * @param tp: A pointer of type threads_t. If null it will be mmaped based on the `mode`.
    * @param size: The size of the pool.
    * @param mode: 0x0 enables shared while 0x01 enables private
    * @param locked: Enable locks allocation. Default lock that is used is mutex.
    * @param stack: 0x0 to disable integration of stack with guard for each thread, otherwise 0x01
*/
[[gnu::hot]]
void create_thread_pool(threads_t* tp, const size_t size, const unsigned char mode, const unsigned char attr, const unsigned char locked, const unsigned char stack) {
    
    if (!tp && mode == 0x0) {
        tp = aligned_alloc(alignof(threads_t), size * sizeof(threads_t));
        if (!tp) return;
        memset(tp, 0, size * sizeof(threads_t));
    }

    for (size_t i = 0; i < size; i++) {
        if (mode == 0x0 && attr == 0x01) {
            tp[i] = init_attr_t(tp, i, attr);
            if (locked == 0x01) tp[i] = init_locks_t(tp, i, mode);
            if (stack == 0x01) init_threads_t_stack(tp, i);
        }
        if ((mode == 0x01) && (attr == 0x01)) {
            if (!tp) {
                tp = shared_address(NULL, size * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
                if (tp == MAP_FAILED) return;
                else {
                    int res = madvise(tp,size * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
                    if (res == -1) {
                        if (tp) munmap_address(tp, size * sizeof(threads_t));
                        tp = NULL;
                        return;
                    }
                    memset(tp, 0, size * sizeof(threads_t));
                }
            }
            tp[i] = init_attr_t(tp, i, attr);
            if (locked == 0x01) tp[i] = init_locks_t(tp, i, mode);
            if (stack == 0x01) init_threads_t_stack(tp, i);
        }
        if ((mode == 0x02) && (attr == 0x01)) {
            if (!tp) {
                tp = private_address(NULL, size * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
                if (tp == MAP_FAILED) return;
                else {
                    int res = madvise(tp,size * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
                    if (res == -1) {
                        if (tp) munmap_address(tp, size * sizeof(threads_t));
                        tp = NULL;
                        return;
                    }
                    memset(tp, 0, size * sizeof(threads_t));
                }
            }
            if (locked == 0x01) {
                tp[i] = init_attr_t(tp, i, attr);
                tp[i] = init_locks_t(tp, i, mode);
            } 
            if (stack == 0x01) {
                if (!tp[i].attr) tp[i] = init_attr_t(tp, i, attr);
                init_threads_t_stack(tp, i);
            }
        }
    }
    return;
}

[[gnu::hot]]
void create_thread_pool_range(threads_t* tp, const unsigned char mode, const unsigned char attr, const unsigned char locked, const unsigned char stack, const size_t start, const size_t end) {
    
    if (!tp && mode == 0x0) {
        tp = aligned_alloc(alignof(threads_t), end * sizeof(threads_t));
        if (!tp) return;
        memset(tp, 0, end * sizeof(threads_t));
    }

    for (size_t i = start; i < end; i++) {
        if (mode == 0x0 && attr == 0x01) {
            tp[i] = init_attr_t(tp, i, attr);
            if (locked == 0x01) tp[i] = init_locks_t(tp, i, mode);
            if (stack == 0x01) init_threads_t_stack(tp, i);
        }
        if ((mode == 0x01) && (attr == 0x01)) {
            if (!tp) {
                tp = shared_address(NULL, end * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
                if (tp == MAP_FAILED) return;
                else {
                    int res = madvise(tp, end * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
                    if (res == -1) {
                        if (tp) munmap_address(tp, end * sizeof(threads_t));
                        tp = NULL;
                        return;
                    }
                    memset(tp, 0, end * sizeof(threads_t));
                }
            }
            tp[i] = init_attr_t(tp, i, attr);
            if (locked == 0x01) tp[i] = init_locks_t(tp, i, mode);
            if (stack == 0x01) init_threads_t_stack(tp, i);
        }
        if ((mode == 0x02) && (attr == 0x01)) {
            if (!tp) {
                tp = private_address(NULL, end * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
                if (tp == MAP_FAILED) return;
                else {
                    int res = madvise(tp, end * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
                    if (res == -1) {
                        if (tp) munmap_address(tp, end * sizeof(threads_t));
                        tp = NULL;
                        return;
                    }
                    memset(tp, 0, end * sizeof(threads_t));
                }
            }
            if (locked == 0x01) {
                tp[i] = init_attr_t(tp, i, attr);
                tp[i] = init_locks_t(tp, i, mode);
            } 
            if (stack == 0x01) {
                if (!tp[i].attr) tp[i] = init_attr_t(tp, i, attr);
                init_threads_t_stack(tp, i);
            }
        }
    }
    return;
}

inline void update_thread_pool(threads_t *tp, const size_t size) {
    pthread_t self = pthread_self();
    for (size_t i = 0; i < size; i++) {
        if (thread_t_routine_tagged(&tp[i].routine) && !pthread_equal(tp[i].thread_id, self)) {
            thread_t_routine_untag(&tp[i].routine);
            join_thread(&tp[i], NULL);
        }
    }
}

/** 
    * @description: Free function that creates a thread and makes it runnable by calling pthread_create. 
    * @param tp: tp is a thread user defined type. It should be initialized by init_threads before this is called.
    * @param mode: shared resources mode is 0x01, otherwise 0x02 should be used
    * @param func: The function i.e the subroutine you want to call.
    * @note: There are cases where the new thread can spawn in and be terminated before pthread_create is done, so checking ESRCH error code using the thread id is crucial.
            Also, thread id pthread_t is a opaque object meaning it can be a numeric value or a struct. Do not initialize it at all 
*/
inline void create_thread(threads_t* tp, void* func) {
    pthread_t self = pthread_self();
    if (!pthread_equal(tp->thread_id, self)) {
        int rc = pthread_create(&tp->thread_id, tp->attr ? &tp->attr->thread_attr : NULL, func, atomic_load_explicit(&tp->routine, memory_order_relaxed));
        if (rc) {
            printf("pthread_create failed: %s (errno: %d)\n", strerror(rc), rc);
            return;
        }
        thread_t_routine_tag(&tp->routine);
    }
}

inline void join_thread(threads_t* t, void** rtn) {
    pthread_t self = pthread_self();
    if (!pthread_equal(t->thread_id, self)) {
        int state;
        if (t->attr) { 
            pthread_attr_getdetachstate(&t->attr->thread_attr, &state);
            if (state != PTHREAD_CREATE_DETACHED) pthread_join(t->thread_id, rtn);
        } else pthread_join(t->thread_id, rtn);

        function_t* routine = NULL;
        if (thread_t_routine_tagged(&t->routine)) {
            thread_t_routine_untag(&t->routine);
            routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
        } else routine = atomic_load_explicit(&t->routine, memory_order_relaxed);

        if (routine && routine->args) for (size_t i = 0; i < routine->size; i++) routine->args[i] = NULL;
        atomic_store_explicit(&t->routine, routine, memory_order_relaxed);
    }
}

inline threads_t* find_thread_t(threads_t* tp, const size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (!thread_t_routine_tagged(&tp[i].routine)) return &tp[i];
    }
    return NULL;
}

inline size_t thread_pool_index(threads_t* tp, threads_t* t) {
    if (!tp || !t) return SIZE_MAX;
    return (size_t)(t - tp);
}

void clean_threads(threads_t* t) {
    if (thread_t_routine_tagged(&t->routine)) {
        printf("Warning: Address of thread_t: [ %p ] has not been joined!\n ", (void*)t);
        thread_t_routine_untag(&t->routine);
        join_thread(t, NULL);
    }


    if (t->attr) {
        if (t->attr->stackaddr) {
            size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
            size_t total_with_guard = (size_t)__ss + page_size;
            if (__ss > 0) {
                mprotect(t->attr->stackaddr, total_with_guard, PROT_READ | PROT_WRITE);
                memset(t->attr->stackaddr, 0, total_with_guard);
            }
            __ss > ALLOC_THRESHOLD ? munmap_address(t->attr->stackaddr, total_with_guard) : free(t->attr->stackaddr);
            t->attr->stackaddr = NULL;
        }
        pthread_attr_destroy(&t->attr->thread_attr);
        pthread_mutexattr_destroy(&t->attr->mutex_attr);
        memset(t->attr, 0, sizeof(attr_t));
        free(t->attr);
        t->attr = NULL;
    }

    if (t->lock) {
        pthread_mutex_destroy(&t->lock->mutex);
        memset(t->lock, 0, sizeof(lock_t));
        free(t->lock);
        t->lock = NULL;
    }

    function_t* routine = atomic_load_explicit(&t->routine, memory_order_relaxed);
    if (routine) {
        if (routine->args) { 
            memset(routine->args, 0, routine->size * sizeof(void*));
            routine->size > ALLOC_THRESHOLD ? munmap_address(routine->args, routine->size * sizeof(void*)) : free(routine->args); 
            routine->args = NULL; 
        }
        memset(routine, 0, sizeof(function_t));
        free(routine);
        atomic_store_explicit(&t->routine, NULL, memory_order_release);
    }
}


void debug_threads(const threads_t tp) {
    printf("Targeted thread address: [ %p ]\n", &tp);
    if (tp.lock) {
        printf("\n============================================\n");
        printf("Lock Attributes: [ %p ]\n", &tp.attr->mutex_attr);
        int pshared;
        if (pthread_mutexattr_getpshared(&tp.attr->mutex_attr, &pshared) != 0) 
            printf("Error failed to get thread [ %p ] mutex attribute pshared state\n", &tp);
        
        if (pshared != PTHREAD_PROCESS_SHARED) 
            printf("Thread [ %p ] process shared was not enabled.\n", &tp.attr->mutex_attr);
        printf("shared process is not enabled, assuming default settings are being used\n");
        printf("\n============================================\n");
    }
        
        
    if (tp.attr->stackaddr != NULL) {
        printf("\n============================================\n");
        printf("Thread Attribute [ %p ]\n", &tp.attr->thread_attr);
        //size_t size = pthread_attr_getstack(thread_attr, void **__restrict stackaddr, STACK_SIZE);
        //printf("pthread's stack size is: [ %ud ]\n", size);
    }

    printf("\n============================================\n");
    //int *schedpolicy;
    //struct sched_param *schedparam;
    //int res = pthread_getschedparam(tp->thread_id,  schedpolicy,  schedparam);


    printf("\n============================================\n");
}
