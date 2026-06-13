#pragma once
#include <sys/types.h>
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
#include "../logger/logger.h"


/* increase the space for ubsan/asan instrumentations */
#ifndef ASAN_STACK_MULTIPLIER
    #define ASAN_STACK_MULTIPLIER 16
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

#ifndef MODERN_ARCH
    #if __x86_64__ || __aarch64__
        #define MODERN_ARCH 1
    #else 
        #define MODERN_ARCH 0
    #endif 
#endif

#ifndef DEFAULT_ALIGNMENT
    #if MODERN_ARCH == 0
        #define DEFAULT_ALIGNMENT 64
    #else
        //#warning "Arch is most likely a embedded system, compiler will choose the best alignment"
        #define DEFAULT_ALIGNMENT 16
    #endif 
#endif

#define FORCE_COMPILER_ALIGNED(n) __attribute__((aligned(n)))
#define FORCE_PACK __attribute__((packed))
#define FORCE_INLINE __attribute__((always_inline)) static inline

typedef struct args_t {

    void**                     arr; 
    char*                      visit;
    size_t                     size;
    
} args_t;


typedef struct FORCE_PACK atomic_t {

    atomic_char                    ac;
    atomic_flag                    af;
    atomic_int                     ai;
    atomic_uintptr_t               aut; 
    

} atomic_t;


typedef struct attr_t {

    pthread_mutexattr_t            mutex_attr;
    uint8_t                        _pad[4];
    void**                         stackaddr; 
    pthread_attr_t                 thread_attr;


} attr_t;

typedef struct lock_t {

    pthread_spinlock_t             spin;
    uint8_t                        type;
    uint8_t                        _pad[4];
    pthread_mutex_t                mutex;

} lock_t;

typedef struct threads_t {

    uint8_t                        flag;
    uint8_t                        _pad[7];
    pthread_t                      thread_id; 
    atomic_t                       atomics;
    uint8_t                        __pad[2];                                     
    args_t                         args;
    lock_t                         lock;
    attr_t                         attr; 

} threads_t;

extern threads_t init_threads_t();
extern void create_thread(threads_t tp, const uint8_t mode, void* func);
extern void join_thread(threads_t tp, const void** rtn);
extern void* thread_arguments(void* args);
extern void debug_threads(const threads_t tp);
extern void clean_threads(threads_t t);

// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void munmap_address(void* addr, size_t len);

#endif