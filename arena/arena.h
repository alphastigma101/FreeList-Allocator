#ifndef _ARENA_H_
#define _ARENA_H_
#include "../threads/threads.h"
#include <stddef.h>

typedef struct /*PACKED_ALIGNED(DEFAULT_ALIGNMENT)*/ arena_t {
    struct arena_t* next;  
    uint8_t* chunk; 
    size_t size;
    size_t curr;
    size_t prev;
    void* res;
    uint8_t flag;
} arena_t;

extern arena_t* init_arena_t();
extern arena_t* push(arena_t* arena, size_t bytes);
extern arena_t* pop(arena_t* arena, size_t offset);
extern void* resize(arena_t *arena, void *old_memory, size_t old_size, size_t new_size, size_t align);
extern void clear_arena_t(arena_t* arena);
extern uintptr_t alignment(uintptr_t ptr, size_t align);
#endif