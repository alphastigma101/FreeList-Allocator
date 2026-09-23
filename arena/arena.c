#include "arena.h"
#include "../threads/threads.h"
#include "../logger/logger.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#define __USE_GNU 1
#include <sys/mman.h>


[[gnu::hot]]
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

inline void init_arena_t(arena_t* arena) {
    if (arena->chunk) return;
    #if ARENA_T_MODE == 0
        if (ARENA_T_SIZE < M_MMAP_THRESHOLD && !arena->chunk) arena->chunk = calloc(ARENA_T_SIZE, 1);
        else if (ARENA_T_SIZE > M_MMAP_THRESHOLD && !arena->chunk) {
            arena->chunk = private_address(nullptr, ARENA_T_SIZE + 1, PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
            if (arena->chunk == MAP_FAILED) return;
            int res = madvise(arena->chunk, ARENA_T_SIZE + 1, MADV_MERGEABLE);
            if (res == -1) {
                if (arena->chunk) munmap_address(arena->chunk, ARENA_T_SIZE + 1, __FILE__,  __LINE__);
                arena->chunk = nullptr;
                return;
            }
            res = madvise(arena->chunk, ARENA_T_SIZE + 1, MADV_SEQUENTIAL);
            if (res == -1) {
                if (arena->chunk) munmap_address(arena->chunk, ARENA_T_SIZE + 1, __FILE__,  __LINE__);
                arena->chunk = nullptr;
                return;
            }
        }
    #else 
        arena->chunk = shared_address(nullptr, ARENA_T_SIZE + 1, PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
        if (arena->chunk == MAP_FAILED) {
            munmap_address(arena->chunk, ARENA_T_SIZE + 1, __FILE__,  __LINE__);
            arena->chunk = nullptr;
            return;
        }
        else {
            int res = madvise(arena->chunk, ARENA_T_SIZE + 1, MADV_MERGEABLE);
            if (res == -1) {
                if (arena->chunk) munmap_address(arena->chunk, ARENA_T_SIZE + 1, __FILE__,  __LINE__);
                arena->chunk = nullptr;
                return;
            }
            res = madvise(arena->chunk, ARENA_T_SIZE + 1, MADV_SEQUENTIAL);
            if (res == -1) {
                if (arena->chunk) munmap_address(arena->chunk, ARENA_T_SIZE + 1, __FILE__,  __LINE__);
                arena->chunk = nullptr;
                return;
            }
        }
    #endif
    if (!arena->next) {
        const size_t size = sizeof(arena_t);
        thread_local static arena_t* next = nullptr;
        if (size < M_MMAP_THRESHOLD) {
            next = aligned_alloc(alignof(arena_t), sizeof(arena_t));
            if (!next) return;
            arena->next = next;
        }
        else {
            next = private_address(nullptr, size, PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
            if (next == MAP_FAILED) return;
            int res = madvise(next, size, MADV_MERGEABLE);
            if (res == -1) {
                if (next) munmap_address(next, size, __FILE__,  __LINE__);
                next = nullptr;
                return;
            }
            arena->next = next;
        }
    }
    arena->curr = 1;
    arena->size = 4096UL + 1UL;
}

inline void init_colosseum_t(colosseum_t* colosseum) {
    if (colosseum->chunk) return;
    
    __builtin_prefetch(colosseum, 0, 3);
    if (!colosseum) {
        // TODO: If it is nullptr, that means we need to use a builtin extension to force what happens here to the parameter.
        // I know for a fact if allocating colosseum right now, after it is done, it will become a dangling pointer, so we need 
        // to make sure we can use a built in extension or use memcpy to actually copy the data to the instructions properly.
        colosseum = aligned_alloc(alignof(colosseum_t), sizeof(colosseum_t));
        if (!colosseum) return;
        memset(colosseum, 0, sizeof(colosseum_t));
    }
    colosseum->size = 2000000UL;
    colosseum->chunk = shared_address(nullptr, colosseum->size, PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
    if (colosseum->chunk != MAP_FAILED) {
        int res = madvise(colosseum->chunk, colosseum->size, MADV_HUGEPAGE);
        if (res == -1) {
            if (colosseum->chunk) munmap_address(colosseum->chunk, colosseum->size, __FILE__, __LINE__);
            colosseum->chunk = nullptr;
            return;
        }
    }
    colosseum->state = COLOSSEUM_T_IS_NOT_FULL;
}

/**
 * @brief A stand alone function apart of the arena's design pattern that expects the parameter to not be NULL to which it allocates certain memory for certain fields.
 * @param arena A pointer to a user defined type variable/object.
 * @note This is a traditional arena, so it will be following the stack data structure pattern and FIFO. 
 *       An arena can be also known as a bump allocator or slab allocator      
*/
FORCE_INLINE arena_t* arena_t_move(arena_t* arena) {
    arena->state = COLOSSEUM_T_IS_FULL;
    arena_t* new_arena = nullptr;
    init_arena_t(new_arena);
    if (!new_arena) return new_arena;
    new_arena->next = arena;
    return new_arena;
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void arena_t_push(arena_t* arena, size_t bytes) {
    if (bytes == 0) return;
    else if (bytes > arena->size) return;
    
    __builtin_prefetch(arena, 0, 1);
    if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));
    uintptr_t raw = (uintptr_t)arena->chunk + (uintptr_t)(arena->curr == 0 ? 1 : arena->curr);
    uintptr_t offset = alignment(raw, bytes);
    offset -= (uintptr_t)arena->chunk;

    if (bytes + offset <= arena->size) {
        arena->res = arena->chunk + offset;
        arena->curr = (unsigned int)offset + (unsigned int)bytes;
        arena->prev = (unsigned int)offset;
        arena->state = ARENA_T_IS_NOT_FULL;
        return;
    }

    arena->state = ARENA_T_IS_FULL;
    arena = arena_t_move(arena);
    if (arena->state == ARENA_T_IS_FULL) return;
    else return arena_t_push(arena, bytes);

}

/**
 * @brief A stand alone function apart of the colloseum's design pattern that expects the parameter to not be NULL to which it allocates certain memory for certain fields.
 * @param colloseum A pointer to a user defined type variable/object.
 * @param bytes a variable that should be the last allocated
 * @note This is a custom design pattern, it will only work iif collosseum is full, 
 *       or passing in the last requested bytes from within a 'block-scope' and return it.
*/
FORCE_INLINE colosseum_t* colosseum_t_move(colosseum_t* colosseum) {
    colosseum->state = COLOSSEUM_T_IS_FULL;
    colosseum_t* new_colosseum = nullptr;
    init_colosseum_t(new_colosseum);
    if (!new_colosseum) return colosseum;
    new_colosseum->next = colosseum;
    return new_colosseum;
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void colosseum_t_push(colosseum_t* colosseum, size_t bytes) {
    if (bytes == 0 || bytes < 4096UL) return arena_t_push(&colosseum->arena, bytes);
    else if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));
    
    __builtin_prefetch(colosseum, 0, 3); 
    float amount_of_pages = bytes % 4096UL > 0 ? bytes % 4096UL : 1.0;
    const size_t total_amount_of_pages = colosseum->pages + amount_of_pages;
    if (total_amount_of_pages % colosseum->size != 0) {
        colosseum->pages += amount_of_pages;
        uintptr_t base = (uintptr_t)colosseum->chunk;
        // offset for colosseum is: offset = page index * 4096UL 
        uintptr_t raw_object = base + (uintptr_t)((colosseum->pages * 4096UL)); // TODO: We need to round up, so seek integer promotions
        uintptr_t raw_object_aligned = alignment(raw_object, bytes);
        raw_object_aligned -= (uintptr_t)colosseum->chunk;
        uintptr_t next = base + raw_object_aligned; 
        if (next > ~base) { colosseum_t_resize(colosseum, bytes); return colosseum_t_push(colosseum, bytes); }
        else if (colosseum->state != COLOSSEUM_T_IS_NOT_FULL) colosseum->state = COLOSSEUM_T_IS_NOT_FULL;
        return;
    }

    colosseum->state = COLOSSEUM_T_IS_FULL;
    colosseum = colosseum_t_move(colosseum);
    if (colosseum->state == COLOSSEUM_T_IS_FULL) return;
    else return colosseum_t_push(colosseum, bytes);
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void* arena_t_get_current_position(arena_t *arena) {
    uintptr_t base = (uintptr_t)arena->chunk;
    uintptr_t size = (uintptr_t)arena->size;
    uintptr_t offset = (uintptr_t)arena->curr;
    uintptr_t ptr = base + offset;
    return ((ptr > base) && (ptr < base + size)) ? (void*)ptr : nullptr;
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void* colosseum_t_get_current_position(colosseum_t *colosseum) {
    __builtin_prefetch(colosseum, 0, 3);
    uintptr_t base = (uintptr_t)colosseum->chunk;
    uintptr_t size = (uintptr_t)colosseum->chunk;
    uintptr_t offset = (((uintptr_t)colosseum->pages) * colosseum->arena.size); // TODO: We need to make sure we round up. Seek promotion in cppreference
    uintptr_t ptr = base + offset;
    return ((ptr > base) && (ptr < base + size)) ? (void*)ptr : nullptr;
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void arena_t_pop(arena_t* arena, size_t offset) {
    __builtin_prefetch(arena, 0, 1);
    if (arena->curr == 1) return;
    // TODO: if we have the size and bytes, we can do maths to verify that offset parameter is actually an offset
    __builtin_prefetch(arena, 1, 1);
    if (arena->curr > offset) {
        arena->curr = arena->prev;
        arena->prev = offset;
    } else if (arena->state == ARENA_T_IS_FULL) arena->state = ARENA_T_IS_NOT_FULL;
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void colosseum_t_pop(colosseum_t* colosseum, size_t bytes) {
    __builtin_prefetch(colosseum, 0, 3);
    if (colosseum->total_bytes == 0 || bytes <= colosseum->arena.size) return arena_t_pop(&colosseum->arena, bytes);
    else if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));
    
    __builtin_prefetch(colosseum, 1, 3);
    if (colosseum->total_bytes >= bytes) {
        float amount_of_pages = bytes % colosseum->arena.size;
        if ((colosseum->pages - amount_of_pages < 0) || (colosseum->total_bytes - bytes < 0)) return;
        colosseum->pages -= amount_of_pages;
        colosseum->total_bytes -= bytes;
    }
    if (colosseum->state == COLOSSEUM_T_IS_FULL) colosseum->state = COLOSSEUM_T_IS_NOT_FULL;
}


[[gnu::nonnull(1)]]
void arena_t_resize(arena_t *arena, size_t new_size) {
    if (new_size > USHRT_MAX || new_size < arena->size) return;
    else if ((new_size != 0) & ((new_size & (new_size - 1)) != 0)) new_size = alignment(new_size, alignof(max_align_t));
    const size_t threshold = M_MMAP_THRESHOLD; 
    if (new_size < threshold) {
        arena->chunk = realloc(arena->chunk, new_size);
        if (!arena->chunk) return;
        arena->size = new_size;
    }
    else {
        remap_address(arena->chunk, arena->size, new_size);
        arena->size = new_size;
    }
    if (arena->state == ARENA_T_IS_FULL) arena->state = ARENA_T_IS_NOT_FULL;
}

[[gnu::nonnull(1)]]
void colosseum_t_resize(colosseum_t *colosseum, size_t new_size) {
    __builtin_prefetch(colosseum, 1, 3); // if you want to increase the size of arena, new_size must be <= ARENA_T_SIZE 
    if (new_size <= colosseum->arena.size) return arena_t_resize(&colosseum->arena, new_size);
    else if (new_size < colosseum->size) new_size = colosseum->size * 2UL;

    if ((new_size != 0) & ((new_size & (new_size - 1)) != 0)) new_size = alignment(new_size, alignof(max_align_t));
    remap_address(colosseum->chunk, colosseum->size, new_size); 
    if (colosseum->state == COLOSSEUM_T_IS_FULL) colosseum->state = COLOSSEUM_T_IS_NOT_FULL;
    colosseum->size = new_size;
}

[[gnu::nonnull(1)]]
void arena_t_reset(arena_t *arena) {
    int res = madvise(arena->chunk, arena->size, MADV_DONTNEED);
    if (res == -1) return;
    else {
        arena->curr = 1;
        arena->prev = 0;
        arena->res = nullptr;
    }
    return;
}

[[gnu::nonnull(1)]]
void colosseum_t_reset(colosseum_t *colosseum) {
    __builtin_prefetch(colosseum, 1, 3);
    int res = madvise(colosseum->chunk, colosseum->size, MADV_DONTNEED);
    if (res == -1) return;
    else {
        colosseum->total_bytes = 0UL;
        colosseum->pages = 0.0;
        colosseum->res = nullptr;
    }
    return;
}


[[gnu::nonnull(1)]]
void dctor_arena(arena_t* arena) {
    if (!arena->chunk) return;
    const size_t threshold = M_MMAP_THRESHOLD;
    arena_t* node = arena;
    for(size_t i = 0; node != nullptr; i++) {
        __builtin_prefetch(node, 0, 1);
        const size_t size = node->size;
        if (size < threshold) { free(node->chunk); node->chunk = nullptr; }
        else { munmap_address(arena->chunk, size, __FILE__, __LINE__); node->chunk = nullptr; }
        if (i > 0) free(node);
        node = node->next;
    }
}

[[gnu::nonnull(1)]]
void dctor_colosseum_t(colosseum_t* colosseum) {
    if (!colosseum->chunk) return;
    const size_t threshold = M_MMAP_THRESHOLD;
    arena_t* node = arena;
    for(size_t i = 0; node != nullptr; i++) {
        __builtin_prefetch(node, 0, 1);
        const size_t size = node->size;
        if (size < threshold) { free(node->chunk); node->chunk = nullptr; }
        else { munmap_address(arena->chunk, size, __FILE__, __LINE__); node->chunk = nullptr; }
        if (i > 0) free(node);
        node = node->next;
    }
}