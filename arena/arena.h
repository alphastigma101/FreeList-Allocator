#ifndef _ARENA_H_
#define _ARENA_H_
#include "../threads/threads.h"
#include "../bitmap/bitmap.h"

#define ARENA_SIZE 4096

typedef struct arena_t {
    struct              arena_t* next; 
    unsigned char*      chunk; 
    void*               res;
    unsigned int        curr;
    unsigned int        prev;
    unsigned char       flag;
} arena_t;

extern arena_t* init_arena_t();
extern uintptr_t alignment(uintptr_t ptr, size_t align);
extern arena_t* push(arena_t* arena, size_t bytes);
extern arena_t* pop(arena_t* arena, size_t offset);
extern void* resize(arena_t *arena, void *old_memory, size_t old_size, size_t new_size, size_t align);
extern void clear_arena_t(arena_t* arena);

#endif