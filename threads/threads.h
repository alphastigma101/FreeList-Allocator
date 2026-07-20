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
    unsigned int               size;
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
    unsigned char                  type;
    unsigned char                  _pad[4];
    pthread_mutex_t                mutex;
} lock_t;

typedef struct threads_t {
    unsigned char                  flag;
    unsigned char                  _pad[7];
    pthread_t                      thread_id;
    struct function_t*             routine; 
    unsigned char                  __pad[2];                                     
    args_t                         args;
    lock_t                         lock;
    attr_t                         attr; 
} threads_t;

extern threads_t routine_metadata(const unsigned char mode, threads_t t, const int length, ...);
extern threads_t init_threads_t();
extern void create_thread(threads_t* tp, const unsigned char mode, void* func);
extern void join_thread(threads_t tp, const void** rtn);
extern threads_t find_thread(threads_t tp);
extern threads_t* create_thread_pool(const unsigned int size);
extern void debug_threads(const threads_t tp);
extern void clean_threads(threads_t t);

// mmap helpers
extern void* shared_address(void *addr, unsigned int len, int prot, int flags, int fildes, unsigned char off);
extern void* private_address(void *addr, unsigned int len, int prot, int flags, int fildes, unsigned char off);
extern void* remap_address(void* addr, unsigned int old_len, unsigned int new_len);
extern void munmap_address(void* addr, unsigned int len);

#endif