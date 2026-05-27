#include "allocator.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct bucket_t {

    uint8_t             flag;
    uint8_t             _pad[7];
    arena_t*            arena;       
    void*               ua;
    uintptr_t           offset;
    uint8_t*            bytes;
    uint8_t*            inuse; 

} bucket_t;

allocator_t allocator = {0};
FORCE_INLINE void thread_pool_ctor(args_t* args);
FORCE_INLINE void resize_arg_t_arr(size_t next);
FORCE_INLINE threads_t* find_available_thread();

/**
    * @description: A Free function that uses a specific bucket index to update its state. 
    * @param idx: the specific parameter determined from an index from allocator->bucket.small, medium and or large. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE int bitmap_set(size_t idx, size_t start, size_t end) {
    if (idx < start || idx > end) return 0;
    allocator.bits[idx] = 0x0;
    return 1;
}

/**
    * @description: A Free function that uses a specific bucket index to mark the bucket as open. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE int bitmap_clear(int idx, int start, int end) {
    if (idx < start || idx > end) return 0;
    allocator.bits[idx] = 0x01;
    return 1;
}

FORCE_INLINE size_t bitmap_find_index(bucket_t* slot, size_t start) {
    uintptr_t slot_addr = (uintptr_t)slot;
    uintptr_t bucket_start = 0;
    switch (start) {
        case BUCKET_SMALL_CAP:
            bucket_start = (uintptr_t)allocator.bucket.small + start * sizeof(bucket_t);
        case BUCKET_MEDIUM_CAP: 
            bucket_start = (uintptr_t)allocator.bucket.medium + start * sizeof(bucket_t);
        #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
            case BUCKET_LARGE_CAP:
                bucket_start = (uintptr_t)allocator.bucket.large + start * sizeof(bucket_t);
        #endif
    }
    uintptr_t offset = slot_addr - bucket_start;
    size_t index = offset / sizeof(bucket_t);
    return index;
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
FORCE_INLINE int bitmap_test(size_t start, size_t end) {
    uintptr_t data       = *(uintptr_t*)(allocator.bits + start);
    size_t    range      = end - start;
    size_t    bit_range  = range * CHAR_BIT;
    uintptr_t range_mask = (bit_range >= sizeof(uintptr_t) * CHAR_BIT)
                         ? ~(uintptr_t)0
                         : ((uintptr_t)1 << bit_range) - 1;

    uintptr_t target_bits = data & range_mask;
    if (target_bits == 0) return -1;

    // Each byte is 0x01 or 0x00 — byte position maps to slot index
    return start + ((__builtin_ffsl((long)target_bits) - 1) / CHAR_BIT);
}

/**
    * @description: A free function that tests to see which index is open. odd numbers the bucket is not full, even numbers bucket is full.
    * @param bm: the bitmap that is integrated into the allocator variable. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END
    * @return: Returns negative one to indicate the partition is full.
*/
FORCE_INLINE int bitmap_find_free(size_t start, size_t end) { return bitmap_test(start, end); }

/**
    * @description: A Free function that copies over the modified bucket to the global variable allocator causing it to sync properly.
    * @param slot: bucket_t pointer type that needs to be synced with allocator variable
    * @return: None.
*/
FORCE_INLINE void sync_allocator_buckets(bucket_t* slot) {
    threads_t* sync_thread = &allocator.pool[ALLOC_THREAD_POOL_SIZE - 1];
    pthread_mutex_lock(sync_thread->mutex);
    uintptr_t slot_offset = (uintptr_t)slot - (uintptr_t)&allocator;
    bucket_t* _slot = (bucket_t*)((uintptr_t)&allocator + slot_offset);
    *_slot = *slot;
    pthread_mutex_unlock(sync_thread->mutex);

}

FORCE_INLINE void sync_thread_pool(args_t* args) {
    bucket_t* shared_slot = args->arr[0];
    threads_t* shared_thread = args->arr[1];
    for (size_t i = 0; i < ALLOC_THREAD_POOL_SIZE - 2; i++) {
        threads_t* iter = allocator.pool + i;
        if (iter->flag == 0x01) {
            // if any threads are set to 0x01, they were marked with MADV_SEQUENTIAL and MADV_MERGEABLE
            // Meaning that we are preventing stale caches, and will mitigates msync's overhead. 
            join_thread(iter, NULL);
            memset(&iter->args, 0, sizeof(args_t));
            iter->flag = 0x0;
            const int res = msync(shared_slot->arena, sizeof(arena_t), MS_SYNC);
            if (res != -1) {
                sync_allocator_buckets(shared_slot);
            }
            else DBG("%d", res);
        }
    }
    shared_thread->flag = 0x0;
    return;
}

/** 
    * @description: A Free function that syncs bucket_t flags with arena.
    * @param b: A specific bucket that needs to be updated
*/
FORCE_INLINE void bucket_sync_flag(bucket_t* b) {
    b->flag = (b->arena && b->arena->flag == 0x01) ? 0x01 : 0x0;
    sync_allocator_buckets(b);
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
        bucket_t* slot = (bucket_t*)((uintptr_t)&allocator + offsetof(allocator_t, bucket) + (abs_idx) * sizeof(bucket_t));
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
    bucket_t* slot = (bucket_t*)((uintptr_t)&allocator + offsetof(allocator_t, bucket) + (abs_idx) * sizeof(bucket_t));
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
    if (sz < BUCKET_SMALL_CAP) {
        idx = bitmap_find_free(SMALL_BIT_START, SMALL_BIT_END - 1);
        #if PRINT_DEBUGGING == 1
            printf("[find_free_slot] small idx: %d\n", idx);
        #endif 
        if (idx != -1) return (uintptr_t)allocator.bucket.small + (idx - SMALL_BIT_START) * sizeof(bucket_t);
    }
    else if (sz < BUCKET_MEDIUM_CAP) {
        idx = bitmap_find_free(MEDIUM_BIT_START, MEDIUM_BIT_END - 1);
        if (idx != -1) return (uintptr_t)allocator.bucket.medium + (idx - MEDIUM_BIT_START) * sizeof(bucket_t);
    }
    else {
        #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
            idx = bitmap_find_free(LARGE_BIT_START, LARGE_BIT_END - 1);
            if (idx != -1) return (uintptr_t)allocator.bucket.large + (idx - LARGE_BIT_START) * sizeof(bucket_t);
        #endif
    }
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
    uintptr_t mask = ~((uintptr_t)allocator.bucket.small + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) - 1); 
    b = ((uintptr_t)ptr & mask) >= (uintptr_t)allocator.bucket.small + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) && 
            ((uintptr_t)ptr & mask) <= (uintptr_t)allocator.bucket.small + ((SMALL_BIT_END - 1) - SMALL_BIT_START) * sizeof(bucket_t) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) ?
            (bucket_t*)((uintptr_t)ptr & (mask - (offsetof(bucket_t, arena) + offsetof(arena_t, chunk)))) : NULL;
    if (b) {
        abs_index = bitmap_find_index(b, SMALL_BIT_START);
        if (bucket_mark_full(b, SMALL_BIT_START, SMALL_BIT_END - 1, abs_index))
                bucket_mark_free(b, SMALL_BIT_START, SMALL_BIT_END - 1, abs_index);
            return b;
    }
    mask = ~((uintptr_t)allocator.bucket.medium + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) - 1);
    b = ((uintptr_t)ptr & mask) >= (uintptr_t)allocator.bucket.medium + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) && 
            ((uintptr_t)ptr & mask) <= (uintptr_t)allocator.bucket.medium + ((MEDIUM_BIT_END - 1) - MEDIUM_BIT_START) * sizeof(bucket_t) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) ?
            (bucket_t*)((uintptr_t)ptr & (mask - (offsetof(bucket_t, arena) + offsetof(arena_t, chunk)))) : NULL;
    if (b) {
        abs_index = bitmap_find_index(b, MEDIUM_BIT_START);
        if (bucket_mark_full(b, MEDIUM_BIT_START, MEDIUM_BIT_END - 1, abs_index))
                bucket_mark_free(b, MEDIUM_BIT_START, MEDIUM_BIT_END - 1, abs_index);
            return b;
    }

    #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
        mask = ~((uintptr_t)allocator.bucket.large + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) - 1);
        b = ((uintptr_t)ptr & mask) >= (uintptr_t)allocator.bucket.large + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) && 
            ((uintptr_t)ptr & mask) <= (uintptr_t)allocator.bucket.large + ((LARGE_BIT_END - 1) - LARGE_BIT_START) * sizeof(bucket_t) + offsetof(bucket_t, arena) + offsetof(arena_t, chunk) ?
            (bucket_t*)((uintptr_t)ptr & (mask - (offsetof(bucket_t, arena) + offsetof(arena_t, chunk)))) : NULL;
        if (b) {
            abs_index = bitmap_find_index(b, LARGE_BIT_START);
            if (bucket_mark_full(b, LARGE_BIT_START, LARGE_BIT_END - 1, abs_index))
                    bucket_mark_free(b, LARGE_BIT_START, LARGE_BIT_END - 1, abs_index);
                return b;
        }
    #endif

    return NULL;
}

/**
    * @description: A Free Function that pushes the unused memory addresses to bucket.
        It is used with deallocation function
    * @param b: A specific bucket that will now have been updated 
*/
FORCE_INLINE void push_to_bucket(bucket_t* slot, size_t offset) {
    if (offset != 0 && offset != 1) {
        size_t bytes = slot->bytes[offset];
        slot->inuse[offset] = 0x0;
        slot->bytes[offset] = bytes;
        slot->ua = (void*)((uintptr_t)slot->ua + offset);
        sync_allocator_buckets(slot);
    }
}

/**
    * @description: A free function that pops off a memory address that is not in use based on the requested size
    * @param slot: The slot which is bucket_t that has memory addresses to be used. 
    * @param bytes: the requested bytes 
    * @return: Returns null if bucket field is null or if bucket == slot->arena->chunk  
*/
FORCE_INLINE void* pop_from_bucket(bucket_t* slot, size_t bytes) {
    if (!slot->ua || slot->ua == slot->arena->chunk) return NULL;

    uintptr_t uint_addr = (uintptr_t)slot->ua - (bytes & ~(bytes - 1)); // clamp to nearest power of two of bytes
    void* address = (void*)(uint_addr);
    slot->ua = (void*)uint_addr;
    size_t offset = (size_t)((uintptr_t)address - (uintptr_t)slot->arena->chunk); // This also needs to be squeezed
    slot->inuse[offset] = 0x01;
    slot->bytes[offset] = 0;
    slot->arena = push(slot->arena, bytes);
    sync_allocator_buckets(slot);
    return address;
}

FORCE_INLINE size_t find_bucket_size(const bucket_t* slot) {
    uintptr_t addr = (uintptr_t)slot;
    uintptr_t small_start  = (uintptr_t)allocator.bucket.small;
    uintptr_t small_end    = small_start  + BUCKET_SMALL_CAP  * sizeof(bucket_t);
    uintptr_t medium_start = (uintptr_t)allocator.bucket.medium;
    uintptr_t medium_end   = medium_start + BUCKET_MEDIUM_CAP * sizeof(bucket_t);
    
    if (addr >= small_start && addr < small_end)   return BUCKET_SMALL_CAP;
    if (addr >= medium_start && addr < medium_end) return BUCKET_MEDIUM_CAP;

    #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
        uintptr_t large_start = (uintptr_t)allocator.bucket.large;
        uintptr_t large_end   = large_start + BUCKET_LARGE_CAP * sizeof(bucket_t);
        if (addr >= large_start && addr < large_end) return BUCKET_LARGE_CAP;
    #endif

    return 0;
}

[[gnu::cold]]
FORCE_INLINE void clear_buckets() {
    size_t small = 0;
    size_t medium = 0;
    while (small < 64) {
        arena_t* arena = allocator.bucket.small[small].arena;
        while (arena->next) {
            munmap_address(arena->chunk, ARENA_SIZE);
            arena_t* prev = arena;
            arena = arena->next;
            munmap_address(prev, sizeof(arena_t));
            
        }
        small = small + 1;
    }
    while (medium < 128) {
        arena_t* arena = allocator.bucket.medium[medium].arena;
        while (arena->next) {
            munmap_address(arena->chunk, ARENA_SIZE);
            arena_t* prev = arena;
            arena = arena->next;
            munmap_address(prev, sizeof(arena_t));
        }
        medium = medium + 1;
    }
    #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
        size_t large = 0;
        while (large < 256) {
            arena_t* arena = allocator.bucket.large[large].arena;
            while (arena->next) {
                munmap_address(arena->chunk, ARENA_SIZE);
                arena_t* prev = arena;
                arena = arena->next;
                munmap_address(prev, sizeof(arena_t));
            }
            large = large + 1;
        }
    #endif
}

FORCE_INLINE void* arena_offset(args_t* args) {
    bucket_t* shared_slot = args->arr[0];
    threads_t* shared_thread = args->arr[1];
    size_t offset = 0;

    int rc;
    rc = pthread_mutex_lock(shared_thread->mutex);
    if (rc == 0) {
        for (size_t i = 0; i < ARENA_SIZE; i++) {
            if (shared_slot->inuse[i] == 0x0 && shared_slot->bytes[offset] != 0) offset = offset + i;
        }
        if (offset == shared_slot->arena->curr) { 
            clear_arena_t(shared_slot->arena);
            size_t bucket_size = find_bucket_size(shared_slot);
            munmap_address(shared_slot->arena, ARENA_SIZE);
            munmap_address(shared_slot->bytes, bucket_size * sizeof(uint8_t));
            munmap_address(shared_slot->inuse, bucket_size * sizeof(uint8_t));
        }
        else {
            // Avoid underflows and or overflows by subtraction arena's offset properly 
            if (shared_slot->arena->curr != 0) {
                shared_slot->arena->curr -= offset;
                shared_slot->arena->prev -= offset;
                shared_slot->offset = 0;
                sync_allocator_buckets(shared_slot);
            }
        }
        shared_thread->flag = 0x0;
        pthread_mutex_unlock(shared_thread->mutex);
    }

    return NULL;
}

/**
    * @description: Function that allocates the thread pool internally. 
    
    * @note: As of 5/27/26 allocator.pool[ALLOC_THREAD_POOL_SIZE - 1] is the only process that should be updating allocator's thread pool and no other thread should 
*/
[[gnu::hot]]
FORCE_INLINE void thread_pool_ctor(args_t* args) {

    size_t* next = (size_t*)args->arr[0];
    threads_t* shared_thread = (threads_t*)args->arr[1];
    int rc;

    if (pthread_equal(shared_thread->thread_id, allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].thread_id)) {
        rc = pthread_mutex_lock(shared_thread->mutex);
        if (rc == 0) {

            for (size_t i = *next; i < ALLOC_THREAD_POOL_SIZE; i++) { 
                threads_t* t = &allocator.pool[i];
                if (!t->mutex) {
                    threads_t* tmp = init_threads_t();
                    memmove(t, tmp, sizeof(threads_t));
                    memset(tmp, 0, sizeof(threads_t));
                    munmap_address(tmp, sizeof(threads_t));
                    t->args.arr = malloc(2 * sizeof(void*));
                }
            }

            shared_thread->flag = 0x0;
            pthread_mutex_unlock(shared_thread->mutex);
            
        }

        memset(next, 0, sizeof(size_t));
        free(next);
    }

    #if LOGGING == 1
            #if LOGLEVEL == 0  

                //printf("thread_arguments success: Successfully synced and joined thread process for thread_pool_ctor \n");
                // include the rc lvalue and the memory address of the thread and match it with the index
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif
    
}

/**
    * @description: An external function that is used for multi-threading. It uses args_t visit variable to find out what function to visit.
                Functions that get visited will get locked and synced to avoid race conditions, while the scope of this function will join the thread.
    * @param arg: A user defined struct type called args_t that is used to visit whatever function needs to be threaded.
*/
void* thread_arguments(void* arg) {
    
    args_t* args = (args_t*)arg;
    int rc = 0;

    if (strcmp(args->visit, "arena_offset") == 0) {

        threads_t* thread = NULL;
        thread = (threads_t*)(args->arr[1]);
        
        rc = pthread_mutex_lock(thread->mutex);
        if (rc == 0)  {

            arena_offset(args);
            pthread_mutex_unlock(thread->mutex);
            join_thread(thread, NULL);

        }
        #if LOGGING == 1
            #if LOGLEVEL == 0  

                printf("thread_arguments success: Successfully synced and joined thread process for sync_threads \n");
                // include the rc output 
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif

        #endif

    }
    else if (strcmp(args->visit, "sync_threads") == 0) {
        threads_t* thread = NULL;
        thread = (threads_t*)(args->arr[1]);
        
        rc = pthread_mutex_lock(thread->mutex);
        if (rc == 0)  {

            sync_thread_pool(args);
            pthread_mutex_unlock(thread->mutex);
            join_thread(thread, NULL);

        }
        #if LOGGING == 1
            #if LOGLEVEL == 0  

                printf("thread_arguments success: Successfully synced and joined thread process for sync_threads \n");
                // include the rc output 
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif
    }
    else if (strcmp(args->visit, "thread_pool_ctor") == 0) {
        thread_pool_ctor(args);
        threads_t* thread = NULL;
        
        rc = pthread_mutex_lock(allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].mutex);
        if (rc == 0) {

            thread = (threads_t*)(args->arr[1]);
            pthread_mutex_unlock(allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].mutex);

        }

        join_thread(thread, NULL);
        #if LOGGING == 1
            #if LOGLEVEL == 0  

                printf("thread_arguments success: Successfully synced and joined thread process for thread_pool_ctor \n");
                
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif

    }
    else if (strcmp(args->visit, "extra_thread") == 0) {
        bucket_t* shared_slot = (bucket_t*)args->arr[0];
        threads_t* shared_thread = (threads_t*)args->arr[1];
        arena_offset(args);
        
        if (pthread_mutex_trylock(shared_thread->mutex) != 0) {
            const int res = msync(shared_slot->arena, sizeof(arena_t), MS_SYNC);
            if (res != -1) {
               sync_allocator_buckets(shared_slot);
            }
            else {
                // Debugging info goes here....
            }
            pthread_mutex_unlock(shared_thread->mutex);
        }

        join_thread(shared_thread, NULL);

        #if LOGGING == 1
            #if LOGLEVEL == 0  

                printf("thread_arguments success: Successfully synced and joined thread process for thread_pool_ctor \n");
                
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif
    }

    pthread_exit(NULL);
}

[[gnu::hot]]
FORCE_INLINE threads_t* find_available_thread() {
    for (size_t _i = 0; _i < ALLOC_THREAD_POOL_SIZE; _i++) { 
        if (allocator.pool[_i].flag == 0x0) {
            allocator.pool[_i].flag = 0x01;
            return &allocator.pool[_i];
        }
    }
    return NULL;
}

[[gnu::hot]]
//[[gnu::constructor(0)]]
// TODO: Need to make sure that MADV_MERGEABLE enabled does not consume a lot of processing power; use with care.
FORCE_INLINE void alloc_init(void) {
    int res = 0;
    threads_t* thread = NULL;
    if (!allocator.bits) {
        #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
            allocator.bits = private_address(NULL, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            res = madvise(allocator.bits, BITMAP_SIZE * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) DBG("%d", res);
            memset(allocator.bits, 1, 448 * sizeof(uint8_t));

            allocator.bucket.large = shared_address(NULL, BUCKET_LARGE_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            res = madvise(allocator.bucket.large, BUCKET_LARGE_CAP * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) DBG("%d", res);
           
        #else 
            allocator.bits = private_address(NULL, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memset(allocator.bits, 1, BITMAP_SIZE * sizeof(uint8_t));
            res = madvise(allocator.bits, BITMAP_SIZE * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) DBG("%d", res);
        #endif

        allocator.bucket.small = shared_address(NULL, BUCKET_SMALL_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        res = madvise(allocator.bucket.small, BUCKET_SMALL_CAP * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) DBG("%d", res);

        allocator.bucket.medium = shared_address(NULL, BUCKET_MEDIUM_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        res = madvise(allocator.bucket.medium, BUCKET_MEDIUM_CAP * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) DBG("%d", res);
        
        allocator.pool = shared_address(NULL, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        res = madvise(allocator.pool, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) DBG("%d", res);
        
        thread = &allocator.pool[ALLOC_THREAD_POOL_SIZE - 1];
        threads_t* tmp = init_threads_t();
        memmove(thread, tmp, sizeof(threads_t));
        munmap_address(tmp, sizeof(threads_t));
        
        thread->args.arr = malloc(2 * sizeof(void*));
        thread->args.visit = "thread_pool_ctor";
        size_t* next = aligned_alloc(alignof(size_t), sizeof(size_t));
        *next = 0;

        thread->args.arr[0] = (void*)next;
        thread->args.arr[1] = (void*)thread;
        thread->flag = 0x01;
        thread = create_thread(thread, 0x01, thread_arguments);
        allocator.n_bytes = sizeof(allocator.bits);
        
    }

    allocator.arena = init_arena_t();
    if (!allocator.arena) {
        fprintf(stderr, "ERROR: Failed to init arena\n");
        munmap_address(allocator.arena, sizeof(arena_t));
        return;
    }

}


FORCE_INLINE void debug_allocator() {
    // For each bucket, output the arena offset, the entries i.e `bytes` and `inuse` field members
    // Group the arena memory address with it's fields and the buckets memory address with its fields
    // output allocator.bits and view all of the slots   
}


/**
    * @description: A Free Function that recrusive calls itself if arena is full and moves it forward.
    * @param bytes: The requested bytes.
    * @return: Return a memory address of desired size or big enough to hold x amount of bytes.
               Otherwise, return null, and that will indicate that everything is full.
*/
[[gnu::hot]]
void* allocate(size_t bytes) {
    char* address = NULL;
    int idx = -1; 
    int res = 0;

    threads_t* thread = &allocator.pool[ALLOC_THREAD_POOL_SIZE - 1];
    threads_t* tao = NULL;
    bucket_t* slot = (bucket_t*)alloc_find_free_slot(bytes);
    
    int rc;
    rc = pthread_mutex_lock(thread->mutex);
    if (rc == 0) {
        
        if (thread->flag == 0x0) {
            thread->flag = 0x01; 
            thread->args.visit = "sync_threads";
            thread->args.arr[0] = (void*)slot;
            thread->args.arr[1] = (void*)thread;
            thread = create_thread(thread, 0x01, thread_arguments);
        }
        #if LOGGING == 1
            #if LOGLEVEL == 0  

                printf("allocate error: Failed to assign memory address... printing out information\n");
                //debug_allocator();

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif
        pthread_mutex_unlock(thread->mutex);

    }

    if (slot->offset > 0) {
        tao = find_available_thread();
        
        if (tao == NULL) {

            threads_t* tmp = init_threads_t();
            tao = shared_address(tao, sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);;
            res = madvise(tao, sizeof(threads_t), MADV_DONTNEED);
            memmove(tao, tmp, sizeof(threads_t));
            munmap_address(tmp, sizeof(threads_t));

        }

        tao->args.visit = "extra_thread";
        tao->args.size = 2;
        tao->args.arr[0] = (void*)slot;
        tao->args.arr[1] = (void*)tao;
        tao = create_thread(thread, 0x01, thread_arguments);

    }
    
    int_fast16_t start = 0;
    int_fast16_t end = 0;
    if (slot) {
        if (!slot->arena) {
            slot->arena = allocator.arena;
            if (!slot->bytes && !slot->inuse) {

                slot->bytes = private_address(slot->bytes, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                slot->inuse = private_address(slot->inuse, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                res = madvise(slot->bytes, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
                res = madvise(slot->inuse, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
                
            }
        }
        

        if (slot->flag != 0x01) {

            if (bytes <= SMALL_BIT_END) {
                start = SMALL_BIT_START;
                end = SMALL_BIT_END;

                address = pop_from_bucket(slot, bytes);              
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }

                idx = bitmap_find_free(SMALL_BIT_START, SMALL_BIT_END);

            }
            else if (bytes <= MEDIUM_BIT_END && bytes >= SMALL_BIT_END) {
                start = MEDIUM_BIT_START;
                end = MEDIUM_BIT_END;

                address = pop_from_bucket(slot, bytes); 
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }

                idx = bitmap_find_free(MEDIUM_BIT_START, MEDIUM_BIT_END);

            }
            else {
                #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
                    start = LARGE_BIT_START;
                    end = LARGE_BIT_END;
                    
                    address = pop_from_bucket(slot, bytes);
                    if (address) {
                        memset(address, 0, bytes);
                        return address;
                    }

                    idx = bitmap_find_free(allocator.map, LARGE_BIT_START, LARGE_BIT_END);
                #endif
            }

            if (allocator.arena->flag != 0x01) {
                allocator.arena = push(allocator.arena, bytes);
                if (allocator.arena->flag == 0x01) { 
                    allocator.bits[(idx) / CHAR_BIT] &= ~(1U << ((idx) % CHAR_BIT));
                    bitmap_set(idx, start, end);
                    bucket_sync_flag(slot);
                    arena_t* full = allocator.arena;
                    allocator.arena = NULL;
                    alloc_init();
                    allocator.arena->next = full;
                    return allocate(bytes);
                }

                address = allocator.arena->res;
                size_t offset = (uintptr_t)address - (uintptr_t)allocator.arena->chunk;
                slot->bytes[offset] = bytes;
                memset(address, 0, bytes);
                return address;
            }
        }
    }
    else {
        #if LOGGING == 1
            // 0 minor 
            // 1 medium
            // 2 large
            #if LOGLEVEL == 0  

                printf("allocate error: Failed to assign memory address... printing out information\n");
                debug_allocator();
                
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif
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
[[gnu::hot]]
FORCE_INLINE void deallocate(void* ptr) {
    if (!ptr || !allocator.bits) return;

    bucket_t* slot = NULL;
    slot = find_slot(ptr);
    if (!slot) return;

    uint8_t* base = (uint8_t*)slot->arena->chunk;
    if ((uint8_t*)ptr < base || (uint8_t*)ptr >= base + slot->arena->size) {
        #if LOGGING == 1
            #if LOGLEVEL == 0  

                //printf("allocate error: Failed to assign memory address... printing out information\n");
                //debug_allocator();
                
            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
        #endif
        printf("ERROR ALLOCATOR.C: deallocate — ptr %p out of arena bounds\n", ptr);
        return;
    }

    size_t offset = (size_t)((uintptr_t)ptr - (uintptr_t)slot->arena->chunk);
    if (slot->offset == slot->arena->curr) {
        threads_t* thread = find_available_thread();
        if (thread) {
            thread->flag = 0x01;
            thread->args.visit = "arena_offset";
            thread->args.size = 1;
            thread->args.arr[0] = (void*)slot;
            thread->args.arr[1] = (void*)thread;
            thread = create_thread(thread, 0x01, thread_arguments);
        }
    } else slot->offset = slot->offset + offset;
    size_t bytes = slot->bytes[offset];

    if (!slot->ua) slot->ua = ptr;
    else push_to_bucket(slot, offset);
    
    memset(ptr, 0xFF, bytes);

}

[[gnu::cold]]
void init_allocator_t() {
    if (!allocator.bits)  alloc_init();

    if (!allocator.allocate && !allocator.deallocate) {
        allocator.allocate   = allocate;
        allocator.deallocate = deallocate;
    }
}

[[gnu::cold]]
void clear_allocator() {
    for (size_t i = 0; ALLOC_THREAD_POOL_SIZE; i++) clean_threads(&allocator.pool[i]);
    clear_buckets();
    munmap_address(allocator.arena->chunk, ARENA_SIZE);
    munmap_address(allocator.arena, sizeof(arena_t));
    
}