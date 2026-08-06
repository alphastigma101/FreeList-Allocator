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
inline void routine_metadata(threads_t* t, const int length, ...) {
    if (!t->routine) {
        t->routine = aligned_alloc(alignof(function_t), sizeof(function_t));
        if (!t->routine) return;
        memset(t->routine, 0, sizeof(function_t));
    }

    if (t->routine->size != (size_t)length) {
        if (t->routine->args) free(t->routine->args);
        t->routine->args = malloc((size_t)(length) * sizeof(void*));
        if (!t->routine->args) return;
        t->routine->size = (size_t)length;
    }
    memset(t->routine->args, 0, (size_t)(length) * sizeof(void*));

    va_list args;
    va_start(args, length);
    for (int i = 0; i < length; i++) t->routine->args[i] = va_arg(args, void*);
    va_end(args);
}

/**
 * @brief Creates a shared memory mapping accessible across processes
 * @param addr Suggested address (usually NULL for kernel to choose)
 * @param len Length of mapping in bytes
 * @param prot Memory protection (PROT_READ, PROT_WRITE, etc.)
 * @param flags Mapping flags (will be OR'd with MAP_SHARED)
 * @param fildes File descriptor (or -1 for anonymous mapping)
 * @param off Offset in the file/object (in pages, multiply by page size)
 * @return Pointer to mapped region, or MAP_FAILED on error
*/
void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off) {
    off_t offset = (off_t)off * sysconf(_SC_PAGE_SIZE);
    
    void* result = mmap(addr, len, prot, MAP_SHARED | MAP_ANONYMOUS | flags, fildes, offset);
    
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
 * @return Pointer to mapped region, or MAP_FAILED on error
*/
void* private_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off) {
    
    void* result = mmap(addr, len, prot, MAP_PRIVATE | MAP_ANONYMOUS | flags, fildes, off);
    
    if (result == MAP_FAILED) {
        fprintf(stderr, "private_address: mmap failed: %s\n", strerror(errno));
        return MAP_FAILED;
    }
    
    return result;
}

inline void* remap_address(void* addr, size_t old_len, size_t new_len) {
    void* res = mremap(addr, old_len, new_len, MREMAP_MAYMOVE);
    if (res == MAP_FAILED) return NULL;
    return res;
}

/**
 * @brief Unmaps a memory region created by shared_address or private_address
 * @param addr Pointer returned by shared_address/private_address
 * @note You must track the length separately or store it in the mapped region
*/
void munmap_address(void* addr, size_t len) {
    if (addr == NULL || addr == MAP_FAILED) {
        fprintf(stderr, "clean_address: invalid address\n");
        return;
    }
    
    if (munmap(addr, len) != 0) {
        fprintf(stderr, "clean_address: munmap failed: %s\n", strerror(errno));
    }
}

FORCE_INLINE void create_attrs(threads_t* tp, const size_t idx, const unsigned char mode, const unsigned char stack);
FORCE_INLINE void init_threads_t_stack(threads_t* tp, const size_t idx);
FORCE_INLINE threads_t init_locks_t(threads_t* t, const size_t idx);
FORCE_INLINE threads_t init_attr_t(threads_t* t, const size_t idx, const unsigned char lock);
size_t __ss = {0}; /* Abbreviated as stack size and is used in create_attrs and clean_threads */


////////////////////////
// THREADING SECTION //
//////////////////////


/**
 * @brief Initializes a thread
 * 
 * @param arr Memory address 
 * @param mode Can be shared or not shared memory space 
 * @param _str Can be a string or it can be NULL
 * 
 * @return tokens_t Object 

 * 
 * @details 
 * - addr is a memory address depending on the mode, can have shared resources or not 
 * - mode is set to default i.e 0x01 which is SHARED       
 *         - 0x02 is for ANONYMOUS
 * @note Passed unit test cases as of 3/3/26 
*/
[[gnu::hot]]
threads_t init_threads_t(const unsigned char mode, const unsigned char locked) {
    threads_t t = {0};
    //if (mode == 0x01 && locked == 0x01) {
        t = init_attr_t(&t, 0, locked);
        t = init_locks_t(&t, 0);
    //}
    return t;
}

FORCE_INLINE threads_t init_locks_t(threads_t* t, const size_t idx) {
    int rc;
    if (!t[idx].lock) {
        t[idx].lock = aligned_alloc(alignof(lock_t), sizeof(lock_t));
        memset(t[idx].lock, 0, sizeof(lock_t));
    }

    rc = pthread_mutex_init(&t[idx].lock->mutex, &t[idx].attr->mutex_attr);
    if (rc) {
        printf("pthread_mutex_init failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
        rc = pthread_mutex_init(&t[idx].lock->mutex, NULL);
        if (rc) {
            printf("pthread_mutex_init failed again: %s (errno: %d)\n\t trying other locks...\n", strerror(rc), rc);
        }
    } 
    return t[idx];
}

FORCE_INLINE threads_t init_attr_t(threads_t* t, const size_t idx, const unsigned char lock) {
    struct sched_param schedparam;
    int rc;

    if (!t[idx].attr) {
        t[idx].attr = aligned_alloc(alignof(attr_t), sizeof(attr_t));
        memset(t[idx].attr, 0, sizeof(attr_t));
    }

    rc = pthread_attr_init(&t[idx].attr->thread_attr);
    if (rc) {
        printf("pthread_attr_init failed: %s (errno: %d)\n\t will pass NULL into pthread_create later on\n", strerror(rc), rc);
    }
    
    if (lock) {
        rc = pthread_mutexattr_init(&t[idx].attr->mutex_attr);
        if (rc) {
            printf("pthread_mutexattr_init failed: %s (errno: %d)\n", strerror(rc), rc);  
        }
    }

    int inherit = INHERITSCHED == 1 ? PTHREAD_EXPLICIT_SCHED : INHERITSCHED == 0 ? PTHREAD_INHERIT_SCHED : -1;
    rc = pthread_attr_setinheritsched(&t[idx].attr->thread_attr, inherit);
    if (rc) {
        printf("pthread_attr_setinheritsched failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("inherit value is: [ %d ]\n\t INHERITSCHED macro numerical values are: (0, 1)\n", inherit);
        printf("\n\t Where 0 == PTHREAD_INHERIT_SCHED, 1 == PTHREAD_EXPLICIT_SCHED\n");
    }

    int policy = USTP == 0 ? SCHED_FIFO : USTP == 1 ? SCHED_RR : USTP == 2 ? SCHED_OTHER : -1;
    rc = pthread_attr_setschedpolicy(&t[idx].attr->thread_attr, policy);
    if (rc) {
        printf("pthread_attr_setschedpolicy failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("policy value is: [ %d ]\n\t User space thread pool policy i.e USTP macro numerical values are: (0, 1, 2)\n", policy);
        printf("\n\t Where 0 == SCHED_FIFO, 1 == SCHED_RR, and 2 == SCHED_OTHER\n");
    }

    schedparam.sched_priority = USTP == 0 ? 1 : USTP == 1 ? 1 : USTP == 2 ? 0 : -1;
    rc = pthread_attr_setschedparam(&t[idx].attr->thread_attr, &schedparam);
    if (rc) {
        printf("pthread_attr_setschedparam failed: %s (errno: %d)\n", strerror(rc), rc);
    }

    int detachstate = THREAD_STATE == 1 ? PTHREAD_CREATE_JOINABLE : THREAD_STATE == 0 ? PTHREAD_CREATE_DETACHED : -1;
    rc = pthread_attr_setdetachstate(&t[idx].attr->thread_attr, detachstate);
    if (rc) {
        printf("pthread_attr_setdetachstate failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("detachstate value is: [ %d ]\n\t THREAD_STATE Macro numerical values are: (0, 1)", detachstate);
        printf("\n\t Where 0 == PTHREAD_CREATE_DETACHED, and 1 == PTHREAD_CREATE_JOINABLE\n");
    }
    
    return t[idx];
}

FORCE_INLINE void init_threads_t_stack(threads_t* tp, const size_t idx) {
    int rc;
    unsigned int page_size = (unsigned int)sysconf(_SC_PAGESIZE);
    unsigned int base_size = (unsigned int)PTHREAD_STACK_MIN * ASAN_STACK_MULTIPLIER;
    __ss                   = (base_size + page_size - 1) & ~(page_size - 1);

    
    size_t total_with_guard = (size_t)__ss + page_size;
    rc = posix_memalign(tp[idx].attr->stackaddr, page_size, total_with_guard);
    if (rc != 0) {
        printf("posix_memalign failed: %s (errno: %d)\n", strerror(rc), rc);
        tp[idx].attr->stackaddr = NULL;
    }
    // TODO: use MAP_GROWSDOWN for architectures that are modern 
    if (tp[idx].attr->stackaddr) {
        /* stacks grow DOWNWARD on x86_64/most architectures, so the
            * guard page goes at the LOW end; the usable stack starts
            * right after it */
        void* usable_stack = (char*)tp[idx].attr->stackaddr + page_size;

        rc = pthread_attr_setstack(&tp[idx].attr->thread_attr, usable_stack, __ss);
        if (rc) {
            printf("pthread_attr_setstack failed: %s (errno: %d)\n\t swapping to pthread attribute default settings\n", strerror(rc), rc);
            pthread_attr_destroy(&tp[idx].attr->thread_attr);
        }
        else {
            /* guard ONLY the first page -- not the whole stack */
            rc = mprotect(tp[idx].attr->stackaddr, page_size, PROT_NONE);
            if (rc == -1) {
                printf("mprotect failed: %s (errno: %d)\n\t failed to guard the stack\n", strerror(errno), errno);
            }
        }
    }
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
inline void create_thread_pool(threads_t* tp, const size_t size, const unsigned char mode, const unsigned char locked, const unsigned char stack) {
    if (mode == 0x0 && !tp) tp = shared_address(NULL, size * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    else if (mode == 0x01 && !tp) tp = private_address(NULL, size * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0); 
    
    if (!tp) return;
  
    for (size_t i = 0; i < size; i++) {
        tp[i] = init_attr_t(tp, i, locked);
        if (locked) tp[i] = init_locks_t(tp, i);
        create_attrs(tp, i, locked, stack);
        tp[i].flag = 0x0;
    }
    return;
}

inline void update_thread_pool(threads_t *tp, const size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (tp[i].flag == 0x01) {
            join_thread(&tp[i], NULL);
            tp[i].flag = 0x0;
        }
    } 
}

FORCE_INLINE void create_attrs(threads_t* tp, const size_t idx, const unsigned char mode, const unsigned char stack) {
    int rc;
    
    if (tp[idx].attr) if (stack) init_threads_t_stack(tp, idx);
    if (mode == 0x01 && tp[idx].lock) {
        rc = pthread_mutexattr_setpshared(&tp[idx].attr->mutex_attr, PTHREAD_PROCESS_SHARED); 
        if (rc) {
            printf("pthread_mutexattr_setpshared failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            pthread_mutexattr_destroy(&tp[idx].attr->mutex_attr);
        }
        int kind = MUTEX_ATTR == 0 ? PTHREAD_MUTEX_DEFAULT : MUTEX_ATTR == 1 ? PTHREAD_MUTEX_ERRORCHECK : MUTEX_ATTR == 2 ? PTHREAD_MUTEX_RECURSIVE : -1;
        rc = pthread_mutexattr_settype(&tp[idx].attr->mutex_attr, kind);
        if (rc) {
            printf("pthread_mutexattr_settype failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            printf("kind value is: [ %d ]\n\t MUTEX_ATTR macro numerical values are: (0, 1, 2)\n", kind);
            printf("\n\t Where 0 == PTHREAD_MUTEX_DEFAULT, 1 == PTHREAD_MUTEX_ERRORCHECK, and 2 == PTHREAD_MUTEX_RECURSIVE\n");
            pthread_mutexattr_destroy(&tp[idx].attr->mutex_attr); 
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
void create_thread(threads_t* tp, void* func) {
    tp->flag = 0x01;
    int rc = pthread_create(&tp->thread_id, tp->attr ? &tp->attr->thread_attr : NULL, func, tp->routine);
    if (rc) {
        printf("pthread_create failed: %s (errno: %d)\n", strerror(rc), rc);
        return;
    }
    return;
}
// TODO: you can create threads without the need of attributes for the thread. So if that is the case, then it will be a stateless thread
// So we need a function that sets the detacthed state up at runtime 
void join_thread(threads_t* t, void** rtn) {
    t->flag = 0x0;
    if (t->attr) {
        int state; 
        pthread_attr_getdetachstate(&t->attr->thread_attr, &state);
        if (state != PTHREAD_CREATE_DETACHED) pthread_join(t->thread_id, rtn);
    }
    if (t->routine->args) for (size_t i = 0; i < t->routine->size; i++) t->routine->args[i] = NULL;
    return;
}

threads_t* find_thread_t(threads_t* tp, const unsigned int size) {

    for (unsigned int i = 0; i < size; i++) {
        if (tp->flag == 0x0) return &tp[i];
    }

    return NULL;
}

void clean_threads(threads_t* t) {
    if (t->flag == 0x01) {
        printf("Warning: Address of thread_t: [ %p ] has not been joined!\n ", t);
    }
    if (t->attr) {
        if (t->attr->stackaddr) {
            size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
            size_t total_with_guard = (size_t)__ss + page_size;
            if (__ss > 0) {
                mprotect(t->attr->stackaddr, total_with_guard, PROT_READ | PROT_WRITE);
                memset(t->attr->stackaddr, 0, total_with_guard);
            }
            free(t->attr->stackaddr);
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

    if (t->routine) {
        if (t->routine->args) { 
            memset(t->routine->args, 0, t->routine->size * sizeof(void*));
            free(t->routine->args); 
            t->routine->args = NULL; 
        }
        memset(t->routine, 0, sizeof(function_t));
        free(t->routine);
        t->routine = NULL;
    }

    return;
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
