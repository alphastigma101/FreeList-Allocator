#include "arena.h"
#include <threads.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include <string.h>
#include <stdlib.h>

/*
#include "arena.h"
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mman.h>
#include <thread>
#include <threads.h>
*/

_Thread_local arena_t* arena = NULL;

/**
	@description: Align an address to the nearest power of two as long as align sizeof(prt) >= alignof(align) holds true.
		If align is sizeof(prt) < alignof(align), there is not enough space.
	@param: ptr: A pointer converted into unintptr_t to hold the memory address of void pointer 
	@param: align can be either a power of two or not. 
	@return: Return the proper alignment of the memory address  
*/
uintptr_t alignment(uintptr_t ptr, size_t align) {
    if (align == 0) return ptr;
    if ((align & (align - 1)) != 0) {
        uintptr_t modulo = ptr % align; /* seek the next alignment */
        if (modulo != 0) ptr += align - modulo; // if there is anything leftover, increment it
        return ptr;
    }
    uintptr_t modulo = ptr & (align - 1); // check to see if it is a power of two
    if (modulo != 0) ptr += align - modulo;
    return ptr;
}

FORCE_INLINE int_fast8_t po2(const size_t bytes) { return (bytes != 0) & ((bytes & (bytes - 1)) == 0); }

inline arena_t* init_arena_t() {
    arena_t* arena = NULL;
    arena = private_address(NULL, sizeof(arena_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) return NULL;
    int res = res = madvise(arena, sizeof(arena_t), MADV_MERGEABLE);
    if (res == -1) {
        if (arena) munmap_address(arena, sizeof(arena_t));
        return NULL;
    }
    memset(arena, 0, sizeof(arena_t));


    if (ENABLE_SHARED_MEMORY) arena->chunk = shared_address(NULL, ARENA_SIZE + 1, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS , -1, 0);
    else arena->chunk = private_address(NULL, ARENA_SIZE + 1, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS , -1, 0); 
    
    if (arena->chunk == MAP_FAILED) {
        munmap_address(arena, sizeof(arena_t));
        arena = NULL;
        return NULL;
    }
    else {
        int res = madvise(arena->chunk, ARENA_SIZE + 1, MADV_SEQUENTIAL);
        if (res == -1) {
            if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE + 1);
            arena->chunk = NULL;
            munmap_address(arena, sizeof(arena_t));
            arena = NULL;
            return NULL;
        }
        memset(arena->chunk, 0, ARENA_SIZE + 1);
    }

    arena->curr = 1;
    arena->size = ARENA_SIZE + 1;
    return arena;
}

inline arena_t* init_thread_ptr_arena_t(const unsigned char mode) {
    arena = mode == 0x0 ? shared_address(NULL, sizeof(arena_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : private_address(NULL, sizeof(arena_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) return NULL;
    int res = res = madvise(arena, sizeof(arena_t), MADV_MERGEABLE);
    if (res == -1) {
        if (arena) munmap_address(arena, sizeof(arena_t));
        return NULL;
    }
    memset(arena, 0, sizeof(arena_t));


    if (mode) arena->chunk = shared_address(NULL, ARENA_SIZE + 1, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS , -1, 0);
    else arena->chunk = private_address(NULL, ARENA_SIZE + 1, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS , -1, 0); 
    
    if (arena->chunk == MAP_FAILED) {
        munmap_address(arena, sizeof(arena_t));
        arena = NULL;
        return NULL;
    }
    else {
        int res = madvise(arena->chunk, ARENA_SIZE + 1, MADV_SEQUENTIAL);
        if (res == -1) {
            if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE + 1);
            arena->chunk = NULL;
            munmap_address(arena, sizeof(arena_t));
            arena = NULL;
            return NULL;
        }
        memset(arena->chunk, 0, ARENA_SIZE + 1);
    }

    arena->curr = 1;
    arena->size = ARENA_SIZE + 1;
    return arena;
}

inline arena_t* init_ptr_arena_t(arena_t* arena) {
    if (!arena) {
        arena = private_address(NULL, sizeof(arena_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena == MAP_FAILED) return NULL;
        int res = res = madvise(arena, sizeof(arena_t), MADV_MERGEABLE);
        if (res == -1) {
            if (arena) munmap_address(arena, sizeof(arena_t));
            return NULL;
        }
        memset(arena, 0, sizeof(arena_t));
    }
    if (ENABLE_SHARED_MEMORY) arena->chunk = shared_address(NULL, ARENA_SIZE + 1, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS , -1, 0);
    else arena->chunk = private_address(NULL, ARENA_SIZE + 1, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS , -1, 0); 
    
    if (arena->chunk == MAP_FAILED) {
        munmap_address(arena, sizeof(arena_t));
        arena = NULL;
        return NULL;
    }
    else {
        int res = madvise(arena->chunk, ARENA_SIZE + 1, MADV_SEQUENTIAL);
        if (res == -1) {
            if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE + 1);
            arena->chunk = NULL;
            munmap_address(arena, sizeof(arena_t));
            arena = NULL;
            return NULL;
        }
        memset(arena->chunk, 0, ARENA_SIZE + 1);
    }

    arena->curr = 1;
    arena->size = ARENA_SIZE + 1;
    return arena;
}


[[gnu::hot]]
arena_t* push(arena_t* arena, size_t bytes) {
    if (bytes == 0) return arena;
    else if (bytes > ARENA_SIZE) return NULL;

    uintptr_t raw = (uintptr_t)arena->chunk + (uintptr_t)(arena->curr == 0 ? 1 : arena->curr);
    uintptr_t offset = alignment(raw, bytes);
    offset -= (uintptr_t)arena->chunk;

    if (bytes + offset <= ARENA_SIZE) {
        arena->res = arena->chunk + offset;
        arena->curr = offset + bytes;
        arena->prev = offset;
        arena->flag = 0x0;
        return arena;
    }
    arena->flag = 0x01;
    return arena;
}

/** 
	* @description: FIFO implemtation for contigous arena. If both arena->curr == arena->prev, we reached the end of the stack
	* @param arena: This can be a stand alone arena, or the arena that is apart of the global variable called allocator. 
*/
[[gnu::hot]]
arena_t* pop(arena_t* arena, size_t offset) {
    if (!arena) return NULL;
    else if (arena->curr == 1) return arena;
    if ((size_t)arena->curr > offset) {
        arena->curr = arena->prev;
        arena->prev = (unsigned short)offset;
    }
	if (arena->flag == 0x01) arena->flag = 0x0;
    return arena;
}

/*void resize(arena_t *arena) {
    //if (arena->size > USHRT_MAX) return;
	return;
}*/

void clear_arena_t(arena_t *arena) {
    int res = madvise(arena->chunk, ARENA_SIZE + 1, MADV_DONTNEED);
    if (res == -1) return;
    else {
        arena->curr = 1;
        arena->prev = 0;
        arena->res = NULL;
    }
    return;
}