#ifndef DEBUGGING_H


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