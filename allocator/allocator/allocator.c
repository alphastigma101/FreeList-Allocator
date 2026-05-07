#include "allocator.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>

allocator_t* allocator = NULL;

/**
    * @description: A Free function that uses a specific bucket index to update its state. 
    * @param idx: the specific parameter determined from an index from allocator->bucket.small, medium and or large. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
static int bitmap_set(int idx, int start, int end) {
    if (idx < start || idx > end) return 0;
    allocator->map.bits[idx / CHAR_BIT] |= (1U << (idx % CHAR_BIT));
    return 1;
}

/**
    * @description: A Free function that uses a specific bucket index to mark the bucket as open. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
static int bitmap_clear(int idx, int start, int end) {
    if (idx < start || idx > end) return 0;
    allocator->map.bits[idx] = 1;
    return 1;
}

/**
    * @description: A Free function that uses a specific bucket index to test and see if the desired index in the bitmap is 0 or 1.
    * @param bm: allocator's bitmap. 
    * @param idx: the specific parameter determined from an index from allocator->bucket.small, medium and or large. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
static int bitmap_test(bitmap_t bm,, unsigned int start, unsigned int end) {
    uintptr_t data = *(uintptr_t*)(bm.bits + start);
    unsigned int range = end - start;
    unsigned int bit_range = range * CHAR_BIT;
    uintptr_t range_mask = (bit_range >= sizeof(uintptr_t) * CHAR_BIT)
                               ? ~(uintptr_t)0
                               : ((uintptr_t)1 << bit_range) - 1;
    uintptr_t target_bits = data & range_mask;

    if (target_bits == 0) return -1;
    return start + ((__builtin_ffsl((long)target_bits) - 1) / CHAR_BIT);
}


/**
    * @description: A free function that tests to see which index is open. odd numbers the bucket is not full, even numbers bucket is full.
    * @param bm: the bitmap that is integrated into the allocator variable. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END
    * @return: Returns negative one to indicate the partition is full.
*/
FORCE_INLINE int bitmap_find_free(bitmap_t bm, int start, int end) {
    const int res = bitmap_test(bm, start, end);
    if (res != -1) return res;
    return -1;
}

/**
    * @description: A Free function that copies over the modified bucket to the global variable allocator causing it to sync properly.
    * @param slot: bucket_t pointer type that needs to be synced with allocator variable
    * @return: None.
*/
FORCE_INLINE void sync_buckets(bucket_t* slot) {
    uintptr_t slot_offset = (uintptr_t)slot - (uintptr_t)allocator;
    bucket_t* _slot = (bucket_t*)((uintptr_t)allocator + slot_offset);
    *_slot = *slot;
}

/** 
    * @description: A Free function that syncs bucket_t flags with arena.
    * @param b: A specific bucket that needs to be updated
*/
FORCE_INLINE void bucket_sync_flag(bucket_t* b) {
    b->flag = (b->arena && b->arena->flag == 0x01) ? 0x01 : 0x0;
    sync_buckets(b);
}

/**
    * @description: A free function of O(1) that syncs the arena flag and the bucket flag indicating it is free and ready to be used. 
    * @param b: bucket_t pointer that needs to be marked. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END
    * @param abs_idx: the absolute index from one of the buckets slot that is used to obtain 'b'. 
                      It is used to update the bitmap to mark 'b' as full.              
*/
FORCE_INLINE int bucket_mark_full(bucket_t* b, int start, int end, int abs_idx) {
    b->flag = (b->arena && b->arena->flag == 0x01) ? 0x01 : 0x0;
    if (b->flag == 0x01) {
        bitmap_clear(abs_idx, start, end);
        bucket_t* slot = (bucket_t*)((uintptr_t)allocator + offsetof(allocator_t, bucket) + (abs_idx) * sizeof(bucket_t));
        *slot = *b;
        return 1;
    }
    return 0;
}

/**
    * @description: A free function of O(1) that syncs the arena flag and the bucket flag indicating it is free and ready to be used. 
    * @param b: bucket_t pointer that needs to be marked. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END
    * @param abs_idx: the absolute index from one of the buckets slot that is used to obtain 'b'. 
                      It is used to update the bitmap to mark 'b' as free.              
*/
FORCE_INLINE void bucket_mark_free(bucket_t* b, int start, int end, int abs_idx) {
    b->flag = 0x0;
    b->arena->flag = 0x0;
    bitmap_set(abs_idx, start, end);
    bucket_t* slot = (bucket_t*)((uintptr_t)allocator + offsetof(allocator_t, bucket) + (abs_idx) * sizeof(bucket_t));
    *slot = *b;
}

/**
    * @description: A Free function that finds a free slot based on the size at O(n). 
    * @param sz: can be a numeric value of size: 64, 128, or 256. 
    * @return: Returns a slot right after checking the bucket's bitmap.
    * @note: if nothing is returned, that means all of the slots from small, medium and large are occupied. 
*/
FORCE_INLINE uintptr_t alloc_find_free_slot(size_t sz) {
    int idx = -1;
    if (sz < BUCKET_SMALL_MAX) {
        idx = bitmap_find_free(allocator->map, SMALL_BIT_START, SMALL_BIT_END);
        if (idx != -1) return (uintptr_t)allocator + offsetof(allocator_t, bucket.small) + (idx - SMALL_BIT_START) * sizeof(bucket_t);
    }
    else if (sz < BUCKET_MEDIUM_MAX) {
        idx = bitmap_find_free(allocator->map, MEDIUM_BIT_START, MEDIUM_BIT_END);
        if (idx != -1) return (uintptr_t)allocator + offsetof(allocator_t, bucket.medium) + (idx - MEDIUM_BIT_START) * sizeof(bucket_t);
    }
    #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
        idx = bitmap_find_free(allocator->map, LARGE_BIT_START, LARGE_BIT_END);
        if (idx != -1) return (uintptr_t)allocator + offsetof(allocator_t, bucket.large) + (idx - LARGE_BIT_START) * sizeof(bucket_t);
    #endif
    return -1;
}

/**
    * @description: A Free function that finds the specific bucket, which contains an arena that the memory address came from or not.
    * @param ptr: A memory address that came from the arena. 
    * @return: Returns null if nothing was found.
    * @note: If nothing was found, that means the 'ptr' memory address is from a memory address that is not associated with the arena. 
             Or an address that was allocated on the heap. 
*/
FORCE_INLINE bucket_t* find_slot(void* ptr) {
    int abs_index = -1;
    bucket_t* b = NULL;
    uintptr_t mask = ~((uintptr_t)allocator + offsetof(allocator_t, bucket.small) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) - 1); 
    b = ((uintptr_t)ptr & mask) >= (uintptr_t)allocator + offsetof(allocator_t, bucket.small) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) && 
            ((uintptr_t)ptr & mask) <= (uintptr_t)allocator + offsetof(allocator_t, bucket.small) + (SMALL_BIT_END - SMALL_BIT_START) * sizeof(bucket_t) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) ?
            (bucket_t*)((uintptr_t)ptr & (mask - (offsetof(bucket_t, arena) + offsetof(arena_t, chunk)))) : NULL;
    if (b) {
        abs_index = bitmap_find_index(b, SMALL_BIT_START);
        if (bucket_mark_full(b, SMALL_BIT_START, SMALL_BIT_END, abs_index))
                bucket_mark_free(b, SMALL_BIT_START, SMALL_BIT_END, abs_index);
            return b;
    }
    mask = ~((uintptr_t)allocator + offsetof(allocator_t, bucket.medium) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) - 1);
    b = ((uintptr_t)ptr & mask) >= (uintptr_t)allocator + offsetof(allocator_t, bucket.medium) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) && 
            ((uintptr_t)ptr & mask) <= (uintptr_t)allocator + offsetof(allocator_t, bucket.medium) + (MEDIUM_BIT_END - MEDIUM_BIT_START) * sizeof(bucket_t) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) ?
            (bucket_t*)((uintptr_t)ptr & (mask - (offsetof(bucket_t, arena) + offsetof(arena_t, chunk)))) : NULL;
    if (b) {
        abs_index = bitmap_find_index(b, MEDIUM_BIT_START);
        if (bucket_mark_full(b, MEDIUM_BIT_START, MEDIUM_BIT_END, abs_index))
                bucket_mark_free(b, MEDIUM_BIT_START, MEDIUM_BIT_END, abs_index);
            return b;
    }
    return NULL;
}

/**
    * @description: A Free Function that pushes the unused memory addresses to bucket.
    * @param b: A specific bucket that will now have updated 
*/
FORCE_INLINE  void push_to_bucket(bucket_t* b, size_t offset) {
    if (offset != 0 && offset != 1) {
        uintptr_t extend_bucket = (uintptr_t)b->arena->chunk + offset;
        b->bucket = (uint8_t*)((void*)((uintptr_t)b->bucket + (uintptr_t)extend_bucket));
        sync_buckets(b);
    }
}

/**
    * @description: A free function that pops off a memory address that is not in use based on the requested size
    * @param slot: The slot which is bucket_t that has memory addresses to be used. 
    * @param bytes: the requested bytes 
    * @return: Returns null if bucket field is null or if bucket == slot->arena->chunk  
*/
FORCE_INLINE void* pop_from_bucket(bucket_t* slot, size_t bytes) {
    if (!slot->bucket || slot->bucket == slot->arena->chunk) return NULL;

    uintptr_t bucket = (uintptr_t)slot->bucket - bytes;
    void* address = (void*)(bucket);
    slot->bucket = (uint8_t*)((uint8_t*)slot->bucket - (uint8_t*)bucket);
    size_t offset = (size_t)((uintptr_t)address - (uintptr_t)slot->arena->chunk);
    //slot->entries.bytes[offset] = 0; // clear or add another variable called inuse. 
    slot->arena = push(slot->arena, bytes);
    sync_buckets(slot);
    return address;
}

FORCE_INLINE void alloc_init(void) {
    if (!allocator) {
        allocator = aligned_alloc(alignof(allocator_t),sizeof(allocator_t));
        if (!allocator) {
            fprintf(stderr, "ERROR: Failed to allocate allocator\n");
            return;
        }
        
        memset(allocator, 0, sizeof(allocator_t));
        memset(allocator->map.bits, 0xFF, sizeof(allocator->map.bits));
        allocator->map.n_bytes = sizeof(allocator->map.bits);
        ALLOC_FOREACH_THREAD(allocator);
    }

    allocator->arena = init_arena_t();
    if (!allocator->arena) {
        fprintf(stderr, "ERROR: Failed to init arena\n");
        free(allocator);
        allocator = NULL;
        return;
    }
}


/**
    * @description: A Free Function that recrusive calls itself if arena is full and moves it forward.
    * @param bytes: The requested bytes.
    * @return: Return a memory address of desired size or big enough to hold x amount of bytes.
               Otherwise, return null, and that will indicate that everything is full.
*/
static void* allocate(size_t bytes) {
    char* address = NULL;
    int idx = -1; 
    bucket_t* slot = (bucket_t*)alloc_find_free_slot(bytes);

    if (slot) {
        if (!slot->arena) {
            slot->arena = allocator->arena;
            slot->entries.bytes = private_address(slot->entries.bytes, 4096, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        }

        if (slot->flag != 0x01) {

            if (bytes <= 64) {
                address = pop_from_bucket(slot, bytes);              
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = bitmap_find_free(allocator->map, SMALL_BIT_START, SMALL_BIT_END); 
            }
            else if (bytes <= 128 && bytes > 64) {
                address = pop_from_bucket(slot, bytes);
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = bitmap_find_free(allocator->map, MEDIUM_BIT_START, MEDIUM_BIT_END);
            }
            else {
                address = pop_from_bucket(slot, bytes);
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = bitmap_find_free(allocator->map, LARGE_BIT_START, LARGE_BIT_END);
            }

            if (allocator->arena->flag != 0x01) {
                allocator->arena = push(allocator->arena, bytes);
                if (allocator->arena->flag == 0x01) { 
                    allocator->map.bits[(idx) / CHAR_BIT] &= ~(1U << ((idx) % CHAR_BIT));
                    bucket_sync_flag(slot);
                    arena_t* full = allocator->arena;
                    allocator->arena = NULL;
                    alloc_init();
                    allocator->arena->next = full; 
                    return allocate(bytes);
                }

                address = allocator->arena->res;
                size_t offset = (uintptr_t)address - (uintptr_t)allocator->arena->chunk;
                slot->entries.bytes[offset] = bytes;
                memset(address, 0, bytes);
                return address;

            }
        }
    }
    else {
        printf("EITHER EXHAUSTED RESOURCES OR\t");
        printf("FAILED TO FIND BUCKET SLOT\n");
        return NULL;
    }

    return NULL;
}

/**
    * @description: A free function that determines if the following parameter that was passed into it is within a specific arena's memory region.
    * @param ptr: A memory address that can or is not apart of a arena. 
    * @return: Returns nothing if 'ptr' is not within any of the arena's memory range. 
    * @note: Known bug: As of 3/24/26, deallocate is supposed to be able to take a memory address despite it's offset, and unwind the specific arena backwards i.e moving the offsets back.
            If we unwind the stack i.e by coming from the begining or anywhere, sooner or later, it will eventually cause the free function in arena.c to underflow.
            Since we are increasing from here and decreasing from 'pop', it breaks the traditional arena logic. 
            A traditional arena also known as a bump allocator, functions just like a stack i.e FILO. 
            We have options to fix this bug: 
                1. We want to keep the traditional arena logic (this is preffered), and we want to convert the function below to be able to handle this bug.
                    It could be the double end stack or a queue. (Hard) 
                2. We ditch the arena idea and use a different memory structure that is more flexible than the arena. (Medium)   
*/
static void deallocate(void* ptr) {
    if (!ptr || !allocator) return;

    bucket_t* slot = NULL;
    slot = find_slot(ptr);
    if (!slot) return;

    uint8_t* base = (uint8_t*)slot->arena->chunk;
    if ((uint8_t*)ptr < base || (uint8_t*)ptr >= base + slot->arena->size) {
        printf("ERROR ALLOCATOR.C: deallocate — ptr %p out of arena bounds\n", ptr);
        return;
    }

    size_t offset = (size_t)((uintptr_t)ptr - (uintptr_t)slot->arena->chunk);
    size_t bytes = slot->entries.bytes[offset];

    // Offset will be a memory region located from the chunk. It can be greater than arena->curr or less than 
    // We will need to scale down arena->curr properly in pop free function
    POP(slot->arena, offset);
    if (!slot->bucket) slot->bucket = ptr;
    else push_to_bucket(slot, offset);
    memset(ptr, 0xFF, bytes);

}

void clear_allocator() {
    for (size_t i = 0; ALLOC_THREAD_POOL_SIZE; i++)
        clean_threads(allocator->pool[i]);
    while (allocator->arena) {
        arena_t* head = allocator->arena;
        if (head) {
            clean_address(head->chunk);
            clear_arena_t(allocator->arena);
        }
        head = allocator->arena->next;
    }
    free(allocator);
}

void init_allocator_t() {
    if (!allocator) 
        alloc_init();

    if (!allocator->allocate && !allocator->deallocate) {
        allocator->allocate   = allocate;
        allocator->deallocate = deallocate;
    }
}

/**
 * @brief Initializes a thread
 * 
 * @param arr Memory address 
 * @param mode Can be shared or not shared memory space 
 * @param _str Can be a string or it can be NULL
 * 
 * @return tokens_t Object 

 * 
 * @details 
 * - addr is a memory address depending on the mode, can have shared resources or not 
 * - mode is set to default i.e 0x01 which is SHARED       
 *         - 0x02 is for ANONYMOUS
 * @note Passed unit test cases as of 3/3/26 
*/
threads_t* init_threads_t() {
    threads_t* t = aligned_alloc(alignof(threads_t), sizeof(threads_t));
    
    if (!t) {
        printf("Failed to allocate memory for thread struct! returning null!");
        return NULL;
    }

    t->attr.mutex = aligned_alloc(alignof(pthread_mutexattr_t), sizeof(pthread_mutexattr_t));
    if (t->attr.mutex) memset(t->attr.mutex, 0, sizeof(pthread_mutexattr_t));

    t->mutex = aligned_alloc(alignof(pthread_mutex_t), sizeof(pthread_mutex_t));
    if (t->mutex) memset(t->mutex, 0, sizeof(pthread_mutex_t));
    else printf("ERROR 325: FAILED TO ALLOCATE MEMORY FOR MUTEX\n");

    t->attr.thread = aligned_alloc(alignof(pthread_attr_t), sizeof(pthread_attr_t));
    if (t->attr.thread) memset(t->attr.thread, 0, sizeof(pthread_attr_t));
    else printf("ERROR 325: FAILED TO ALLOCATE MEMORY FOR THREAD ATTRIBUTE\n");

    t->id = aligned_alloc(alignof(pthread_t), sizeof(pthread_t));
    if (t->id) memset(t->id, 0, sizeof(pthread_t));
    t->flag = 0x0;
    return t;
}

arena_t* init_arena_t() {
    arena_t* arena = NULL;
    arena = aligned_alloc(alignof(arena_t), sizeof(arena_t));
    if (!arena) {
        printf("ERROR 10 IN ARENA.C, FAILED TO ALLOCATE MEMORY FOR ARENA!\n");
        return NULL;
    }
    memset(arena, 0, sizeof(arena_t));
    arena->chunk = shared_address(arena->chunk, 4096, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!arena->chunk) {
        printf("ERROR 15 IN ARENA.C, FAILED TO ALLOCATE A CHUNK OF MEMORY!\n");
        free(arena);
        return NULL;
    }
    memset(arena->chunk, 0, 4096);
    arena->size = 4096;
    arena->curr = 1;
    return arena;
}