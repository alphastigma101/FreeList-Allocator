#ifndef _DEBUGGING_H_
#define _DEBUGGING_H_

#define FORCE_COMPILER_ALIGNED(n) __attribute__((aligned(n)))
#define FORCE_PACK __attribute__((packed))
#define FORCE_INLINE __attribute__((always_inline)) static inline
// {
// "": [],
//}
typedef struct logger_t {
    int                depth; // use with offset and pointer math
    void               (*add)(int line, const char* file, const char* desc);
    void               (*clean)(char* key); // key is date and time
    void               (*write)(); // This will iterate through keys and entries ad add the {} at the correct spots
    struct             entries_t* (*find)(const char** key);
    char**              keys; // This part is this part: ""
    struct entries_t* entries; // This part is this part: []

} logger_t;

extern logger_t logger;

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

#endif 