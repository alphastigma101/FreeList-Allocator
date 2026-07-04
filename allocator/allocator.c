#include "allocator.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
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
FORCE_INLINE threads_t find_available_thread();

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
    * @param mutex: A pointer that can be either null or not.
    * @return: None.
*/
FORCE_INLINE void sync_allocator_buckets(bucket_t* slot, pthread_mutex_t* mutex) {

    if (mutex) {

        int rc;
        rc = pthread_mutex_lock(mutex);
        
        if (rc == 0) {

            uintptr_t slot_offset = (uintptr_t)slot - (uintptr_t)&allocator;
            bucket_t* _slot = (bucket_t*)((uintptr_t)&allocator + slot_offset);
            *_slot = *slot;
            pthread_mutex_unlock(mutex);

        }

        #if LOGGING == 1

            #if LOGLEVEL == 0  

                // print out the status of rc and the memory address of the thread which should also display
                // the index of allocator.pool the thread that was used  

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif

        #endif

        return;
    }

    uintptr_t slot_offset = (uintptr_t)slot - (uintptr_t)&allocator;
    bucket_t* _slot = (bucket_t*)((uintptr_t)&allocator + slot_offset);
    if (_slot == slot) {

        *_slot = *slot;
    }
    return;

}

FORCE_INLINE void sync_thread_pool(args_t* args) {
    bucket_t* shared_slot = args->arr[0];
    threads_t* shared_thread = args->arr[1];
    for (size_t i = 0; i < ALLOC_THREAD_POOL_SIZE - 2; i++) {
        threads_t iter = allocator.pool[i];
        if (iter.flag == 0x01) {
            // if any threads are set to 0x01, they were marked with MADV_SEQUENTIAL and MADV_MERGEABLE
            // Meaning that we are preventing stale caches, and will mitigates msync's overhead. 
            join_thread(iter, NULL);
            memset(&iter.args, 0, sizeof(args_t));
            iter.flag = 0x0;
            const int res = msync(shared_slot->arena, sizeof(arena_t), MS_SYNC);
            if (res != -1) {
                sync_allocator_buckets(shared_slot, &shared_thread->lock.mutex);
            }
            //else DBG("%d", NULL);
        }
    }

    shared_thread->flag = 0x0;
    return;
}

/**
    * @description: A free function of O(1) that syncs the arena flag and the bucket flag indicating it is free and ready to be used. 
    * @param b: bucket_t pointer that needs to be marked. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END
    * @param abs_idx: the absolute index from one of the buckets slot that is used to obtain 'b'. 
                      It is used to update the bitmap to mark 'b' as free.              
*/
FORCE_INLINE void bucket_mark_free(int abs_idx) {
    size_t size = abs_idx;
    if (size < BUCKET_SMALL_CAP) {

        allocator.bucket.small[abs_idx].flag = 0x0;
        allocator.bucket.small[abs_idx].arena->flag = 0x0;
        bitmap_clear(abs_idx, SMALL_BIT_START, SMALL_BIT_END - 1);
        return;

    }
    else if (size < BUCKET_MEDIUM_CAP && size >= BUCKET_SMALL_CAP) {

        allocator.bucket.medium[abs_idx].flag = 0x0;
        allocator.bucket.medium[abs_idx].arena->flag = 0x0;
        bitmap_clear(abs_idx, MEDIUM_BIT_START, MEDIUM_BIT_END - 1);
        return;

    }
    else {
        #if MODERN_ARCH == 1
            allocator.bucket.large[abs_idx].flag = 0x0;
            allocator.bucket.large[abs_idx].arena->flag = 0x0;
            bitmap_clear(abs_idx, LARGE_BIT_START, LARGE_BIT_END - 1);
            return;
        #endif 
    }

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
        #if LOGGING == 1

            #if LOGLEVEL == 0  

                printf("[find_free_slot] small idx: %d\n", idx);

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif

        #endif

        if (idx != -1) return (uintptr_t)allocator.bucket.small + (idx - SMALL_BIT_START) * sizeof(bucket_t);

    }
    else if (sz < BUCKET_MEDIUM_CAP) {
        idx = bitmap_find_free(MEDIUM_BIT_START, MEDIUM_BIT_END - 1);

        #if LOGGING == 1

            #if LOGLEVEL == 0 

                printf("[find_free_slot] medium idx: %d\n", idx);

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif

        #endif 

        if (idx != -1) return (uintptr_t)allocator.bucket.medium + (idx - MEDIUM_BIT_START) * sizeof(bucket_t);
    }
    else {

        #if MODERN_ARCH == 1
            idx = bitmap_find_free(LARGE_BIT_START, LARGE_BIT_END - 1);
            #if LOGGING == 1

                #if LOGLEVEL == 0 

                    printf("[find_free_slot] large idx: %d\n", idx);

                #elif LOGLEVEL > 1

                    // TODO: Include the logger variable here and its functions

                #endif
                
            #endif 

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
    uintptr_t p = (uintptr_t)ptr;
    int abs_index = -1;

    uintptr_t small_base = (uintptr_t)allocator.bucket.small;
    uintptr_t idx_bits   = (p - (uintptr_t)allocator.bucket.small[0].arena->chunk) / ARENA_SIZE;
    uintptr_t b_addr     = small_base + idx_bits * sizeof(bucket_t);
    bucket_t* b          = (bucket_t*)b_addr;

    if (b >= allocator.bucket.small &&
        b <  allocator.bucket.small + BUCKET_SMALL_CAP &&
        b->arena && p >= (uintptr_t)b->arena->chunk &&
        p <  (uintptr_t)b->arena->chunk + b->arena->size) {
        abs_index = (int)(b - allocator.bucket.small);
        if (b->flag == 0x01 && b->arena->flag == 0x01) {

            bucket_mark_free(abs_index);
            b = &allocator.bucket.small[abs_index];

        }

        return b;
    }

    idx_bits = (p - (uintptr_t)allocator.bucket.medium[0].arena->chunk) / ARENA_SIZE;
    b_addr   = (uintptr_t)allocator.bucket.medium + idx_bits * sizeof(bucket_t);
    b        = (bucket_t*)b_addr;

    if (b >= allocator.bucket.medium &&
        b <  allocator.bucket.medium + BUCKET_MEDIUM_CAP &&
        b->arena && p >= (uintptr_t)b->arena->chunk &&
        p <  (uintptr_t)b->arena->chunk + b->arena->size) {
        abs_index = (int)(b - allocator.bucket.medium);
        if (b->flag == 0x01 && b->arena->flag == 0x01) {

            bucket_mark_free(abs_index);
            b = &allocator.bucket.medium[abs_index];

        }
        return b;
    }

    #if MODERN_ARCH == 1
        idx_bits = (p - (uintptr_t)allocator.bucket.large[0].arena->chunk) / ARENA_SIZE;
        b_addr   = (uintptr_t)allocator.bucket.large + idx_bits * sizeof(bucket_t);
        b        = (bucket_t*)b_addr;

        if (b >= allocator.bucket.large &&
            b <  allocator.bucket.large + BUCKET_LARGE_CAP &&
            b->arena && p >= (uintptr_t)b->arena->chunk &&
            p <  (uintptr_t)b->arena->chunk + b->arena->size) {
            abs_index = (int)(b - allocator.bucket.large);
            if (b->flag == 0x01 && b->arena->flag == 0x01) {

                bucket_mark_free(abs_index);
                b = &allocator.bucket.large[abs_index];

            }
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
        slot->inuse[offset] = 0x0;
        slot->ua = (void*)((uintptr_t)slot->ua + (uintptr_t)offset);
        sync_allocator_buckets(slot, NULL);
    }
    #if LOGGING == 0 || LOGGING == 1
        char* res = write_long_cstr(0x01, 4, "slot->ua memory address value is: [ %p ]\n offset variable value is: %zu\n Result of adding the offset to slot->ua: %zu\n", slot->ua, offset, (uintptr_t)slot->ua + (uintptr_t)offset);
        #if LOGGING == 0
            printer.add(0, __FILE__, __LINE__, res);
            printer.print(__FILE__, __LINE__);
        #else
            logger.add(0, __FILE__, res);
        #endif
        cstr_size(1, res) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, res) : unmap_cstr(1, res);
    #endif
    return;
}

/**
    * @description: A free function that pops off a memory address that is not in use based on the requested size
    * @param slot: The slot which is bucket_t that has memory addresses to be used. 
    * @param bytes: the requested bytes 
    * @return: Returns null if bucket field is null or if bucket == slot->arena->chunk  
*/
FORCE_INLINE void* pop_from_bucket(bucket_t* slot, size_t bytes) {
    if (!slot->ua || slot->ua == slot->arena->chunk) return NULL;

    uintptr_t uint_addr = (uintptr_t)slot->ua - (uintptr_t)(alignment(bytes, bytes)); // clamp to nearest power of two of bytes
    
    void* address = (void*)(uint_addr);
    slot->ua = (void*)uint_addr;
    size_t offset = (size_t)((uintptr_t)address - (uintptr_t)slot->arena->chunk); // This also needs to be squeezed
    
    slot->inuse[offset] = 0x01;
    slot->bytes[offset] = 0;
    slot->arena = push(slot->arena, bytes);

    sync_allocator_buckets(slot, NULL);
    
    #if LOGGING == 0 || LOGGING == 1
        const int line = __LINE__;
        char* res = write_long_cstr(0x01, 3, "Unused memory address stack: [ %p ] \n Offset value is: [ %zu ]\n", slot->ua, offset);
        #if LOGGING == 0
            printer.add(0, __FILE__, line, res);
            printer.print(__FILE__, line);
        #else
            logger.add(0, __FILE__, line, res);
        #endif
        cstr_size(1, res) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, res) : unmap_cstr(1, res);
    #endif

    return address;
}

/**
    * @description: Function that returns the size where it came from 
    * @param slot: Pointer variable that possibly comes from one of the bucket's memory regions
    * @return: Returns either BUCKET_SMALL_CAP, BUCKET_MEDIUM_CAP, or BUCKET_LARGE_CAP, or returns 0
*/
FORCE_INLINE size_t find_bucket_size(const bucket_t* slot) {
    uintptr_t addr = (uintptr_t)slot;
    uintptr_t small_start  = (uintptr_t)allocator.bucket.small;
    uintptr_t small_end    = small_start  + BUCKET_SMALL_CAP  * sizeof(bucket_t);
    uintptr_t medium_start = (uintptr_t)allocator.bucket.medium;
    uintptr_t medium_end   = medium_start + BUCKET_MEDIUM_CAP * sizeof(bucket_t);
    
    if (addr >= small_start && addr < small_end)   return BUCKET_SMALL_CAP;
    if (addr >= medium_start && addr < medium_end) return BUCKET_MEDIUM_CAP;

    #if MODERN_ARCH == 1
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
    #if MODERN_ARCH == 1
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
    rc = pthread_mutex_lock(&shared_thread->lock.mutex);
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
                sync_allocator_buckets(shared_slot, &shared_thread->lock.mutex);
            }
        }

        shared_thread->flag = 0x0;
        pthread_mutex_unlock(&shared_thread->lock.mutex);

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
    rc = pthread_mutex_lock(&shared_thread->lock.mutex);
    if (rc == 0) {
        for (size_t i = *next; i < ALLOC_THREAD_POOL_SIZE; i++) { 
            if (allocator.pool[i].args.arr == NULL) {
                allocator.pool[i] = init_threads_t();
                allocator.pool[i].args.arr = malloc(2 * sizeof(void*));
            }
        }
        shared_thread->flag = 0x0;
        pthread_mutex_unlock(&shared_thread->lock.mutex);    
    }
    reset_and_free_cstr(1, next);
    #if LOGGING == 1 || LOGGING == 0
       const int line = __LINE__;
       #if LOGGING == 0
            printer.add(0, __FILE__, line, ANSI_GREEN "thread_pool_ctor: Finished initializing allocator's internal threads!\n.... Returning back to caller\n" ANSI_RESET);
            printer.print(__FILE__, line);
        #else 
            logger.add(0, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member large bucket!\n.... Returning back to caller\n" ANSI_RESET);
        #endif
    #endif
}

/**
    * @description: An external function that is used for multi-threading. It uses args_t visit variable to find out what function to visit.
                Functions that get visited will get locked and synced to avoid race conditions, while the scope of this function will join the thread.
    * @param arg: A user defined struct type called args_t that is used to visit whatever function needs to be threaded.
*/
void* thread_arguments(args_t* args) {
    
    int rc = 0;

    if (strcmp(args->visit, "arena_offset") == 0) {

        threads_t* thread = NULL;
        thread = (threads_t*)(args->arr[1]);
        
        rc = pthread_mutex_lock(&thread->lock.mutex);
        if (rc == 0)  {

            arena_offset(args);
            pthread_mutex_unlock(&thread->lock.mutex);
            join_thread(*thread, NULL);

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
        
        rc = pthread_mutex_lock(&thread->lock.mutex);
        if (rc == 0)  {

            sync_thread_pool(args);
            pthread_mutex_unlock(&thread->lock.mutex);
            join_thread(*thread, NULL);

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
        threads_t* thread = NULL;
        thread_pool_ctor(args);
        rc = pthread_mutex_lock(&allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].lock.mutex);
        if (rc == 0) {
            thread = (threads_t*)(args->arr[1]);
            pthread_mutex_unlock(&allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].lock.mutex);
        }
        join_thread(allocator.pool[ALLOC_THREAD_POOL_SIZE - 1], NULL);

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
        
        int rc;
        rc = pthread_mutex_lock(&shared_thread->lock.mutex);
        if (rc == 0) {

            const int res = msync(shared_slot->arena, sizeof(arena_t), MS_SYNC);
            if (res != -1) sync_allocator_buckets(shared_slot, &shared_thread->lock.mutex);
            pthread_mutex_unlock(&shared_thread->lock.mutex);

            #if LOGGING == 1

                #if LOGLEVEL == 0  

                    // print of the status and see if msync actually synced successfully or not 
                    // print off the rc and thread memory address and the index associated with allocator.pool 
                    
                #elif LOGLEVEL > 1

                    // TODO: Include the logger variable here and its functions

                #endif

            #endif

        }

        join_thread(*shared_thread, NULL);
        
        uint8_t* base = (uint8_t*)allocator.pool;
        if (!((uint8_t*)shared_thread < base || !((uint8_t*)shared_thread >= base + ALLOC_THREAD_POOL_SIZE))) {

            clean_threads(*shared_thread);

        }

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
FORCE_INLINE threads_t find_available_thread() {

    int rc; 
    threads_t t = {0};
    rc = pthread_mutex_lock(&allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].lock.mutex);
    if (rc == 0) {
        /*uint8_t flag = 0x0;
        threads_t* res = (threads_t*)((uintptr_t)flag - ~((uintptr_t)allocator.pool));
        if (res->flag == 0x0) {
            res->flag = 0x01;
            memcpy(&t, res, sizeof(threads_t));
            return t;
        }*/
        for (size_t _i = 0; _i < ALLOC_THREAD_POOL_SIZE; _i++) { 
            if (allocator.pool[_i].flag == 0x0) {
                allocator.pool[_i].flag = 0x01;
                return allocator.pool[_i];
            }
        }
        pthread_mutex_unlock(&allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].lock.mutex);
    }

    return t;
}

[[gnu::hot]]
//[[gnu::constructor(0)]]
// TODO: Need to make sure that MADV_MERGEABLE enabled does not consume a lot of processing power; use with care.
FORCE_INLINE void alloc_init(void) {
    int res = 0;
    if (!allocator.bits) {
        init_logger_t();
        #if MODERN_ARCH == 1
            allocator.bucket.large = shared_address(NULL, BUCKET_LARGE_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            #if LOGGING == 1 || LOGGING == 0
                if (allocator.bucket.large == MAP_FAILED) {
                    const int line = __LINE__;
                    #if LOGGING == 0
                        printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member large bucket!\n.... Returning back to caller\n" ANSI_RESET);
                        printer.print(__FILE__, line);
                    #else 
                        logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member large bucket!\n.... Returning back to caller\n" ANSI_RESET);
                    #endif
                }
            #else 
                if (allocator.bucket.large == MAP_FAILED) return;
            #endif 
            res = madvise(allocator.bucket.large, BUCKET_LARGE_CAP * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            #if LOGGING == 1 || LOGGING == 0
                if (res == -1) {
                    const int line = __LINE__;
                    #if LOGGING == 0
                        printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate modify memory region for data member large bucket!\n.... Returning back to caller\n" ANSI_RESET);
                        printer.print(__FILE__, line);
                    #else 
                        logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify memory region for data member large bucket!\n.... Returning back to caller\n" ANSI_RESET);
                    #endif
                }
            #else 
                if (res == -1) return;
            #endif
        #endif
        allocator.bits = private_address(NULL, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        #if LOGGING == 1 || LOGGING == 0
            if (allocator.bits == MAP_FAILED) {
                const int line = __LINE__;
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member bits\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member bits\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (allocator.bits == MAP_FAILED) return;
        #endif
        memset(allocator.bits, 1, BITMAP_SIZE * sizeof(uint8_t));
        res = madvise(allocator.bits, BITMAP_SIZE * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        #if LOGGING == 1 || LOGGING == 0
            if (res == -1) {
                const int line = __LINE__; 
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify memory region for data member bits\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify memory region for data member bits\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (res == -1) return;
        #endif
        allocator.bucket.small = shared_address(NULL, BUCKET_SMALL_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        #if LOGGING == 1 || LOGGING == 0
            if (allocator.bucket.small == MAP_FAILED) {
                const int line = __LINE__;
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member medium bucket!\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member medium bucket!\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (allocator.bucket.small == MAP_FAILED) return;
        #endif
        res = madvise(allocator.bucket.small, BUCKET_SMALL_CAP * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        #if LOGGING == 1 || LOGGING == 0
            if (res == -1) {
                const int line =  __LINE__; 
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify allocator's small bucket memory region with madvise!\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify allocator's small bucket memory region with madvise!\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (res == -1) return;
        #endif
        allocator.bucket.medium = shared_address(NULL, BUCKET_MEDIUM_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        #if LOGGING == 1 || LOGGING == 0
            if (allocator.bucket.medium == MAP_FAILED) {
                const int line =  __LINE__; 
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member medium bucket!\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for data member medium bucket!\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (allocator.bucket.medium == MAP_FAILED) return;
        #endif
        res = madvise(allocator.bucket.medium, BUCKET_MEDIUM_CAP * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        #if LOGGING == 1 || LOGGING == 0
            if (res == -1) {
                const int line = __LINE__; 
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify allocator's medium bucket memory region with madvise!\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify allocator's medium bucket memory region with madvise!\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (res == -1) return;
        #endif
        allocator.pool = shared_address(NULL, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        #if LOGGING == 1 || LOGGING == 0
            if (allocator.pool == MAP_FAILED) {
                const int line = __LINE__;
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to create allocator's internal threads!\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line,  ANSI_RED "alloc_init: Failed to create allocator's internal threads!\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (allocator.pool == MAP_FAILED) return;
        #endif
        res = madvise(allocator.pool, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        #if LOGGING == 1 || LOGGING == 0
            if (res == -1) {
                const int line = __LINE__;
                #if LOGGING == 0
                    printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify memory region of allocator internal threads with madvise!\n.... Returning back to caller\n" ANSI_RESET);
                    printer.print(__FILE__, line);
                #else 
                    logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to modify memory region of allocator internal threads with madvise!\n.... Returning back to caller\n" ANSI_RESET);
                #endif
            }
        #else 
            if (res == -1) return;
        #endif
        allocator.pool[ALLOC_THREAD_POOL_SIZE - 1] = init_threads_t();
        allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].args.arr = malloc(2 * sizeof(void*));
        allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].args.visit = "thread_pool_ctor";
        size_t* next = aligned_alloc(alignof(size_t), sizeof(size_t));
        *next = 0;

        allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].args.arr[0] = next;
        allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].args.arr[1] = &allocator.pool[ALLOC_THREAD_POOL_SIZE - 1];
        allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].flag = 0x01;
       
        create_thread(allocator.pool[ALLOC_THREAD_POOL_SIZE - 1], 0x01, thread_arguments);
        allocator.n_bytes = sizeof(allocator.bits);
    }
    allocator.arena = init_arena_t();
    if (!allocator.arena) {
        #if LOGGING == 0 || LOGGING == 1
            const int line = __LINE__;
            #if LOGGING == 0
                printer.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for allocator's arena!\n.... Returning back to caller\n" ANSI_RESET);
                printer.print(__FILE__, line);
            #else 
                logger.add(5, __FILE__, line, ANSI_RED "alloc_init: Failed to allocate memory for allocator's arena!\n.... Returning back to caller\n" ANSI_RESET);
            #endif
        #endif 
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
    int res = -1;  

    threads_t thread = allocator.pool[ALLOC_THREAD_POOL_SIZE - 1];
    threads_t tao = {0};
    bucket_t* slot = (bucket_t*)alloc_find_free_slot(bytes);
    
    int rc;
    rc = pthread_mutex_lock(&thread.lock.mutex);
    if (rc == 0) {
        if (thread.flag == 0x0) {
            thread.flag = 0x01; 
            thread.args.visit = "sync_threads";
            thread.args.arr[0] = slot;
            thread.args.arr[1] = &thread;
            create_thread(thread, 0x01, thread_arguments);
        }
        #if LOGGING == 0 || LOGGIN0 == 1
            const int line = __LINE__;
            #if LOGGING == 0
                printer.add(0, __FILE__, line, ANSI_GREEN "allocate: Successfully able to lock the user choice lock!\n\t rc value is: [ %d ]\n\t internal thread for allocator: 'sync_threads'\n\t flag status: %#0x" ANSI_RESET, rc, thread.flag);
                printer.print(__FILE__, line);
            #else
                logger.add(0, __FILE__, line, ANSI_GREEN "allocate: Successfully able to lock the user choice lock!\n\t rc value is: [ %d ]\n\t internal thread for allocator: 'sync_threads'\n\t flag status: %#0x" ANSI_RESET, rc, thread.flag);
            #endif
        #endif
        pthread_mutex_unlock(&thread.lock.mutex);
    }

    if (slot->offset > 0) {
        tao = find_available_thread();
        
        if (tao.thread_id == 0) {

            threads_t tmp = init_threads_t();
            memcpy(&tao, &tmp, sizeof(threads_t));
            res = madvise(&tao, sizeof(threads_t), MADV_DONTNEED);
            
            memcpy(&tao, &tmp, sizeof(threads_t));
            memset(&tmp, 0, sizeof(threads_t));
        }

        tao.args.visit = "extra_thread";
        tao.args.size = 2;
        tao.args.arr[0] = (void*)slot;
        tao.args.arr[1] = (void*)&tao;
        create_thread(thread, 0x01, thread_arguments);
        #if LOGGING == 0 || LOGGIN0 == 1
            const int line = __LINE__;
            #if LOGGING == 0
                printer.add(0, __FILE__, line, ANSI_GREEN "allocate: Successfully able to lock the user choice lock!\n\t rc value is: [ %d ]\n\t internal thread for allocator: 'extra_thread'\n\t flag status: %#0x" ANSI_RESET, rc, tao.flag);
                printer.print(__FILE__, line);
            #else
                logger.add(0, __FILE__, line, ANSI_GREEN "allocate: Successfully able to lock the user choice lock!\n\t rc value is: [ %d ]\n\t internal thread for allocator: 'extra_thread'\n\t flag status: %#0x" ANSI_RESET, rc, tao.flag);
            #endif
        #endif
    }
    
    int_fast16_t start = 0;
    int_fast16_t end = 0;
    if (slot) {
        if (!slot->arena) {
            slot->arena = allocator.arena;
            if (!slot->bytes && !slot->inuse) {
                slot->bytes = private_address(slot->bytes, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                #if LOGGING == 1 || LOGGING == 0
                    if (slot->bytes == MAP_FAILED) {
                        const int line = __LINE__;
                        #if LOGGING == 0
                            printer.add(5, __FILE__, line, "FMI");
                            printer.print(__FILE__, line);
                        #else 
                            logger.add(5, __FILE__, line, "FMI");
                        #endif
                    }
                #else 
                   if (!slot->bytes) return NULL;
                #endif
                slot->inuse = private_address(slot->inuse, BITMAP_SIZE * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                #if LOGGING == 1 || LOGGING == 0
                    if (slot->inuse == MAP_FAILED) {
                        const int line = __LINE__;
                        #if LOGGING == 0
                            printer.add(5, __FILE__, line, "FMI");
                            printer.print(__FILE__, line);
                        #else 
                            logger.add(5, __FILE__, line, "FMI");
                        #endif
                    }
                #else 
                    if (!slot->inuse) return NULL;
                #endif
                res = madvise(slot->bytes, BITMAP_SIZE * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                #if LOGGING == 1 || LOGGING == 0
                    if (res == -1) {
                        const int line = __LINE__;
                        #if LOGGING == 0
                            printer.add(5, __FILE__, line, "FMI");
                            printer.print(__FILE__, line);
                        #else 
                            logger.add(5, __FILE__, line, "FMI");
                        #endif
                    }
                #else 
                   if (res == -1) return NULL;
                #endif
                res = madvise(slot->inuse, BITMAP_SIZE * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                #if LOGGING == 1 || LOGGING == 0
                    if (res == -1) {
                        const int line = __LINE__;
                        #if LOGGING == 0
                            printer.add(5, __FILE__, line, "FMI");
                            printer.print(__FILE__, line);
                        #else 
                            logger.add(5, __FILE__, line, "FMI");
                        #endif
                    }
                #else 
                   if (res == -1) return NULL;
                #endif
            }
        }
        
        if (slot->flag != 0x01) {
            if (bytes <= SMALL_BIT_END) {
                start = SMALL_BIT_START;
                end = SMALL_BIT_END;
                address = pop_from_bucket(slot, bytes); // TODO: If data race occurs in here, then we need to pass in tao->mutex into it              
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = bitmap_find_free(SMALL_BIT_START, SMALL_BIT_END);
            }
            else if (bytes <= MEDIUM_BIT_END && bytes >= SMALL_BIT_END) {
                start = MEDIUM_BIT_START;
                end = MEDIUM_BIT_END;
                address = pop_from_bucket(slot, bytes); // TODO: If data race occurs in here, then we need to pass in tao->mutex into it 
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = bitmap_find_free(MEDIUM_BIT_START, MEDIUM_BIT_END);
            }
            else {
                #if MODERN_ARCH == 1
                    start = LARGE_BIT_START;
                    end = LARGE_BIT_END;
                    address = pop_from_bucket(slot, bytes);
                    if (address) {
                        memset(address, 0, bytes);
                        return address;
                    }
                    idx = bitmap_find_free( LARGE_BIT_START, LARGE_BIT_END);
                #endif
            }

            if (allocator.arena->flag != 0x01) {
                allocator.arena = push(allocator.arena, bytes);
                
                if (allocator.arena->flag == 0x01) {

                    allocator.bits[(idx) / CHAR_BIT] &= ~(1U << ((idx) % CHAR_BIT));
                    bitmap_set(idx, start, end);
                    slot->flag = (slot->arena && slot->arena->flag == 0x01) ? 0x01 : 0x0;
                    
                    sync_allocator_buckets(slot, NULL);
                    arena_t* full = allocator.arena;
                    allocator.arena = NULL;
                    
                    alloc_init();
                    allocator.arena->next = full;
                    return allocate(bytes);
                }

                address = allocator.arena->res;
                size_t offset = (uintptr_t)address - (uintptr_t)allocator.arena->chunk;
                slot->bytes[offset] = (size_t)(alignment(bytes, bytes)); // TODO: Data race can occur here as well, use tao->mutex and lock it if needed 
                memset(address, 0, bytes);
                return address;
            }
        }
    }
    else 
        //DBG("allocate error [ %d ]: Failed to assign memory address... printing out information\n", __LINE__);
        debug_allocator();
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
        #if LOGGING == 1 || LOGGING == 0
            char* res = write_long_cstr(0x01, 4, ANSI_RED, "ERROR ALLOCATOR.C: deallocate — ptr %p out of arena bounds\n", ANSI_RESET, ptr);
            const int line = __LINE__;
            #if LOGGING == 0
                printer.add(5, __FILE__, line, "FMI");
                printer.print(__FILE__, line);
            #else 
                logger.add(5, __FILE__, line, "FMI");
            #endif
            reset_and_free_cstr(1, res);
        #else 
            return;
        #endif
    }

    size_t offset = (size_t)((uintptr_t)ptr - (uintptr_t)slot->arena->chunk);
    if (slot->offset == slot->arena->curr) {
        threads_t thread = find_available_thread();
        // TODO: We need to check to see if the memory address is from the memory region of allocator.pool 
        if (thread.flag == 0x0) {

            thread.flag = 0x01;
            thread.args.visit = "arena_offset";
            thread.args.size = 1;
            thread.args.arr[0] = (void*)slot;
            thread.args.arr[1] = (void*)&thread;
            
            create_thread(thread, 0x01, thread_arguments);

        }
    } else slot->offset = slot->offset + offset;
    size_t bytes = slot->bytes[offset];

    if (!slot->ua) slot->ua = ptr;
    else push_to_bucket(slot, offset);
    //int rc = 0;
    
    /*rc = mprotect(ptr, bytes, PROT_READ);
    if (rc == -1) {
        printf("Failed to make memory address read only\n");
    }*/
    memset(ptr, 0xFF, bytes);
}

[[gnu::cold]]
void init_allocator_t() {
    if (!allocator.bits)  alloc_init();

    if (!allocator.allocate) {
        allocator.allocate   = allocate;
        allocator.deallocate = deallocate;
    }
}

[[gnu::cold]]
void clear_allocator() {
    for (size_t i = 0; ALLOC_THREAD_POOL_SIZE; i++) clean_threads(allocator.pool[i]);
    clear_buckets();
    munmap_address(allocator.arena->chunk, ARENA_SIZE);
    munmap_address(allocator.arena, sizeof(arena_t));
    
}