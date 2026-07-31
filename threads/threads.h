#ifndef _THREADS_H
#define _THREADS_H
#include <stddef.h>
#define _GNU_SOURCE 1
#define __USE_UNIX98 1 
#define __USE_XOPEN2K 1
#include <pthread.h>
#include <sys/sem.h>
#include <stdatomic.h>
#include <unistd.h> 
#include <stdint.h>
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


typedef struct attr_t {
    pthread_attr_t                 thread_attr;
    void**                         stackaddr; 
    pthread_mutexattr_t            mutex_attr;
} attr_t;

typedef struct lock_t {
    pthread_mutex_t                mutex;
    unsigned char                  type;
} lock_t;

typedef struct threads_t {
    attr_t*                        attr; 
    lock_t*                        lock;
    pthread_t                      thread_id;
    struct function_t*             routine;
    unsigned char                  flag;                                    
} threads_t;

typedef struct semaphores_t {
    struct sembuf** semaphores;
    struct sembuf*  next;
    int (*semget)(key_t __key, int __nsems, int __semflg);  /* Create a standard or get a standard semaphore*/
    int  (*semop)(int __semid, struct sembuf *__sops, size_t __nsops); /* Define and create a semaphore with special flags */
    unsigned int bucket_count;
} semaphores_t;


/**
 * @brief Initializes a fresh threads_t instance with default values.
*/
extern threads_t init_threads_t(const unsigned char mode, const unsigned char locked);

/**
 * @brief Allocates and configures a contiguous block of threads forming a pool.
*/
extern void create_thread_pool(threads_t* tp, const unsigned int size, const unsigned char mode, const unsigned char locked, const unsigned char stack);

/**
 * @brief Dynamically resizes or reconfigures an existing thread pool.
*/
extern void update_thread_pool(threads_t *tp, const unsigned int size);

/**
 * @brief Spawns a single managed thread executing the target function. Requires tp->metadata to be initialized
*/
extern void create_thread(threads_t* tp, void* func);

/**
 * @brief Blocks the caller until the specified thread terminates, capturing its return value.
*/
extern void join_thread(threads_t tp, void** rtn);

/**
 * @brief Looks up a specific thread instance within a collection.
*/
extern threads_t find_thread_t(const threads_t* tp, const unsigned int size);

/**
 * @brief Registers or mutates internal runtime configuration and metadata for a thread context.
*/
extern void routine_metadata(threads_t* t, const int length, ...);

/**
 * @brief Extracts the raw argument vector packed within a function configuration structure.
*/
extern void** routine_metadata_arguments(struct function_t* meta);

extern size_t routine_metadata_size(struct function_t *meta);

/**
 * @brief Prints state diagnostics and metrics for the specified thread context.
*/
extern void debug_threads(const threads_t tp);

/**
 * @brief Releases heap memory and system resources bound to a thread context.
*/
extern void clean_threads(threads_t* t);


// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, unsigned char off);
extern void* remap_address(void* addr, size_t old_len, size_t new_len);
extern void munmap_address(void* addr, size_t len);

#endif