#include "arena.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
        uintptr_t modulo = ptr % align;
        if (modulo != 0) ptr += align - modulo;
        return ptr;
    }
    uintptr_t modulo = ptr & (align - 1);
    if (modulo != 0) ptr += align - modulo;
    return ptr;
}

void clear_arena_t(arena_t *arena) {
    arena->curr = 0;
    arena->prev = 0;
}

arena_t* push(arena_t* arena, size_t bytes) {
    if (bytes == 0) return arena;

    uintptr_t raw = (uintptr_t)arena->chunk + (uintptr_t)(arena->curr == 0 ? 1 : arena->curr);
    uintptr_t offset = alignment(raw, bytes);
    offset -= (uintptr_t)arena->chunk;

    if (bytes + offset <= arena->size) {
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
arena_t* pop(arena_t* arena, size_t offset) {
    if (!arena) return NULL;
    else if (arena->curr == 1) return arena;
    size_t distance = arena->curr - offset;
    arena->curr -= offset;
    arena->prev -= offset;
	if (arena->flag == 0x01) arena->flag = 0x0;

    return arena;
}

void* resize(arena_t *arena, void *old_memory, size_t old_size, size_t new_size, size_t align) {
	uintptr_t old_mem = (uintptr_t)old_memory;

	if (!(align & (align - 1))) return (void*)0;

	if (old_mem == NULL || old_size == 0) {
        arena = push(arena, new_size);
        void* resized = arena->res;
		return resized;
	} 
	else if ((uintptr_t)arena->chunk <= old_mem && old_mem < (uintptr_t)arena->chunk + arena->size) {
		if ((uintptr_t)arena->chunk + arena->prev == (uintptr_t)old_mem) {
			arena->curr = arena->prev + new_size;
			if (new_size > old_size) {
				// Zero the new memory by default
				memset(arena->chunk, 0, new_size - old_size);
			}
			return old_memory;
		} 
		else {
            arena = push(arena, new_size);
			void *new_memory = arena->res;
			size_t copy = old_size < new_size ? old_size : new_size;
			memmove(new_memory, old_memory, copy);
			return new_memory;
		}
	}

	return (void*)0;
}