#ifndef _THREADS_H
#define _THREADS_H
#define _GNU_SOURCE 1
#define __USE_UNIX98 1 
#define __USE_XOPEN2K 1
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <unistd.h> 
#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
 

#define FORCE_COMPILER_ALIGNED(n) __attribute__((aligned(n)))
#define FORCE_PACK __attribute__((packed))
#define FORCE_INLINE __attribute__((always_inline)) static inline
#define FORCE_RUNTIME_ALIGNMENT(prval) \
    ({ uintptr_t __raw = (uintptr_t)prval;\
        uintptr_t __aligned = alignment(__raw, DEFAULT_ALIGNMENT);\
        (__typeof__(prval))__aligned;\
    })

#define EMBEDDED_SYSTEMS 16
#define GAMING 64

#ifdef GAMING_ENABLED
    #define DEFAULT_ALIGNMENT GAMING
#else
    #define DEFAULT_ALIGNMENT EMBEDDED_SYSTEMS
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Print Debugging Macros
// ─────────────────────────────────────────────────────────────────────────────

// Print a value with its variable name, file, and line number
#define DBG(fmt, val) \
    printf("[DBG] %s:%d — %s = " fmt "\n", __FILE__, __LINE__, #val, (val))

// Print a pointer address with its variable name
#define DBG_PTR(ptr) \
    printf("[DBG] %s:%d — %s = %p\n", __FILE__, __LINE__, #ptr, (void*)(ptr))

// Print a size_t or uintptr_t offset
#define DBG_OFFSET(val) \
    printf("[DBG] %s:%d — %s = %zu (0x%zx)\n", __FILE__, __LINE__, #val, (size_t)(val), (size_t)(val))

// Print a uint8_t flag value
#define DBG_FLAG(val) \
    printf("[DBG] %s:%d — %s = 0x%02x\n", __FILE__, __LINE__, #val, (uint8_t)(val))

// Print a labeled message with no value
#define DBG_MSG(msg) \
    printf("[DBG] %s:%d — " msg "\n", __FILE__, __LINE__)

// Print arena state
#define DBG_ARENA(arena) \
    printf("[DBG] %s:%d — arena { curr: %zu  prev: %zu  size: %zu  flag: 0x%02x  res: %p }\n", \
           __FILE__, __LINE__, \
           (arena)->curr, (arena)->prev, (arena)->size, (arena)->flag, (arena)->res)

// Print bucket state
#define DBG_BUCKET(b) \
    printf("[DBG] %s:%d — bucket { flag: 0x%02x  stack: %p  arena: %p }\n", \
           __FILE__, __LINE__, \
           (b)->flag, (void*)(b)->stack, (void*)(b)->arena)



typedef struct FORCE_COMPILER_ALIGNED(DEFAULT_ALIGNMENT) args_t {
    void**                     arr; 
    char*                      visit;
    size_t                     size;
} args_t;

typedef struct FORCE_COMPILER_ALIGNED(DEFAULT_ALIGNMENT) threads_t {
    uint8_t                 flag;
    uint8_t                 _pad[3];
    pthread_mutexattr_t     mutex_attr; 
    pthread_t               thread_id;
    pthread_attr_t*         thread_attr;
    pthread_mutex_t*        mutex;                                     
    void*                   addr;     
    args_t*                 args; 
} threads_t;

extern uintptr_t alignment(uintptr_t ptr, size_t align);
extern threads_t* init_threads_t();
extern args_t* init_args_t();
extern threads_t* create_thread(threads_t* tp, const uint8_t mode, const void* func);
extern void join_thread(pthread_t t, const void** rtn);
extern threads_t* create_thread_attr(threads_t* t, const uint8_t mode);
extern void* thread_arguments(args_t* args);
extern void debug_threads(const threads_t* tp);
extern void clean_threads(threads_t* t);

// mmap helpers
extern void* shared_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void* private_address(void *addr, size_t len, int prot, int flags, int fildes, uint8_t off);
extern void munmap_address(void* addr);

#endif