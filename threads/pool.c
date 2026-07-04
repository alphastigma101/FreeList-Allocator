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
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

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
    
    void* result = mmap(addr, len, prot, flags | MAP_PRIVATE | MAP_ANONYMOUS, fildes, off);
    
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
threads_t init_threads_t() {

    struct sched_param schedparam;
    threads_t t = {0};
    int rc;

    rc = pthread_mutexattr_init(&t.attr.mutex_attr);
    if (rc) {
        printf("pthread_mutexattr_init failed: %s (errno: %d)\n", strerror(rc), rc);
        // TODO: Try other locks
    }

    rc = pthread_mutex_init(&t.lock.mutex, &t.attr.mutex_attr);
    if (rc) {
        printf("pthread_mutex_init failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
        rc = pthread_mutex_init(&t.lock.mutex, NULL);
        if (rc) {
            printf("pthread_mutex_init failed again: %s (errno: %d)\n\t trying other locks...\n", strerror(rc), rc);
        }
        // TODO: Try other locks here 
    } 

    rc = pthread_attr_init(&t.attr.thread_attr);
    if (rc) {

        printf("pthread_attr_init failed: %s (errno: %d)\n\t will pass NULL into pthread_create later on\n", strerror(rc), rc);
        // TODO: If this fails, then we can swap over and use semaphores instead.
    } 


    int inherit = INHERITSCHED == 1 ? PTHREAD_EXPLICIT_SCHED : INHERITSCHED == 0 ? PTHREAD_INHERIT_SCHED : -1;
    rc = pthread_attr_setinheritsched(&t.attr.thread_attr, inherit);
    if (rc) {
        printf("pthread_attr_setinheritsched failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("inherit value is: [ %d ]\n\t INHERITSCHED macro numerical values are: (0, 1)\n", inherit);
        printf("\n\t Where 0 == PTHREAD_INHERIT_SCHED, 1 == PTHREAD_EXPLICIT_SCHED\n");
    }

    int policy = USTP == 0 ? SCHED_FIFO : USTP == 1 ? SCHED_RR : USTP == 2 ? SCHED_OTHER : -1;
    rc = pthread_attr_setschedpolicy(&t.attr.thread_attr, policy);
    if (rc) {
        printf("pthread_attr_setschedpolicy failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("policy value is: [ %d ]\n\t User space thread pool policy i.e USTP macro numerical values are: (0, 1, 2)\n", policy);
        printf("\n\t Where 0 == SCHED_FIFO, 1 == SCHED_RR, and 2 == SCHED_OTHER\n");
    }

    schedparam.sched_priority = USTP == 0 ? 1 : USTP == 1 ? 1 : USTP == 2 ? 0 : -1;
    rc = pthread_attr_setschedparam(&t.attr.thread_attr, &schedparam);
    if (rc) {
        printf("pthread_attr_setschedparam failed: %s (errno: %d)\n", strerror(rc), rc);

    }

    int detachstate = THREAD_STATE == 1 ? PTHREAD_CREATE_JOINABLE : THREAD_STATE == 0 ? PTHREAD_CREATE_DETACHED : -1;
    rc = pthread_attr_setdetachstate(&t.attr.thread_attr, detachstate);
    if (rc) {
        printf("pthread_attr_setdetachstate failed: %s (errno: %d)\n", strerror(rc), rc);
        printf("detachstate value is: [ %d ]\n\t THREAD_STATE Macro numerical values are: (0, 1)", detachstate);
        printf("\n\t Where 0 == PTHREAD_CREATE_DETACHED, and 1 == PTHREAD_CREATE_JOINABLE\n");
        // TODO: Try something else, like a different attribute or use semaphores 
    }

    t.flag = 0x0;
    return t;
}

FORCE_INLINE threads_t create_attrs(threads_t tp, const uint8_t mode) {
    int rc;
    const void* mutex_attr = &tp.attr.mutex_attr;
    const void* thread_attr = &tp.attr.thread_attr;
    
    if (thread_attr) {
        
        size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
        size_t base_size = PTHREAD_STACK_MIN * ASAN_STACK_MULTIPLIER;
        __ss             = (base_size + page_size - 1) & ~(page_size - 1);

        rc = pthread_attr_setstacksize(&tp.attr.thread_attr, __ss);
        if (rc) {
            printf("pthread_attr_setstacksize failed: %s (errno: %d)\n\t swapping to pthread attribute default settings\n", strerror(rc), rc);
            pthread_attr_destroy(&tp.attr.thread_attr);
        }

        // pthread_attr_setguardsize will be ignored, since pthread_attr_setstacksize is used in this scope
        // TODO: Swap malloc out with this and modify the flags: private_address(NULL, ss, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        tp.attr.stackaddr = malloc(__ss); 
        rc                 = pthread_attr_getstack(&tp.attr.thread_attr, tp.attr.stackaddr, &__ss);
        if (rc) {
            printf("ppthread_attr_getstack failed: %s (errno: %d)\n\t failed to get stack address\n", strerror(rc), rc);
            pthread_attr_destroy(&tp.attr.thread_attr);
        }
        else {
            rc = mprotect(tp.attr.stackaddr, __ss, PROT_NONE);
            if (rc == -1) {
                printf("mprotect failed: %s (errno: %d)\n\t failed to get stack address\n", strerror(rc), rc);
                // TODO: We then use semaphores instead
            }
        }
    }
    if (mode == 0x01 && mutex_attr) {
        rc = pthread_mutexattr_setpshared(&tp.attr.mutex_attr, PTHREAD_PROCESS_SHARED); 
        if (rc) {
            printf("pthread_mutexattr_setpshared failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            pthread_mutexattr_destroy(&tp.attr.mutex_attr);
        }

        int kind = MUTEX_ATTR == 0 ? PTHREAD_MUTEX_DEFAULT : MUTEX_ATTR == 1 ? PTHREAD_MUTEX_ERRORCHECK : MUTEX_ATTR == 2 ? PTHREAD_MUTEX_RECURSIVE : -1;
        rc = pthread_mutexattr_settype(&tp.attr.mutex_attr, kind);
        if (rc) {
            printf("pthread_mutexattr_settype failed: %s (errno: %d)\n\t swapping to mutex default settings\n", strerror(rc), rc);
            printf("kind value is: [ %d ]\n\t MUTEX_ATTR macro numerical values are: (0, 1, 2)\n", kind);
            printf("\n\t Where 0 == PTHREAD_MUTEX_DEFAULT, 1 == PTHREAD_MUTEX_ERRORCHECK, and 2 == PTHREAD_MUTEX_RECURSIVE\n");
            pthread_mutexattr_destroy(&tp.attr.mutex_attr);
            // TODO: Try other locks 
        }

    }
    else if (mode == 0x02) {
        // Aquire default settings 
        const void* attr = &tp.attr.thread_attr;
        if (attr) pthread_attr_destroy(&tp.attr.thread_attr);
        const void* mutex_attr = &tp.attr.mutex_attr;
        if (mutex_attr) pthread_mutexattr_destroy(&tp.attr.mutex_attr);
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
void create_thread(threads_t tp, const uint8_t mode, void* func) {

    tp = create_attrs(tp, mode);
    int rc = pthread_create(&tp.thread_id, &tp.attr.thread_attr, func, (void*)&tp.args);
    if (rc) {
        printf("pthread_create failed: %s (errno: %d)\n", strerror(rc), rc);
        // TODO: Could try creating a semaphore here instead.
        return;
    }

    return;
}


void join_thread(threads_t t, const void** rtn) {
    int state; 
    pthread_attr_getdetachstate(&t.attr.thread_attr, &state);
    if (state != PTHREAD_CREATE_DETACHED)
        pthread_join(t.thread_id, (void**)rtn); 
    return;
}

void clean_threads(threads_t t) {
    
        
    if (t.lock.type == 0x01) {

        pthread_mutexattr_destroy(&t.attr.mutex_attr);

    }
    if (t.attr.stackaddr != NULL) {

        if (__ss > 0) memset(t.attr.stackaddr, 0, __ss);
        if (t.attr.stackaddr) free(t.attr.stackaddr);
        pthread_attr_destroy(&t.attr.thread_attr);

    }
    if (t.args.arr != NULL) {

        // size_t size = sizeof(t->args.arr) / t->args.arr[0]; // get the length of the array
        free(t.args.arr);
        
    }

    return;
}


void debug_threads(const threads_t tp) {
    printf("Targeted thread address: [ %p ]\n", &tp);
    if (tp.lock.type == 0x01) {
        printf("\n============================================\n");
        printf("Lock Attributes: [ %p ]\n", &tp.attr.mutex_attr);
        int pshared;
        if (pthread_mutexattr_getpshared(&tp.attr.mutex_attr, &pshared) != 0) 
            printf("Error failed to get thread [ %p ] mutex attribute pshared state\n", &tp);
        
        if (pshared != PTHREAD_PROCESS_SHARED) 
            printf("Thread [ %p ] process shared was not enabled.\n", &tp.attr.mutex_attr);
        printf("shared process is not enabled, assuming default settings are being used\n");
        printf("\n============================================\n");
    }
        
        
    if (tp.attr.stackaddr != NULL) {
        printf("\n============================================\n");
        printf("Thread Attribute [ %p ]\n", &tp.attr.thread_attr);
        //size_t size = pthread_attr_getstack(thread_attr, void **__restrict stackaddr, STACK_SIZE);
        //printf("pthread's stack size is: [ %ud ]\n", size);
    }

    printf("\n============================================\n");
    //int *schedpolicy;
    //struct sched_param *schedparam;
    //int res = pthread_getschedparam(tp->thread_id,  schedpolicy,  schedparam);


    printf("\n============================================\n");
}
