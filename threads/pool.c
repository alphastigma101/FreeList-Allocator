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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
    threads_t* t = private_address(NULL, sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!t) return NULL;

    if (pthread_mutexattr_init(&t->mutex_attr) != 0) {
        printf("ERROR 38: FAILED TO INIT MUTEX ATTRIBUTE\n");
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    pthread_mutex_t mutex = {0};
    if (pthread_mutex_init(&mutex, &t->mutex_attr) != 0) {
        printf("ERROR 44: Failed to set the attribute for shared resources cleaning!\n");
        pthread_mutex_destroy(t->mutex);
        pthread_mutexattr_destroy(&t->mutex_attr);
        munmap_address(t, sizeof(threads_t));
        return NULL;
    } else t->mutex = &mutex;

    pthread_attr_t thread_attr = {0};
    if (pthread_attr_init(&thread_attr) != 0) {
        munmap_address(t, sizeof(threads_t));
        printf("ERROR 103: FAILED TO INIT THREAD ATTRIBUTES\n");
        return NULL;
    } else t->thread_attr = &thread_attr;

    // Configure the schedular policy here 
    schedparam.sched_priority = SCHED_PRIORITY;
    if (pthread_attr_setinheritsched(t->thread_attr, PTHREAD_EXPLICIT_SCHED) != 0) {
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    if (pthread_attr_setschedpolicy(t->thread_attr, USTP) != 0) {
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }
    
    if (pthread_attr_setschedparam(t->thread_attr, &schedparam) != 0) {
        munmap_address(t, sizeof(threads_t));
        return NULL;
    }

    t->flag = 0x0;
    return t;
}

FORCE_INLINE threads_t* create_attrs(threads_t* tp, const uint8_t mode) {
    if (mode == 0x01) {
        // Configure the mutex attributes 
        if (pthread_mutexattr_setpshared(&tp->mutex_attr, PTHREAD_PROCESS_SHARED) != 0) {
            printf("ERROR 37: FAILED TO INITIALIZE MUTEX AND ATTRIBUTES\n");
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(&tp->mutex_attr);
            munmap_address(tp, sizeof(threads_t));
            return NULL;
        }

        int pshared = 0;
        if (pthread_mutexattr_getpshared(&tp->mutex_attr, &pshared) != 0) {
            printf("ERROR 38: FAILED TO GET MUTEX PSHARED ATTRIBUTE\n");
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(&tp->mutex_attr);
            munmap_address(tp, sizeof(threads_t));
            return NULL;
        }

        if (pshared != PTHREAD_PROCESS_SHARED) {
            printf("ERROR 39: MUTEX PSHARED NOT SET CORRECTLY\n");
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(&tp->mutex_attr);
            munmap_address(tp, sizeof(threads_t));
            return NULL;
        }

        // Configure the pthread_attributes here .... More of a fine tune 

        // Fine tune the stack size for thread.
        // a stack is allocated dynamically on the heap for the thread, this includes the threads attributes
        
        //if (pthread_attr_setstack(tp->thread_attr) != 0) {

        //}
        //if (pthread_attr_setguardsize(tp->thread_attr, 0) != 0) {

        //}
    }
    else if (mode == 0x02) {
        // Default settings. Meaning that we do not enable shared memory 
        // Have not been tested as of 3/3/26
    }
    return tp;

}

// Portablility for either creating a single thread or using threads_t i.e the threadpool
threads_t* create_thread(threads_t* tp, const uint8_t mode, void* func) {
    if (tp) {
        tp = create_attrs(tp, mode);
        int rc = pthread_create(&tp->thread_id, tp->thread_attr, func, &tp->args);
        if (rc) {
            printf("pthread_create failed: %s (errno: %d)\n", strerror(rc), rc);
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(&tp->mutex_attr);
            pthread_attr_destroy(tp->thread_attr);
            munmap_address(tp, sizeof(threads_t));
            return NULL;
        }
        if (mode == 0x02) {
            if (pthread_mutex_lock(tp->mutex) != 0) {
                printf("ERROR 359: Failed to lock mutex\n");
                pthread_mutex_destroy(tp->mutex);
                pthread_mutexattr_destroy(&tp->mutex_attr);
                pthread_attr_destroy(tp->thread_attr);
                munmap_address(tp, sizeof(threads_t));
                return NULL; 
            }
        }
    }

    return tp;
}

void join_thread(pthread_t t, const void** rtn) { pthread_join(t, (void**)rtn); }

void clean_threads(threads_t* t) {
    if (t) {
        pthread_mutex_destroy(t->mutex);
        pthread_mutexattr_destroy(&t->mutex_attr);
        pthread_attr_destroy(t->thread_attr);
        if (t->thread_id) join_thread(t->thread_id, NULL);

        const int res = msync(t, sizeof(threads_t), MS_SYNC);
        if (res == ENOMEM) free(t);
        else munmap_address(t, sizeof(threads_t));
        memset(t, 0, sizeof(threads_t));
    }
}

void debug_threads(const threads_t* tp) {
    uint8_t flag = 0;
    if (tp) {
        // Check the attributes set for mutex
        const void* mutex = &tp->mutex_attr;
        if (mutex) {
            int pshared = 0;
            pshared = _SC_THREAD_PROCESS_SHARED;
            if (pthread_mutexattr_getpshared(&tp->mutex_attr, &pshared) != 0) {
                flag = flag << 2; // Shift it over twice: 000000010

                // Check to see if the default settings were set   
                if (pthread_mutexattr_gettype(&tp->mutex_attr, NULL) != 0) {
                    // we do not know what kind of settings are set, so they are not supported
                } 
                else flag = (flag | 1) >> 2; // Should look like this 0000011
                if (flag == 0x11) printf("\nFunction debug_threads, source line 47: \n\tMutex has default settings set\n");
                else printf("\nFunction debug_threads, source line 48: \n\tMutex has unsupported settings set\n");
            }
        }
        const void* thread_attr = &tp->thread_attr;
        if (thread_attr) {
            flag = 0;
            // We need to check the threads attributes to see if they are default or not 
            // We then can check to see the size of the stack and get the stack address if needed 
            // It is all intertwined with the thread attributes
        }
    }
}
