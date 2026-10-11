#include "colosseum.h"
#include "../threads/threads.h"
#include "../logger/logger.h"
#include <stdalign.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#define __USE_GNU 1
#include <sys/mman.h>

typedef struct iterator_t {
    colosseum_t* colosseum_t_next;
    arena_t*     arena_t_next;
    int          arena_t_depth;
    int          colosseum_t_depth;
} iterator_t;

static iterator_t iterator = {0};

colosseum_t* iterator_t_colosseum_t_range_sequence(colosseum_t* head, const int range) {
    if (range <= 0) return head;
    else if (range == 1) {
        head->next = calloc(1, sizeof(colosseum_t));
        head->next->size = 2000000UL;
        return head;
    }
    colosseum_t* node = head->next;
    for (int i = 0; i < range; i++) {
        if (!node) {
            init_colosseum_t(node);
            if (!node) return head;
            node->size = 2000000UL;
            iterator.colosseum_t_depth++;
        }        
        if (node) node = node->next;
        else break;
    }
    return head;
}

arena_t* iterator_t_arena_t_range_sequence(arena_t* head, const int range) {
    if (range <= 0) return head;
    else if (range == 1) {
        head->next = calloc(1, sizeof(arena_t));
        head->next->curr = 1;
        head->next->size = ARENA_T_SIZE + 1UL;
        return head;
    }

    arena_t* node = head->next;
    for (int i = 0; i < range; i++) {
        if (!node) {
            init_arena_t(node);
            if (!node) return head;
            node->curr = 1;
            node->size = ARENA_T_SIZE + 1UL;
            iterator.arena_t_depth++;
        }
        
        if (node) node = node->next;
        else break;
    }
    return head;
}

colosseum_t* iterator_t_colosseum_t_iterate(colosseum_t* head, const int index, const void* address) {
    if (index == 0) return head;
    else if (index == 1) return head->next;
    
    bool in_bounds = false;
    if (address) {
       const uintptr_t block_size = head->size;
       const uintptr_t raw = (uintptr_t)address;
       const uintptr_t end = raw + block_size;
       const uintptr_t start = end - block_size + 1UL;
       colosseum_t* colosseum = (colosseum_t*)(start + offsetof(colosseum_t, next));
       if (colosseum) if (raw > start && raw < end) return colosseum;
       // It will fail iif the specific address belonging to the colosseum->chunk has been resized. const uintptr_t block_size = head->size;
    }

    __builtin_prefetch(head, 0, 3);
    for (int i = 0; i < iterator.colosseum_t_depth; i++) {
        __builtin_prefetch(head->next, 0, 3);
        colosseum_t* node = head->next + i;
        if (address && node) {
            const uintptr_t base = (uintptr_t)node->chunk;
            const uintptr_t end = (uintptr_t)(address + node->size);
            const uintptr_t block = (uintptr_t)address;
            in_bounds = (block > base) && (block < end); 
        }
        if (i == index || in_bounds) return node;
        else if (!node) break;
    }

    return nullptr;
}

FORCE_INLINE colosseum_t* iterator_t_colosseum_t_end(const colosseum_t* head) { return head->next + iterator.colosseum_t_depth; }

arena_t* iterator_t_arena_t_iterate(arena_t* head, const int index, const void* address) {
    if (index == 0) return head;
    else if (index == 1) return head->next;
    
    bool in_bounds = false;
    if (address) {
        const uintptr_t block_size = head->size;
        const uintptr_t raw = (uintptr_t)address;
        const uintptr_t end = raw + block_size;
        const uintptr_t start = end - block_size + 1UL;
        arena_t* arena = (arena_t*)(start - offsetof(arena_t, chunk));
        if (arena) if (raw > start && raw < end) return arena;
        // It will fail iif the specific address belonging to the arena->chunk has been resized.
    }

    __builtin_prefetch(head, 0, 1);
    for (int i = 0; i < iterator.arena_t_depth; i++) {
        arena_t* node = head->next + i;
        if (address && node) {
            const uintptr_t base = (uintptr_t)node->chunk;
            const uintptr_t end = (uintptr_t)(address + node->size);
            const uintptr_t block = (uintptr_t)address;
            in_bounds = (block > base) && (block < end); 
        }
        if (i == index || in_bounds) return node;
        else if (!node) break;
    }

    return nullptr;
}

FORCE_INLINE arena_t* iterator_t_arena_t_end(const arena_t* head) { return head->next + iterator.arena_t_depth; }

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
    else if (!arena) {
        const size_t size = sizeof(arena_t);
        thread_local static arena_t* node = nullptr;
        if (size < M_MMAP_THRESHOLD) {
            node = aligned_alloc(alignof(arena_t), sizeof(arena_t));
            if (!node) return;
            arena->next = node;
        }
        else {
            node = private_address(nullptr, size, PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
            if (node == MAP_FAILED) return;
            int res = madvise(node, size, MADV_MERGEABLE);
            if (res == -1) {
                if (node) munmap_address(node, size, __FILE__,  __LINE__);
                node = nullptr;
                return;
            }
            arena = node;
        }
    }
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
    arena->curr = 1;
    arena->size = ARENA_T_SIZE + 1UL;
}

inline void init_colosseum_t(colosseum_t* colosseum) {
    if (colosseum->chunk) return;
    else if (!colosseum) {
        const size_t size = sizeof(colosseum_t);
        thread_local static colosseum_t* node = nullptr;
        if (size < M_MMAP_THRESHOLD) {
            node = aligned_alloc(alignof(colosseum_t), sizeof(colosseum_t));
            if (!node) return;
        }
        else {
            node = private_address(nullptr, size, PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
            if (node == MAP_FAILED) return;
            int res = madvise(node, size, MADV_MERGEABLE);
            if (res == -1) {
                if (node) munmap_address(node, size, __FILE__,  __LINE__);
                node = nullptr;
                return;
            }
            colosseum = node;
        }
    }
    __builtin_prefetch(colosseum, 1, 3);
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
    arena->state = ARENA_T_IS_FULL;
    arena_t* node = nullptr;
    if (arena->next) {
        node = arena->next;
        arena_t* end = iterator_t_arena_t_end(arena);
        end->next = arena;   
    } else {
        node = iterator_t_arena_t_end(arena);
        init_arena_t(node);
        node->next = arena;
    }
    return node ? node : arena;
}


/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply pushes bytes to the arena, and assigns the data field 'res' with a memory address that will be used.
 * @param arena a pointer to a user defined type variable/object.
 * @param bytes A numerical value that represents the size of the object
 * @details if 'bytes' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
[[gnu::hot]]
[[gnu::nonnull(1)]]
FORCE_INLINE void arena_t_push(arena_t* arena, size_t bytes) {
    if (bytes == 0) return;
    else if (bytes > arena->size) return;
    
    __builtin_prefetch(arena, 0, 1);
    if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));
    uintptr_t raw = (uintptr_t)arena->chunk + (uintptr_t)(arena->curr == 0 ? 1 : arena->curr);
    uintptr_t offset = alignment(raw, bytes);
    offset -= (uintptr_t)arena->chunk;
    
    __builtin_prefetch(arena, 1, 1);
    if (bytes + offset <= arena->size) {
        arena->res = arena->chunk + offset;
        arena->curr = (unsigned int)offset + (unsigned int)bytes;
        arena->prev = (unsigned int)offset;
        arena->state = ARENA_T_IS_NOT_FULL;
        return;
    }

    arena->state = ARENA_T_IS_FULL;
    arena = arena_t_move(arena);
    iterator.arena_t_depth++;
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
    colosseum_t* node = nullptr;
    if (colosseum->next) {
        node = colosseum->next;
        colosseum_t* end = iterator_t_colosseum_t_end(colosseum);
        end->next = colosseum;   
    } else {
        node = iterator_t_colosseum_t_end(colosseum);
        init_colosseum_t(node);
        node->next = colosseum;
    }
    return node ? node : colosseum;
}

[[gnu::hot]]
[[gnu::nonnull(1)]]
void colosseum_t_push(colosseum_t* colosseum, size_t bytes) {
    if (bytes == 0 || bytes < colosseum->arena.size) return arena_t_push(&colosseum->arena, bytes);
    else if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));
    
    __builtin_prefetch(colosseum, 0, 3); 
    float amount_of_pages = bytes % colosseum->arena.size > 0 ? bytes % ARENA_T_SIZE : 1.0;
    const size_t total_amount_of_pages = colosseum->pages + amount_of_pages;
    if (total_amount_of_pages % colosseum->size != 0) {
        colosseum->pages += amount_of_pages;
        uintptr_t base = (uintptr_t)colosseum->chunk;
        // offset for colosseum is: offset = page index * 4096UL 
        uintptr_t raw = base + (uintptr_t)((colosseum->pages * ARENA_T_SIZE)); // TODO: We need to round up, so seek integer promotions
        uintptr_t raw_aligned = alignment(raw, bytes);
        raw_aligned -= (uintptr_t)colosseum->chunk;
        uintptr_t next = base + raw_aligned; 
        if (next > (base + colosseum->size)) { colosseum_t_resize(colosseum, bytes); return colosseum_t_push(colosseum, bytes); }
        else if (colosseum->state != COLOSSEUM_T_IS_NOT_FULL) colosseum->state = COLOSSEUM_T_IS_NOT_FULL;
        return;
    }

    colosseum->state = COLOSSEUM_T_IS_FULL;
    colosseum = colosseum_t_move(colosseum);
    iterator.colosseum_t_depth++;
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
    uintptr_t offset = (((uintptr_t)colosseum->pages) * ARENA_T_SIZE); // TODO: We need to make sure we round up. Seek promotion in cppreference
    uintptr_t ptr = base + offset;
    return ((ptr > base) && (ptr < base + size)) ? (void*)ptr : nullptr;
}


/** 
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply pops bytes from the arena, re-assigns data field's 'curr' and 'prev'.
 * @param arena a pointer to a user defined type variable/object.
 * @param offset A numerical value that represents the distance from arena to the object.   
*/
[[gnu::hot]]
[[gnu::nonnull(1)]]
FORCE_INLINE void arena_t_pop(arena_t* arena, size_t bytes) {
    __builtin_prefetch(arena, 0, 1);
    if ((arena->curr == 1) || (bytes == 0UL)) return;

    __builtin_prefetch(arena, 1, 1);
    if (arena->curr > 0) {
        if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));
        uintptr_t raw = ((uintptr_t)arena->res) - bytes; // Is missing the offset, so this should cause it be to misalgined 
        uintptr_t raw_aligned = alignment(raw, bytes); // we re-enforce the aligning here with the max
        void* result = (void*)raw_aligned;
        if (!result) {
            raw_aligned = alignment(raw, alignof(max_align_t));
            result = (void*)raw_aligned;
        }
        const uintptr_t end = ((uintptr_t)arena->chunk) + arena->size; 
        if (((uintptr_t)result >= end) || ((uintptr_t)result) <= ((uintptr_t)arena->chunk) + 1UL) {
            if (arena->curr < 1) arena->curr = 1;
            return;
        }
        arena->curr = (int)(((uintptr_t)arena->chunk) - raw_aligned); // we minus the arena->chunk from the aligned object to get the previous object 
        arena->prev = (int)(((uintptr_t)arena->chunk) - (uintptr_t)arena->res);
        arena->res = result;
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

/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply resizes arena's 'chunk' to the desired size, and will continue to do so until USHRT_MAX is hit.
 * @param arena a pointer to a user defined type variable/object.
 * @param new_size A numerical value that represents the size of the object
 * @note if 'new_size' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
[[gnu::nonnull(1)]]
FORCE_INLINE void arena_t_resize(arena_t *arena, size_t new_size) {
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


/** 
 * @brief A stand alone function that expects the parameter to not be NULL. It is a destructor for arena
 * @param arena A pointer to a user defined type variable/object.
 * @details This function should be called at the end of the program or if a catastrophic event occurs  
*/
[[gnu::nonnull(1)]]
FORCE_INLINE void dctor_arena_t(arena_t* arena) {
    if (!arena->chunk) return;
    const size_t threshold = M_MMAP_THRESHOLD;
    arena_t* node = arena;
    for(size_t i = 0; node != nullptr; i++) {
        __builtin_prefetch(node, 1, 1);
        const size_t size = node->size;
        if (size < threshold) { free(node->chunk); node->chunk = nullptr; }
        else { munmap_address(node->chunk, size, __FILE__, __LINE__); node->chunk = nullptr; }
        arena_t* next = node->next;
        if (i > 0) threshold < size ? free(node) : munmap_address(node, size, __FILE__, __LINE__);
        node = next;
    }
}

[[gnu::nonnull(1)]]
void dctor_colosseum_t(colosseum_t* colosseum) {
    const size_t threshold = M_MMAP_THRESHOLD;
    colosseum_t* node = colosseum;
    for(size_t i = 0; node != nullptr; i++) {
        __builtin_prefetch(node, 1, 3);
        const size_t size = node->size;
        if (size < threshold) { free(node->chunk); node->chunk = nullptr; }
        else { munmap_address(node->chunk, size, __FILE__, __LINE__); node->chunk = nullptr; }
        dctor_arena_t(&node->arena);
        colosseum_t* next = node->next;
        if (i > 0) threshold < size ? free(node) : munmap_address(node, size, __FILE__, __LINE__);
        node = next;
    }
}