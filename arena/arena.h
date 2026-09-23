#ifndef _ARENA_H_
#define _ARENA_H_
#include <stddef.h>
#include <stdint.h>

#define ARENA_T_SIZE 4096U
#ifdef ARENA_T_MODE
    #ifdef THREADS_T_MODE
        #if (THREADS_T_MODE == 0) && (ARENA_T_MODE == 1)
            #undef ARENA_T_MODE
            #define ARENA_T_MODE 0
        #elif (THREADS_T_MODE == 1) && (ARENA_T_MODE == 0)
            #undef ARENA_T_MODE 
            #define ARENA_T_MODE 1
        #endif 
    #endif
    #if (ARENA_T_MODE != 0) || (ARENA_T_MODE != 1)
        #undef ARENA_T_MODE 
        #define ARENA_T_MODE 1
    #endif 
#endif 

enum FREELIST_ALLOCATOR_STATES {
    ARENA_T_IS_FULL,
    ARENA_T_IS_NOT_FULL,
    COLOSSEUM_T_IS_FULL,
    COLOSSEUM_T_IS_NOT_FULL 
};
typedef struct arena_t {
    struct arena_t*                next; 
    unsigned char*                 chunk; 
    void*                          res;
    size_t                         size;
    unsigned int                   curr;
    unsigned int                   prev;
    enum FREELIST_ALLOCATOR_STATES state;
} arena_t;

/**
 * @brief A colosseum is a data structure pattern created by Logan Welty, who was inspired by the arena data structure pattern, and its flaws.
 *        It can work by itself or with a memory confinedment arena. By default, it uses FIFO and requires a memory confinedment arena.
 *        It permits/handles requests byte requests that are not normal using transparent huge pages, and allows the arena to handle
 *        byte requests that fit into small cachelines for optimal performance.  
*/
typedef struct __attribute__((aligned(64))) colosseum_t {
    arena_t                        arena;
    unsigned char*                 chunk;
    void*                          res;
    size_t                         size; // default 1MB
    size_t                         total_bytes;
    struct colosseum_t*            next;
    float                          pages; // only allow up to 256 pages which equals 1MB
    unsigned char                  __pad[32];
    enum FREELIST_ALLOCATOR_STATES state;
} colosseum_t;

/**
 * @brief A stand alone function that expects the parameter to not be NULL to which it allocates certain memory for certain fields.
 * @param arena A pointer to a user defined type variable/object.
 * @note This is a traditional arena, so it will be following the stack data structure pattern and FIFO. 
 *       An arena can be also known as a bump allocator or slab allocator      
*/
void init_arena_t(arena_t* arena);

/**
 * @brief A stand alone function that expects the parameter to not be NULL to which it allocates certain memory for certain fields.
 * @param colosseum A pointer to a user defined type variable/object.
 * @note This is a traditional arena, so it will be following the stack data structure pattern and FIFO. 
 *       An arena can be also known as a bump allocator or slab allocator      
*/
void init_colosseum_t(colosseum_t* colosseum);

/** 
 * @brief A stand alone function that expects the parameter to not be NULL. It is a destructor for arena
 * @param arena A pointer to a user defined type variable/object.
 * @note This function should be called at the end of the program or if a catastrophic event occurs  
*/
void dctor_arena_t(arena_t* arena);

/** 
 * @brief A stand alone function that expects the parameter to not be NULL. It is a destructor for arena
 * @param arena A pointer to a user defined type variable/object.
 * @note This function should be called at the end of the program or if a catastrophic event occurs  
*/
void dctor_colosseum_t(colosseum_t* colosseum);

/** 
 * @brief A stand alone function that enforces the power of two alignment
 * @param ptr a variable that needs to be aligned 
 * @param align the desired byte boundary that 'ptr' should align to 
*/
extern uintptr_t alignment(uintptr_t ptr, size_t align);

/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply pushes bytes to the arena, and assigns the data field 'res' with a memory address that will be used.
 * @param arena a pointer to a user defined type variable/object.
 * @param bytes A numerical value that represents the size of the object
 * @note if 'bytes' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
void arena_t_push(arena_t* arena, size_t bytes);

/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply pushes bytes to the arena, and assigns the data field 'res' with a memory address that will be used.
 * @param colosseum a pointer to a user defined type variable/object.
 * @param bytes A numerical value that represents the size of the object
 * @note if 'bytes' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
void colosseum_t_push(colosseum_t* colosseum, size_t bytes);

/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply returns a valid raw memory pointer that is from 'chunk' memory region.
 * @param arena a pointer to a user defined type variable/object.   
*/
void* arena_t_get_current_position(arena_t* arena);

/**
 * @brief A stand alone function apart of the colosseum's design pattern. 
 *        It simply returns a valid raw memory pointer that is from 'chunk' memory region.
 * @param colosseum a pointer to a user defined type variable/object.
 * @param cacheline a unique numerical value that represents the cacheline where the targeted bytes will be stored at
 * @note this implementation uses hash-mixing to achieve O(1)   
*/
colosseum_t* colosseum_t_get_colosseum(colosseum_t* colosseum, const int cacheline);

/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply pops bytes from the arena, re-assigns data field's 'curr' and 'prev'.
 * @param arena a pointer to a user defined type variable/object.
 * @param offset A numerical value that represents the distance from arena to the object.   
*/
void arena_t_pop(arena_t* arena, size_t offset);

/**
 * @brief A stand alone function apart of the colloseum design pattern. 
 *        It simply pops bytes from the arena, re-assigns data field's 'curr' and 'prev'.
 * @param colloseum a pointer to a user defined type variable/object.
 * @param bytes A numerical value that represents the object.   
*/
void colosseum_t_pop_bytes(colosseum_t* colloseum, size_t bytes);

/**
 * @brief A stand alone function apart of the arena's design pattern. 
 *        It simply resizes arena's 'chunk' to the desired size, and will continue to do so until USHRT_MAX is hit.
 * @param arena a pointer to a user defined type variable/object.
 * @param new_size A numerical value that represents the size of the object
 * @note if 'new_size' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
void arena_t_resize(arena_t *arena, size_t new_size);

/**
 * @brief A stand alone function apart of the colosseum's design pattern. 
 *        It simply resizes arena's 'chunk' to the desired size.
 * @param arena a pointer to a user defined type variable/object.
 * @param new_size A numerical value that represents the size of the object
 * @note if 'new_size' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
void colosseum_t_resize(colosseum_t* colosseum, size_t new_size);

/**
 * @brief A stand alone function that simply puts the compound literal's or itself in a state where the pages will be "freed" by the kernel.
 *        But, the pages will be re-initialized iif the program re-accesses it again. 
 * @param arena a pointer to a user defined type variable/object.
 * @param bytes A numerical value that represents the size of the object
 * @note if 'bytes' is not a power of two numerical value, it will be scaled to the closest power of two numerical value   
*/
void arena_t_reset(arena_t* arena);

#endif