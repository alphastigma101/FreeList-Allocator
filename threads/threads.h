#pragma once
#ifndef _THREADS_H
#define _THREADS_H
#define _GNU_SOURCE 1
#define __USE_UNIX98 1 
#define __USE_XOPEN2K 1
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h> 
#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include "../debugging/debugging.h"

/* INHERITSCHED — Thread scheduling inheritance */
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


#define EMBEDDED_SYSTEMS 16
#define GAMING 64

#ifdef GAMING_ENABLED
    #define DEFAULT_ALIGNMENT GAMING
#else
    #define DEFAULT_ALIGNMENT EMBEDDED_SYSTEMS
#endif

typedef struct args_t {

    void**                     arr; 
    char*                      visit;
    size_t                     size;
    
} args_t;

typedef struct threads_t {

    uint8_t                        flag;
    uint8_t                        _pad[3];
    pthread_mutexattr_t            mutex_attr; 
    pthread_t                      thread_id;
    pthread_attr_t*                thread_attr;
    pthread_mutex_t*               mutex;
    atomic_uintptr_t               aut;                                     
    void**                         stackaddr;     
    args_t                         args;

} threads_t;

extern threads_t* init_threads_t();
extern threads_t* create_thread(threads_t* tp, const uint8_t mode, void* func);
extern void join_thread(threads_t* tp, const void** rtn);
extern void* thread_arguments(void* args);
extern void upid(threads_t* tp);
extern void debug_threads(const threads_t* tp);
extern void clean_threads(threads_t* t);

// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void munmap_address(void* addr, size_t len);

#endif