#include "allocator.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>


allocator_t* allocator = NULL;
FORCE_INLINE void thread_pool_ctor_range(size_t next);

/**
    * @description: A Free function that uses a specific bucket index to update its state. 
    * @param idx: the specific parameter determined from an index from allocator->bucket.small, medium and or large. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END 
    * @return: Returns 0 if out of bounds, otherwise returns 1
    * @note: As of 3/24/26, bit map values are not being set to either one or zero. This is still a bug that will eventually be fixed.
*/
FORCE_INLINE int bitmap_set(int idx, int start, int end) {
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
FORCE_INLINE int bitmap_clear(int idx, int start, int end) {
    if (idx < start || idx > end) return 0;
    allocator->map.bits[idx / CHAR_BIT] &= ~(1U << (idx % CHAR_BIT));
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
FORCE_INLINE int bitmap_test(bitmap_t bm, int idx, int start, int end) {
    if (idx < start || idx > end) return 0;
    return (bm.bits[idx / CHAR_BIT] & (1U << (idx % CHAR_BIT))) != 0;
}

/**
    * @description: A free function that tests to see which index is open. odd numbers the bucket is not full, even numbers bucket is full.
    * @param bm: the bitmap that is integrated into the allocator variable. 
    * @param start: Is a macro value. it can be: SMALL_BIT_START, MEDIUM_BIT_START, or LARGE_BIT_START
    * @param end: Is a macro value. It can be: SMALL_BIT_END, MEDIUM_BIT_END, or LARGE_BIT_END
    * @return: Returns negative one to indicate the partition is full.
*/
FORCE_INLINE int bitmap_find_free(bitmap_t bm, int start, int end) {
    for (int i = start; i <= end; i++) {
        if (bitmap_test(bm, i, start, end)) return i;
    }
    return -1;
}

/**
    * @description: A Free function that copies over the modified bucket to the global variable allocator causing it to sync properly.
    * @param slot: bucket_t pointer type that needs to be synced with allocator variable
    * @return: None.
*/
FORCE_INLINE void sync_allocator_buckets(bucket_t* slot) {
    uintptr_t slot_offset = (uintptr_t)slot - (uintptr_t)allocator;
    bucket_t* _slot = (bucket_t*)((uintptr_t)allocator + slot_offset);
    *_slot = *slot;
}

FORCE_INLINE void sync_thread_pool(bucket_t* slot) {
    for (size_t i = 0; i < ALLOC_THREAD_POOL_SIZE - 2; i++) {
        threads_t* iter = allocator->pool + i;
        if (iter->flag == 0x01) {
            // if any threads are set to 0x01, they were marked with MADV_SEQUENTIAL and MADV_MERGEABLE
            // Meaning that we are preventing stale caches, and will mitigates msync's overhead. 
            join_thread(iter->thread_id, NULL);
            iter->flag = 0x0;
            memset(iter->args->visit, 0, sizeof(char*));
            const int res = msync(slot->arena, sizeof(arena_t), MS_SYNC);
            sync_allocator_buckets(slot);
            if (!res) 
                DBG("%d", res);
        }
    }
    threads_t* thread = (threads_t*)((uintptr_t)allocator->pool + ALLOC_THREAD_POOL_SIZE - 1);
    join_thread(thread->thread_id, NULL);
    thread->flag = 0x0;
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
    if (sz <= BUCKET_SMALL_MAX) {
        idx = bitmap_find_free(allocator->map, SMALL_BIT_START, SMALL_BIT_END);
        if (idx != -1) return (uintptr_t)allocator + offsetof(allocator_t, bucket.small) + (idx - SMALL_BIT_START) * sizeof(bucket_t);
    }
    else if (sz <= BUCKET_MEDIUM_MAX) {
        idx = bitmap_find_free(allocator->map, MEDIUM_BIT_START, MEDIUM_BIT_END);
        if (idx != -1) return (uintptr_t)allocator + offsetof(allocator_t, bucket.medium) + (idx - MEDIUM_BIT_START) * sizeof(bucket_t);
    }
    #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
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
    char* _ptr = (char*)ptr;
    
    for (int i = SMALL_BIT_START; i < SMALL_BIT_END; i++) {
        bucket_t* b = (bucket_t*)((uintptr_t)allocator + offsetof(allocator_t, bucket.small) + (i - SMALL_BIT_START) * sizeof(bucket_t));
        if (b->arena && _ptr >= (char*)b->arena->chunk && _ptr < (char*)b->arena->chunk + b->arena->size) {
            int abs_idx = i - SMALL_BIT_START;
            if (bucket_mark_full(b, SMALL_BIT_START, SMALL_BIT_END, abs_idx))
                bucket_mark_free(b, SMALL_BIT_START, SMALL_BIT_END, abs_idx);
            return b;
        }
    }

    for (int i = MEDIUM_BIT_START; i < MEDIUM_BIT_END; i++) {
        bucket_t* b = (bucket_t*)((uintptr_t)allocator + offsetof(allocator_t, bucket.medium) + (i - MEDIUM_BIT_START) * sizeof(bucket_t));
        if (b->arena && _ptr >= (char*)b->arena->chunk && _ptr < (char*)b->arena->chunk + b->arena->size) {
            int abs_idx = i - MEDIUM_BIT_START;
            if (bucket_mark_full(b, MEDIUM_BIT_START, MEDIUM_BIT_END, abs_idx))
                bucket_mark_free(b, MEDIUM_BIT_START, MEDIUM_BIT_END, abs_idx);
            return b;
        }
    }
    #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
        for (int i = LARGE_BIT_START; i < LARGE_BIT_END; i++) {
            bucket_t* b = (bucket_t*)((uintptr_t)allocator + offsetof(allocator_t, bucket.large) + (i - LARGE_BIT_START) * sizeof(bucket_t));
            if (b->arena && _ptr >= (char*)b->arena->chunk && _ptr < (char*)b->arena->chunk + b->arena->size) {
                int abs_idx = i - LARGE_BIT_START;
                if (bucket_mark_full(b, LARGE_BIT_START, LARGE_BIT_END, abs_idx))
                    bucket_mark_free(b, LARGE_BIT_START, LARGE_BIT_END, abs_idx);
                return b;
            }
        }
    #endif

    return NULL;
}

/**
    * @description: A Free Function that pushes the unused memory addresses to bucket.
    * @param b: A specific bucket that will now have updated 
*/
FORCE_INLINE void push_to_bucket(bucket_t* b, size_t offset) {
    if (offset != 0 && offset != 1) {
        uintptr_t extend_bucket = (uintptr_t)b->arena->chunk + offset;
        b->unused_addresses = (uint8_t*)((uintptr_t)b->unused_addresses + (uintptr_t)extend_bucket);
        sync_allocator_buckets(b);
    }
}

/**
    * @description: A free function that pops off a memory address that is not in use based on the requested size
    * @param slot: The slot which is bucket_t that has memory addresses to be used. 
    * @param bytes: the requested bytes 
    * @return: Returns null if bucket field is null or if bucket == slot->arena->chunk  
*/
FORCE_INLINE void* pop_from_bucket(bucket_t* slot, size_t bytes) {
    if (!slot->unused_addresses || slot->unused_addresses == slot->arena->chunk) return NULL;

    uintptr_t bucket = (uintptr_t)slot->unused_addresses - bytes; 
    void* address = (void*)(bucket);
    slot->unused_addresses = (uint8_t*)bucket;
    size_t offset = (size_t)((uintptr_t)address - (uintptr_t)slot->arena->chunk);
    slot->entries.inuse[offset] = 0x01;
    slot->entries.bytes[offset] = 0;
    slot->arena = push(slot->arena, bytes);
    sync_allocator_buckets(slot);
    return address;
}

FORCE_INLINE void* arena_offset(bucket_t* slot) {
    size_t offset = 0;
    for (size_t i = 0; i < ARENA_SIZE; i++) {
        if (slot->entries.inuse[i] == 0x0 && slot->entries.bytes[offset] != 0) offset = offset + i;
    }
    if (offset == slot->arena->curr) { 
        clear_arena_t(slot->arena);
        munmap_address(slot->arena);
        munmap_address(slot->entries.bytes);
        munmap_address(slot->entries.inuse);
    }
    else {
        if (slot->arena->curr != 0) {
            slot->arena->curr -= offset;
            slot->arena->prev -= offset;
            slot->offset = 0;
            sync_allocator_buckets(slot);
        }
    }
    return NULL;
}

void* thread_arguments(args_t* args) {
    if (strcmp(args->visit, "arena_offset") != 0) {
        bucket_t* slot = args->arr[0];
        arena_offset(slot);
    }
    else if (strcmp(args->visit, "sync_threads") != 0) {
        bucket_t* slot = (bucket_t*)(args->arr[0]);
        sync_thread_pool(slot);
        threads_t* thread = allocator->pool + ALLOC_THREAD_POOL_SIZE - 1;
        thread->flag = 0x0;
        join_thread(thread->thread_id, NULL);
    }
    else if (strcmp(args->visit, "internal_allocator_thread_pool") != 0) {
        size_t next = (size_t)args->arr[0];
        thread_pool_ctor_range(next);
    }
    else if (strcmp(args->visit, "extra_thread") != 0) {
        bucket_t* slot = args->arr[0];
        threads_t* thread = args->arr[1];
        arena_offset(slot);
        const int res = msync(slot->arena, sizeof(arena_t), MS_SYNC);
        sync_allocator_buckets(slot);
        join_thread(thread->thread_id, NULL);
        munmap_address(thread);
        memset(thread, 0, sizeof(threads_t));
    }
    return NULL;
}


[[gnu::hot]]
FORCE_INLINE void thread_pool_ctor_range(size_t next) {
    if (next != ALLOC_THREAD_POOL_SIZE) { 
        threads_t* t = &allocator->pool[next];
        threads_t* tmp = init_threads_t();
        memmove(t, tmp, sizeof(threads_t));
        free(tmp->args);
        free(tmp);
        memset(tmp, 0, sizeof(threads_t));
        return thread_pool_ctor_range(next + 1);
    }
    
    return;
}


// embedded system:
            // Allocator = 56 bytes 
            // bucket_t = 48 bytes
            // allocator->bucket.small = 64 * 48 = 3072
            // allocator->bucket.medium = 192 * 48 = 9216
            // allocator->map.bits = 192 bytes
            // allocator->map.inuse = 192 bytes
                // total = 12.7KB
[[gnu::hot]]
//[[gnu::constructor(0)]]
// TODO: Need to make sure that MADV_MERGEABLE enabled does not consume a lot of processing power; use with care.
FORCE_INLINE void alloc_init(void) {
    int res = 0;
    if (!allocator) {
        allocator = private_address(allocator, sizeof(allocator_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (allocator == MAP_FAILED) {
            fprintf(stderr, "ERROR: Failed to allocate allocator\n");
            return;
        }
        memset(allocator, 0, sizeof(allocator_t));
        #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
            allocator->map.bits = private_address(allocator->map.bits, 448 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            allocator->bucket.large = private_address(allocator->bucket.large, 256 * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            memset(allocator->map.bits, 1, 448 * sizeof(uint8_t));
            res = madvise(allocator->map.bits, 448 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) DBG("%d", res);
            res = madvise(allocator->bucket.large, 256 * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) DBG("%d", res);
        #else 
            allocator->map.bits = private_address(allocator->map.bits, 192 * sizeof(bitmap_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            memset(allocator->map.bits, 1, 192 * sizeof(uint8_t));
            res = madvise(allocator->map.bits, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) DBG("%d", res);
        #endif
        allocator->bucket.small = private_address(allocator->bucket.small, 64 * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        res = madvise(allocator->bucket.small, 64 * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) DBG("%d", res);
        allocator->bucket.medium = private_address(allocator->bucket.medium, 128 * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        res = madvise(allocator->bucket.medium, 128 * sizeof(bucket_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) DBG("%d", res);
        allocator->pool = private_address(allocator->pool, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        res = madvise(allocator->pool, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) DBG("%d", res);
        thread_pool_ctor_range(ALLOC_THREAD_POOL_SIZE - 1);
        threads_t* thread = &allocator->pool[ALLOC_THREAD_POOL_SIZE - 1];
        thread->args->visit = "internal_allocator_thread_pool";
        size_t next = 0;
        thread->args->arr = (void**)&next;
        thread = create_thread_attr(thread, 0x01);
        thread = create_thread(thread, 0x01, thread_arguments);
        allocator->map.n_bytes = sizeof(allocator->map.bits);
    }

    allocator->arena = init_arena_t();
    if (!allocator->arena) {
        fprintf(stderr, "ERROR: Failed to init arena\n");
        munmap_address(allocator);
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
[[gnu::hot]]
FORCE_INLINE void* allocate(size_t bytes) {
    char* address = NULL;
    int idx = -1; 
    int res = 0;

    threads_t* thread = &allocator->pool[ALLOC_THREAD_POOL_SIZE - 1];
    bucket_t* slot = (bucket_t*)alloc_find_free_slot(bytes);
    
    if (thread->flag == 0x0) {
        if (slot->offset > 0) {
            threads_t* tao = NULL;
            FIND_AVAILABLE_THREAD(allocator, tao);
            if (tao == NULL) {
                threads_t* tmp = init_threads_t();
                tao = private_address(tao, sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);;
                res = madvise(tao, sizeof(threads_t), MADV_DONTNEED);
                memmove(tao, tmp, sizeof(threads_t));
                free(tmp);
                memset(tmp, 0, sizeof(threads_t));
            }
            tao->args->visit = "extra_thread";
            tao->args->size = 2;
            tao->args->arr = (void*)slot;
            tao->args->arr = (void**)((uintptr_t)tao->args->arr[0] + (uintptr_t)tao);
            tao = create_thread_attr(thread, 0x01);
            tao = create_thread(thread, 0x01, thread_arguments(thread->args));
        }
        thread->flag = 0x01; 
        thread->args->visit = "sync_threads";
        thread = create_thread_attr(thread, 0x01);
        thread = create_thread(thread, 0x01, thread_arguments(thread->args));
        
    }

    if (slot) {
        if (!slot->arena && allocator->arena->curr == 0) {
            slot->arena = allocator->arena;
            #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS) 
                slot->entries.bytes = private_address(slot->entries.bytes, 448 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                slot->entries.inuse = private_address(slot->entries.inuse, 448 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                res = madvise(slot->entries.bytes, 448 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
                res = madvise(slot->entries.inuse, 448 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
            #else
                slot->entries.bytes = private_address(slot->entries.bytes, 192 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                slot->entries.inuse = private_address(slot->entries.inuse, 192 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                res = madvise(slot->entries.bytes, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
                res = madvise(slot->entries.inuse, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
            #endif
        }
        else {
            #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS) 
                slot->entries.bytes = private_address(slot->entries.bytes, 448 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                slot->entries.inuse = private_address(slot->entries.inuse, 448 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                res = madvise(slot->entries.bytes, 448 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
                res = madvise(slot->entries.inuse, 448 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
            #else
                slot->entries.bytes = private_address(slot->entries.bytes, 192 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                slot->entries.inuse = private_address(slot->entries.inuse, 192 * sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                res = madvise(slot->entries.bytes, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
                res = madvise(slot->entries.inuse, 192 * sizeof(uint8_t), MADV_SEQUENTIAL | MADV_MERGEABLE | MADV_DONTNEED);
                if (res == -1) DBG("%d", res);
            #endif
            slot->arena = init_arena_t();
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
                #if (DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS)
                    address = pop_from_bucket(slot, bytes);
                    if (address) {
                        memset(address, 0, bytes);
                        return address;
                    }
                    idx = bitmap_find_free(allocator->map, LARGE_BIT_START, LARGE_BIT_END);
                #endif
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
//[[gnu::hot]]
FORCE_INLINE void deallocate(void* ptr) {
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
    if (slot->offset == slot->arena->curr) {
        threads_t* thread = NULL;
        FIND_AVAILABLE_THREAD(allocator, thread);
        if (thread) {
            thread->args->visit = "arena_offset";
            thread->args->size = 1;
            thread->args->arr = (void*)slot;
            thread = create_thread_attr(thread, 0x01);
            thread = create_thread(thread, 0x01, thread_arguments(thread->args));
        }
    } else slot->offset = slot->offset + offset;
    size_t bytes = slot->entries.bytes[offset];

    if (!slot->unused_addresses) slot->unused_addresses = ptr;
    else push_to_bucket(slot, offset);
    memset(ptr, 0xFF, bytes);

}

[[gnu::cold]]
FORCE_INLINE void clear_buckets() {
    size_t small = 0;
    size_t medium = 0;
    while (small < 64) {
        arena_t* arena = allocator->bucket.small[small].arena;
        while (arena->next) {
            munmap_address(arena);
            munmap_address(arena->chunk);
            arena = arena->next;
        }
        small = small + 1;
    }
    while (medium < 128) {
        arena_t* arena = allocator->bucket.medium[medium].arena;
        while (arena->next) {
            munmap_address(arena);
            munmap_address(arena->chunk);
            arena = arena->next;
        }
        medium = medium + 1;
    }
    #if DEFAULT_ALIGNMENT > EMBEDDED_SYSTEMS
        size_t large = 0;
        while (large < 256) {
            arena_t* arena = allocator->bucket.large[large].arena;
            while (arena->next) {
                munmap_address(arena);
                munmap_address(arena->chunk);
                arena = arena->next;
            }
            large = large + 1;
        }
    #endif
}

[[gnu::cold]]
void clear_allocator() {
    for (size_t i = 0; ALLOC_THREAD_POOL_SIZE; i++) clean_threads(&allocator->pool[i]);
    clear_buckets();
    munmap_address(allocator->arena);
    munmap_address(allocator->arena->chunk);
    munmap_address(allocator);
}

void init_allocator_t() {
    if (!allocator)  alloc_init();

    if (!allocator->allocate && !allocator->deallocate) {
        allocator->allocate   = allocate;
        allocator->deallocate = deallocate;
    }
}


arena_t* init_arena_t() {
    arena_t* arena = NULL;
    int res = 0;
    arena = private_address(arena, sizeof(arena_t), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    res = madvise(arena, sizeof(arena_t), MADV_MERGEABLE);
    if (!arena || res == -1) {
        printf("ERROR 10 IN ARENA.C, FAILED TO ALLOCATE MEMORY FOR ARENA!\n");
        return NULL;
    }
    memset(arena, 0, sizeof(arena_t));
    arena->chunk = shared_address(arena->chunk, ARENA_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS , -1, 0);
    res = madvise(arena->chunk, ARENA_SIZE, MADV_SEQUENTIAL);
    if (!arena->chunk || res == -1) {
        printf("ERROR 15 IN ARENA.C, FAILED TO ALLOCATE A CHUNK OF MEMORY!\n");
        free(arena);
        return NULL;
    }
    memset(arena->chunk, 0, ARENA_SIZE);
    arena->size = ARENA_SIZE;
    arena->curr = 1;
    return arena;
}