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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include <errno.h>

void debug_threads(const threads_t* tp) {
    uint8_t flag = 0;
    if (tp) {
        // Check the attributes set for mutex
        if (tp->attr.mutex) {
            int* pshared = malloc(sizeof(int));
            if (!pshared) {
                printf("ERROR 21: IN DEBUG_TRHEADS: FAILED TO ALLOCATE MEMORY ON HEAP FOR PSHARED\n");
                return;
            }
            memset(pshared, 0, sizeof(int));
            *pshared = _SC_THREAD_PROCESS_SHARED;
            if (pthread_mutexattr_getpshared(tp->attr.mutex, pshared) != 0) {
                free(pshared);
                flag = flag << 2; // Shift it over twice: 000000010

                // Check to see if the default settings were set   
                if (pthread_mutexattr_gettype(tp->attr.mutex, NULL) != 0) {
                    // we do not know what kind of settings are set, so they are not supported
                } 
                else flag = (flag | 1) >> 2; // Should look like this 0000011
                if (flag == 0x11) printf("\nFunction debug_threads, source line 47: \n\tMutex has default settings set\n");
                else printf("\nFunction debug_threads, source line 48: \n\tMutex has unsupported settings set\n");
            }
        }
        if (tp->attr.thread) {
            flag = 0;
            // We need to check the threads attributes to see if they are default or not 
            // We then can check to see the size of the stack and get the stack address if needed 
            // It is all intertwined with the thread attributes
        }
    }
}

// Portablility for either creating a single thread or using threads_t i.e the threadpool
threads_t* create_thread(threads_t* tp, const uint8_t mode, const void* func) {
    if (tp) return create_thread_attr(tp, mode, func);
    return (void*)0;
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
void clean_address(void* addr) {
    if (addr == NULL || addr == MAP_FAILED) {
        fprintf(stderr, "clean_address: invalid address\n");
        return;
    }

    unsigned char* bytes = (unsigned char*)addr;
    size_t len = 0;
    while (bytes) {
        if (bytes) {

            bytes++;
            len++;
        }
    }
    
    if (munmap(addr, len) == -1) {
        fprintf(stderr, "clean_address: munmap failed: %s\n", strerror(errno));
    }
}

// Can also be used as this: threads_t* thread[0] = (threads_t*)set_thread_attr(thread[0], 0x01, the_subroutine);
// Meaning that init_threads_t is not needed, but it calls all of these functions on its own.
threads_t* create_thread_attr(threads_t* tp, const uint8_t mode, const void* func) {
    if (mode == 0x01) {

        tp->attr.mutex = aligned_alloc(alignof(pthread_mutexattr_t), sizeof(pthread_mutexattr_t));
        if (tp->attr.mutex) memset(tp->attr.mutex, 0, sizeof(pthread_mutexattr_t));
        else printf("ERROR 310: FAILED TO ALLOCATE MEMORY FOR MUTEX ATTRIBUTE\n");
        if (pthread_mutexattr_init(tp->attr.mutex) != 0) {
            printf("ERROR 38: FAILED TO INIT MUTEX ATTRIBUTE\n");
            free(tp->attr.mutex);
            free(tp);
            return (void*)0;
        }

        if (pthread_mutexattr_setpshared(tp->attr.mutex, PTHREAD_PROCESS_SHARED) != 0) {
            printf("ERROR 37: FAILED TO INITIALIZE MUTEX AND ATTRIBUTES\n");
            free(tp);
            return (void*)0;
        }

        tp->mutex = aligned_alloc(alignof(pthread_mutex_t), sizeof(pthread_mutex_t));
        if (tp->mutex) memset(tp->mutex, 0, sizeof(pthread_mutex_t));
        else printf("ERROR 325: FAILED TO ALLOCATE MEMORY FOR MUTEX\n");

        if (pthread_mutex_init(tp->mutex, tp->attr.mutex) != 0) {
            printf("ERROR 44: Failed to set the attribute for shared resources cleaning!\n");
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(tp->attr.mutex);
            free(tp);
            return (void*)0;
        }

        tp->attr.thread = aligned_alloc(alignof(pthread_attr_t), sizeof(pthread_attr_t));
        if (tp->attr.thread) memset(tp->attr.thread, 0, sizeof(pthread_attr_t));
        else printf("ERROR 325: FAILED TO ALLOCATE MEMORY FOR THREAD ATTRIBUTE\n");

        if (pthread_attr_init(tp->attr.thread) != 0) {
            printf("ERROR 103: FAILED TO INIT THREAD ATTRIBUTES\n");
            return (void*)0;
        }

        tp->id = aligned_alloc(alignof(pthread_t), sizeof(pthread_t));
        if (tp->id) memset(tp->id, 0, sizeof(pthread_t));
        if (pthread_create(tp->id, tp->attr.thread, func, tp->args) != 0) {
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(tp->attr.mutex);
            pthread_attr_destroy(tp->attr.thread);
            free(tp);
            return (void*)0;
        }
    }
    else if (mode == 0x02) {
        // Default settings. Meaning that we do not enable shared memory 
        // Have not been tested as of 3/3/26
        if (pthread_mutex_lock(tp->mutex) != 0) {
            printf("ERROR 359: Failed to lock mutex\n");
            pthread_mutex_destroy(tp->mutex);
            pthread_mutexattr_destroy(tp->attr.mutex);
            pthread_attr_destroy(tp->attr.thread);
            free(tp);  
            return (void*)0; 
        }
    }
    return tp;
}

void join_thread(pthread_t t, const void** rtn) {
    pthread_join(t, (void**)rtn);
}


void clean_threads(threads_t* t) {
    if (t) {
        if (t->mutex && t->attr.mutex) {
            pthread_mutex_destroy(t->mutex);
            pthread_mutexattr_destroy(t->attr.mutex);
        }
        if (t->attr.thread) {
            pthread_attr_destroy(t->attr.thread);
        }
        if (t->id) {
            uint8_t res = pthread_join(*t->id, NULL);
            if (!res) {
                printf("ERROR 103: THREAD WAS CALLED PREMATURELY\n");

            }
        }
        if (t->addr) clean_address(t->addr);
        free(t);
    }
}