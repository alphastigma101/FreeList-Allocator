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
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    // ASAN/TSAN need significantly more stack space for instrumentation
    #define ASAN_STACK_MULTIPLIER 16
#else
    #define ASAN_STACK_MULTIPLIER 1
#endif

/* Abbreviated as stack size and is used in create_attrs and clean_threads */
size_t __ss = {0};

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
void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off) {
    off_t offset = (off_t)off * sysconf(_SC_PAGE_SIZE);
    
    void* result = mmap(addr, len, prot, flags | MAP_SHARED, fildes, offset);
    
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
void* private_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off) {
    (void)fildes;  // Unused for anonymous mappings
    (void)off;     // Unused for anonymous mappings
    
    void* result = mmap(addr, len, prot, flags | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (result == MAP_FAILED) {
        fprintf(stderr, "private_address: mmap failed: %s\n", strerror(errno));
        return MAP_FAILED;
    }
    
    return result;
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
    
    if (munmap(addr, len) == -1) {
        fprintf(stderr, "clean_address: munmap failed: %s\n", strerror(errno));
    }
}

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
threads_t* init_threads_t() {

    struct sched_param schedparam;
    int rc;
    threads_t* t = shared_address(NULL, sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!t) return NULL;

    rc = pthread_mutexattr_init(&t->mutex_attr);
    if (rc) {
        printf("pthread_mutexattr_init failed: %s (errno: %d)\n", strerror(rc), rc);
        munmap_address(t, sizeof(threads_t));
        // TODO: Try other locks 
        return NULL;
    }

    pthread_mutex_t mutex = {0};
    rc = pthread_mutex_init(&mutex, &t->mutex_attr);
    if (rc) {
        printf("pthread_mutex_init failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
        rc = pthread_mutex_init(&mutex, NULL);
        if (rc) {
            printf("pthread_mutex_init failed again: %s (errno: %d)\n\t trying other locks...\n", strerror(rc), rc);
        }
        // TODO: Try other locks here 
    } 
    else {

        t->mutex = aligned_alloc(alignof(pthread_mutex_t), sizeof(pthread_mutex_t));
        memmove(t->mutex,       &mutex,       sizeof(pthread_mutex_t));
        pthread_mutex_destroy(&mutex);

    }

    pthread_attr_t thread_attr = {0};
    rc = pthread_attr_init(&thread_attr);
    if (rc) {

        printf("pthread_attr_init failed: %s (errno: %d)\n\t will pass NULL into pthread_create later on\n", strerror(rc), rc);
        // TODO: If this fails, then we can swap over and use semaphores instead.
    } 
    else {

        t->thread_attr = aligned_alloc(alignof(pthread_attr_t), sizeof(pthread_attr_t));
        memmove(t->thread_attr,       &thread_attr,       sizeof(pthread_attr_t));
        pthread_attr_destroy(&thread_attr);

    }

    int inherit = INHERITSCHED == 1 ? PTHREAD_EXPLICIT_SCHED : INHERITSCHED == 0 ? PTHREAD_INHERIT_SCHED : -1;
    rc = pthread_attr_setinheritsched(t->thread_attr, inherit);
    if (rc) {
        printf("pthread_attr_setinheritsched failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("inherit value is: [ %d ]\n\t INHERITSCHED macro numerical values are: (0, 1)\n", inherit);
        printf("\n\t Where 0 == PTHREAD_INHERIT_SCHED, 1 == PTHREAD_EXPLICIT_SCHED\n");
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    int policy = USTP == 0 ? SCHED_FIFO : USTP == 1 ? SCHED_RR : USTP == 2 ? SCHED_OTHER : -1;
    rc = pthread_attr_setschedpolicy(t->thread_attr, policy);
    if (rc) {
        printf("pthread_attr_setschedpolicy failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("policy value is: [ %d ]\n\t User space thread pool policy i.e USTP macro numerical values are: (0, 1, 2)\n", policy);
        printf("\n\t Where 0 == SCHED_FIFO, 1 == SCHED_RR, and 2 == SCHED_OTHER\n");
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    schedparam.sched_priority = USTP == 0 ? 1 : USTP == 1 ? 1 : USTP == 2 ? 0 : -1;
    rc = pthread_attr_setschedparam(t->thread_attr, &schedparam);
    if (rc) {
        printf("pthread_attr_setschedparam failed: %s (errno: %d)\n", strerror(rc), rc);
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    int detachstate = THREAD_STATE == 1 ? PTHREAD_CREATE_JOINABLE : THREAD_STATE == 0 ? PTHREAD_CREATE_DETACHED : -1;
    rc = pthread_attr_setdetachstate(t->thread_attr, detachstate);
    if (rc) {
        printf("pthread_attr_setdetachstate failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("detachstate value is: [ %d ]\n\t THREAD_STATE Macro numerical values are: (0, 1)", detachstate);
        printf("\n\t Where 0 == PTHREAD_CREATE_DETACHED, and 1 == PTHREAD_CREATE_JOINABLE\n");
        // TODO: Try something else, like a different attribute or use semaphores 
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    t->flag = 0x0;
    return t;
}

FORCE_INLINE threads_t* create_attrs(threads_t* tp, const uint8_t mode) {
    int rc;
    const void* mutex_attr = &tp->mutex_attr;
    const void* thread_attr = &tp->thread_attr;
    
    if (thread_attr) {
        
        size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
        size_t base_size = PTHREAD_STACK_MIN * ASAN_STACK_MULTIPLIER;
        __ss             = (base_size + page_size - 1) & ~(page_size - 1);

        rc = pthread_attr_setstacksize(tp->thread_attr, __ss);
        if (rc) {
            printf("pthread_attr_setstacksize failed: %s (errno: %d)\n\t swapping to pthread attribute default settings\n", strerror(rc), rc);
            pthread_attr_destroy(tp->thread_attr);
        }

        // pthread_attr_setguardsize will be ignored, since pthread_attr_setstacksize is used in this scope
        // TODO: Swap malloc out with this and modify the flags: private_address(NULL, ss, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        tp->stackaddr = malloc(__ss); 
        rc            = pthread_attr_getstack(tp->thread_attr, tp->stackaddr, &__ss);
        if (rc) {
            printf("ppthread_attr_getstack failed: %s (errno: %d)\n\t failed to get stack address\n", strerror(rc), rc);
            pthread_attr_destroy(tp->thread_attr);
        }
        else {
            rc = mprotect(tp->stackaddr, __ss, PROT_NONE);
            if (rc == -1) {
                printf("mprotect failed: %s (errno: %d)\n\t failed to get stack address\n", strerror(rc), rc);
                // TODO: We then use semaphores instead
            }
        }
    }
    if (mode == 0x01 && mutex_attr) {
        rc = pthread_mutexattr_setpshared(&tp->mutex_attr, PTHREAD_PROCESS_SHARED); 
        if (rc) {
            printf("pthread_mutexattr_setpshared failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            pthread_mutexattr_destroy(&tp->mutex_attr);
        }

        int kind = MUTEX_ATTR == 0 ? PTHREAD_MUTEX_DEFAULT : MUTEX_ATTR == 1 ? PTHREAD_MUTEX_ERRORCHECK : MUTEX_ATTR == 2 ? PTHREAD_MUTEX_RECURSIVE : -1;
        rc = pthread_mutexattr_settype(&tp->mutex_attr, kind);
        if (rc) {
            printf("pthread_mutexattr_settype failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            printf("kind value is: [ %d ]\n\t MUTEX_ATTR macro numerical values are: (0, 1, 2)\n", kind);
            printf("\n\t Where 0 == PTHREAD_MUTEX_DEFAULT, 1 == PTHREAD_MUTEX_ERRORCHECK, and 2 == PTHREAD_MUTEX_RECURSIVE\n");
            pthread_mutexattr_destroy(&tp->mutex_attr);
            // TODO: Try other locks 
        }

    }
    else if (mode == 0x02) {
        // Aquire default settings 
        if (tp->thread_attr) pthread_attr_destroy(tp->thread_attr);
        const void* mutex_attr = &tp->mutex_attr;
        if (mutex_attr) pthread_mutexattr_destroy(&tp->mutex_attr);
    }

    return tp;
}

/** 
    * @description: Free function that creates a thread and makes it runnable by calling pthread_create. 
    * @param tp: tp is a thread user defined type. It should be initialized by init_threads before this is called.
    * @param mode: shared resources mode is 0x01, otherwise 0x02 should be used
    * @param func: The function i.e the subroutine you want to call.
    * @note: There are cases where the new thread can spawn in and be terminated before pthread_create is done, so checking ESRCH error code using the thread id is crucial.
            Also, thread id pthread_t is a opaque object meaning it can be a numeric value or a struct. Do not initialize it at all 
*/
threads_t* create_thread(threads_t* tp, const uint8_t mode, void* func) {
    if (tp) {
        tp = create_attrs(tp, mode);
        int rc = pthread_create(&tp->thread_id, tp->thread_attr, func, (void*)&tp->args);
        if (rc) {
            printf("pthread_create failed: %s (errno: %d)\n", strerror(rc), rc);
            // TODO: Could try creating a semaphore here instead.
            return tp;
        }
    }

    return tp;
}


void join_thread(threads_t* t, const void** rtn) {
    int state; 
    pthread_attr_getdetachstate(t->thread_attr, &state);
    if (state != PTHREAD_CREATE_DETACHED)
        pthread_join(t->thread_id, (void**)rtn); 
    return;
}

void upid(threads_t *tp) {
    // Compare the threads to see if they are the same
    pthread_t pid = pthread_self(); 
    if (pthread_equal(tp->thread_id, pid) == 0) {
        printf("\n========================================\n");
        printf("In debug_threads function: %p which is a pointer to user space threads id is not the same\n", tp);
    }
    else {

    }

}

void clean_threads(threads_t* t) {
    if (t) {
        if (t->mutex) {
            memset(t->mutex, 0, sizeof(pthread_mutex_t));
            free(t->mutex);
            pthread_mutexattr_destroy(&t->mutex_attr);
        }
        if (t->thread_attr) {
            if (__ss > 0) memset(t->stackaddr, 0, __ss);
            if (t->stackaddr) free(t->stackaddr);
            memset(t->thread_attr, 0, sizeof(pthread_attr_t));
            free(t->thread_attr);

        }
        if (t->args.arr) {
            // size_t size = sizeof(t->args.arr) / t->args.arr[0]; // get the length of the array
            free(t->args.arr);
        }

        munmap_address(t, sizeof(threads_t));

    }
}


void debug_threads(const threads_t* tp) {
    if (tp) {
        printf("Targeted thread address: [ %p ]\n", tp);
        if (tp->mutex) {
            printf("\n============================================\n");
            printf("Lock Attributes: [ %p ]\n", &tp->mutex_attr);
            int pshared;
            if (pthread_mutexattr_getpshared(&tp->mutex_attr, &pshared) != 0) 
                printf("Error failed to get thread [ %p ] mutex attribute pshared state\n", tp);
            
            if (pshared != PTHREAD_PROCESS_SHARED) 
                printf("Thread [ %p ] process shared was not enabled.\n", &tp->mutex_attr);
            printf("shared process is not enabled, assuming default settings are being used\n");
            printf("\n============================================\n");
        }
        
        const void* thread_attr = &tp->thread_attr;
        if (thread_attr) {
            printf("\n============================================\n");
            printf("Thread Attribute [ %p ]\n", thread_attr);
            //size_t size = pthread_attr_getstack(thread_attr, void **__restrict stackaddr, STACK_SIZE);
            //printf("pthread's stack size is: [ %ud ]\n", size);
        }

        printf("\n============================================\n");
        //int *schedpolicy;
        //struct sched_param *schedparam;
        //int res = pthread_getschedparam(tp->thread_id,  schedpolicy,  schedparam);

    }

    printf("\n============================================\n");
}
