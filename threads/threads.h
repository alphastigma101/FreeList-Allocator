#ifndef _THREADS_H
#define _THREADS_H
#define _GNU_SOURCE 1
#define __USE_UNIX98 1 
#define __USE_XOPEN2K 1
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <unistd.h> 
#include <stdint.h>
#include <sys/mman.h>
 
#define PACKED_ALIGNED(n) __attribute__((packed, aligned(n)))

#define EMBEDDED_SYSTEMS 16
#define GAMING 64

#ifdef GAMING_ENABLED
    #define DEFAULT_ALIGNMENT GAMING
#else
    #define DEFAULT_ALIGNMENT EMBEDDED_SYSTEMS
#endif


typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ args_t {
    void** arr; 
    size_t size;
} args_t;

typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ threads_t {
    pthread_t*  id;
    void* addr;
    args_t* args;
    pthread_mutex_t* mutex;
    struct {
        pthread_mutexattr_t* mutex; 
        pthread_attr_t* thread; 
    } attr; 
    uint8_t flag;
} threads_t;

extern threads_t* init_threads_t();
extern args_t* init_args_t();
extern threads_t* create_thread(threads_t* tp, const uint8_t mode, const void* func);
extern void join_thread(pthread_t t, const void** rtn);
extern threads_t* create_thread_attr(threads_t* t, const uint8_t mode, const void* func);
extern void* thread_arguments(args_t* args);
extern void debug_threads(const threads_t* tp);
extern void clean_threads(threads_t* t);

// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void clean_address(void* addr);

#endif