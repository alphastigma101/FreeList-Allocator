#include "allocator.h"
#include "../hash_table/hash_table.h"
#include <pthread.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

/*
#include "allocator.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
*/

typedef struct bucket_t {
    unsigned char       flag;
    unsigned char       _pad[7];
    arena_t*            arena;       
    memory_address_hash_table_t* maht;
} bucket_t;

allocator_t allocator = {0};
FORCE_INLINE threads_t find_available_thread();

/**
    * @description: A Free function that copies over the modified bucket to the global variable allocator causing it to sync properly.
    * @param slot: bucket_t pointer type that needs to be synced with allocator variable
    * @param mutex: A pointer that can be either null or not.
    * @return: None.
*/
FORCE_INLINE void __sync(bucket_t* slot, pthread_mutex_t* mutex) {
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
    if (_slot == slot) *_slot = *slot;
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
    const size_t size = abs_idx;
    if (size < BUCKET_SMALL_CAP) {
        allocator.bucket.small[abs_idx].flag = 0x0;
        allocator.bucket.small[abs_idx].arena->flag = 0x0;
        allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, abs_idx, SMALL_BIT_START, SMALL_BIT_END - 1);
        return;
    }
    else if (size < BUCKET_MEDIUM_CAP && size >= BUCKET_SMALL_CAP) {

        allocator.bucket.medium[abs_idx].flag = 0x0;
        allocator.bucket.medium[abs_idx].arena->flag = 0x0;
        allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, abs_idx, MEDIUM_BIT_START, MEDIUM_BIT_END - 1);
        return;
    }
    else {
        allocator.bucket.large[abs_idx].flag = 0x0;
        allocator.bucket.large[abs_idx].arena->flag = 0x0;
        allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, abs_idx, LARGE_BIT_START, LARGE_BIT_END - 1);
        return;
    }

}

/**
    * @description: A Free function that finds a free slot based on the size at O(n). 
    * @param sz: can be a numeric value of size: 64, 128, or 256. 
    * @return: Returns a slot right after checking the bucket's bitmap.
    * @note: if nothing is returned, that means all of the slots from small, medium and large are occupied. 
*/
FORCE_INLINE bucket_t* alloc_find_free_slot(size_t sz) {
    int idx = -1;

    if (sz < BUCKET_SMALL_CAP) {
        idx = allocator.bitmap.bitmap_test(allocator.bitmap, SMALL_BIT_START, SMALL_BIT_END - 1);
        #if LOGGING == 1

            #if LOGLEVEL == 0  

                printf("[find_free_slot] small idx: %d\n", idx);

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif

        #endif

        if (idx != -1) return (bucket_t*)((uintptr_t)allocator.bucket.small + (idx - SMALL_BIT_START) * sizeof(bucket_t));

    }
    else if (sz < BUCKET_MEDIUM_CAP) {
        idx = allocator.bitmap.bitmap_test(allocator.bitmap, MEDIUM_BIT_START, MEDIUM_BIT_END - 1);

        #if LOGGING == 1

            #if LOGLEVEL == 0 

                printf("[find_free_slot] medium idx: %d\n", idx);

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif

        #endif 

        if (idx != -1) return (bucket_t*)((uintptr_t)allocator.bucket.medium + (idx - MEDIUM_BIT_START) * sizeof(bucket_t));
    }
    else {
        idx = allocator.bitmap.bitmap_test(allocator.bitmap, LARGE_BIT_START, LARGE_BIT_END - 1);
        #if LOGGING == 1

            #if LOGLEVEL == 0 

                printf("[find_free_slot] large idx: %d\n", idx);

            #elif LOGLEVEL > 1

                // TODO: Include the logger variable here and its functions

            #endif
            
        #endif 

        if (idx != -1) return (bucket_t*)((uintptr_t)allocator.bucket.large + (idx - LARGE_BIT_START) * sizeof(bucket_t));
    }

    return NULL;
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
        p <  (uintptr_t)b->arena->chunk + ARENA_SIZE) {
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
        p <  (uintptr_t)b->arena->chunk + ARENA_SIZE) {
        abs_index = (int)(b - allocator.bucket.medium);
        if (b->flag == 0x01 && b->arena->flag == 0x01) {
            bucket_mark_free(abs_index);
            b = &allocator.bucket.medium[abs_index];
        }
        return b;
    }

    idx_bits = (p - (uintptr_t)allocator.bucket.large[0].arena->chunk) / ARENA_SIZE;
    b_addr   = (uintptr_t)allocator.bucket.large + idx_bits * sizeof(bucket_t);
    b        = (bucket_t*)b_addr;

    if (b >= allocator.bucket.large &&
        b <  allocator.bucket.large + BUCKET_LARGE_CAP &&
        b->arena && p >= (uintptr_t)b->arena->chunk &&
        p <  (uintptr_t)b->arena->chunk + ARENA_SIZE) {
        abs_index = (int)(b - allocator.bucket.large);
        if (b->flag == 0x01 && b->arena->flag == 0x01) {
            bucket_mark_free(abs_index);
            b = &allocator.bucket.large[abs_index];
        }
        return b;
    }
    return NULL;
}

/**
    * @description: A Free Function that pushes the unused memory addresses to bucket.
        It is used with deallocation function
    * @param b: A specific bucket that will now have been updated 
*/
FORCE_INLINE void push_to_bucket(bucket_t* slot, size_t offset) {
    if (offset != 0 && offset != 1) {
        slot->maht = update(slot->maht, offset, 0, 0x0);
        __sync(slot, NULL);
    }
    return;
}

/**
    * @description: A free function that pops off a memory address that is not in use based on the requested size
    * @param slot: The slot which is bucket_t that has memory addresses to be used. 
    * @param bytes: the requested bytes 
    * @return: Returns null if bucket field is null or if bucket == slot->arena->chunk  
*/
FORCE_INLINE void* pop_from_bucket(bucket_t* slot, size_t bytes) {
    byte_entries_t* tmp = get_entry_t_by_bytes(slot->maht, bytes, 0x0);
    if (!tmp || tmp->ptr == NULL) return NULL;
    void* address = NULL;
    if (tmp) {
        slot->maht = update(slot->maht, tmp->offset->offset, 0, 0x01);
        slot->arena = push(slot->arena, bytes);
        address = tmp->ptr;
        __sync(slot, NULL);
    }
    /*#if LOGGING == 0 || LOGGING == 1
        const int line = __LINE__;
        char* res = write_long_cstr(0x01, 1, "Entry variable is: [ %p ]\n", tmp);
        #if LOGGING == 0
            printer.add(0, __FILE__, line, res);
            printer.print(__FILE__, line);
        #else
            logger.add(0, __FILE__, line, res);
        #endif
        cstr_size(1, res) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, res) : unmap_cstr(1, res);
    #endif*/
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
    unsigned int small = 0;
    unsigned int medium = 0;
    while (small < 64) {
        arena_t* arena = allocator.bucket.small[small].arena;
        bucket_t slot = allocator.bucket.small[small];
        if (slot.maht) clean(slot.maht);
        //while (arena->next) {
            if (arena) {
                if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE);
                munmap_address(arena, sizeof(arena_t));
            }
            //arena_t* prev = arena;
            //arena = arena->next;
            //munmap_address(prev, sizeof(arena_t));
            
        //}
        small = small + 1;
    }
    munmap_address(allocator.bucket.small, BUCKET_SMALL_CAP * sizeof(bucket_t));
    while (medium < 128) {
        arena_t* arena = allocator.bucket.medium[medium].arena;
        bucket_t slot = allocator.bucket.medium[medium];
        clean(slot.maht);
        //while (arena->next) {
            if (arena) {
                if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE);
                munmap_address(arena, sizeof(arena_t));
            }
            //arena_t* prev = arena;
            //arena = arena->next;
            //munmap_address(prev, sizeof(arena_t));
        //}
        medium = medium + 1;
    }
    munmap_address(allocator.bucket.medium, BUCKET_MEDIUM_CAP * sizeof(bucket_t));
    unsigned int large = 0;
    while (large < 256) {
        arena_t* arena = allocator.bucket.large[large].arena;
        bucket_t slot = allocator.bucket.large[large];
        if (slot.maht) clean(slot.maht);
        if (arena) {
            if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE);
            munmap_address(arena, sizeof(arena_t));
        }
        //while (arena->next) {
            //munmap_address(arena->chunk, ARENA_SIZE);
            //arena_t* prev = arena;
            //arena = arena->next;
            //munmap_address(prev, sizeof(arena_t));
        //}
        large = large + 1;
    }
    munmap_address(allocator.bucket.large, BUCKET_LARGE_CAP * sizeof(bucket_t));
}

FORCE_INLINE void __rewind(bucket_t* slot) {
    // arena->prev holds the offset of the topmost (most recently pushed)
    // allocation -- push() sets it directly, so we search offset_map with
    // prev as the key. get_entry_t_by_offset filters by inuse==0x0 itself,
    // so if the topmost entry is still alive, onode comes back NULL and
    // we stop there -- never cascading past a live allocation.
    offset_entries_t* onode = get_entry_t_by_offset(slot->maht, slot->arena->prev, 0x0);
    for (;;) {
        // Following the procedures of a stack which is FILO --
        // revert to a safe checkpoint, one freed byteset offset at a time.
        if (!onode) break;
        unsigned int freed_bytes = onode->bytes->bytes;
        slot->maht = destroy(slot->maht, onode->offset, 0);
        slot->arena = pop(slot->arena, freed_bytes);      // pop() subtracts by size, correctly shrinking both curr and prev
        onode = get_entry_t_by_offset(slot->maht, slot->arena->prev, 0x0);
    }
    // onode is guaranteed NULL here -- the loop only ever exits via `!onode`.
    // Check arena state instead of dereferencing it.
    if (slot->arena->prev <= 10) {
        munmap_address(slot->arena->chunk, ARENA_SIZE);
        munmap_address(slot->arena, sizeof(arena_t));
        return;
    }
    __sync(slot, NULL);
}

[[gnu::hot]]
FORCE_INLINE threads_t find_available_thread() {

    int rc; 
    threads_t t = {0};
    memset(&t, -1, sizeof(threads_t));
    rc = pthread_mutex_lock(&allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].lock.mutex);
    if (rc == 0) {
        threads_t* res = (threads_t*)((uintptr_t)allocator.pool + ((uintptr_t)offsetof(threads_t, flag) * sizeof(threads_t)));
        if (res->flag == 0x0) {
            res->flag = 0x01;
            memcpy(&t, res, sizeof(threads_t));
        }
        pthread_mutex_unlock(&allocator.pool[ALLOC_THREAD_POOL_SIZE - 1].lock.mutex);
    }

    return t;
}

[[gnu::hot]]
// TODO: Need to make sure that MADV_MERGEABLE enabled does not consume a lot of processing power; use with care.
FORCE_INLINE void alloc_init(void) {
    int res = 0;
    if (!allocator.bucket.small) {
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
    //DBG("allocate error [ %d ]: Failed to assign memory address... printing out information\n", __LINE__);
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
    bucket_t* slot = (bucket_t*)alloc_find_free_slot(bytes);
    if (slot) {
        int_fast16_t start = 0;
        int_fast16_t end = 0;
        if (!slot->arena) slot->arena = allocator.arena;
        if (slot->flag != 0x01) {
            if (bytes <= SMALL_BIT_END) {
                start = SMALL_BIT_START;
                end = SMALL_BIT_END;
                address = pop_from_bucket(slot, bytes);             
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = allocator.bitmap.bitmap_test(allocator.bitmap, SMALL_BIT_START, SMALL_BIT_END);
            }
            else if (bytes <= MEDIUM_BIT_END && bytes >= SMALL_BIT_END) {
                start = MEDIUM_BIT_START;
                end = MEDIUM_BIT_END;
                address = pop_from_bucket(slot, bytes); 
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = allocator.bitmap.bitmap_test(allocator.bitmap, MEDIUM_BIT_START, MEDIUM_BIT_END);
            }
            else {
                start = LARGE_BIT_START;
                end = LARGE_BIT_END;
                address = pop_from_bucket(slot, bytes);
                if (address) {
                    memset(address, 0, bytes);
                    return address;
                }
                idx = allocator.bitmap.bitmap_test(allocator.bitmap, LARGE_BIT_START, LARGE_BIT_END);
            }

            if (allocator.arena->flag != 0x01 && idx != -1) {
                allocator.arena = push(allocator.arena, bytes);
                if (allocator.arena->flag == 0x01) {
                    slot->flag = 0x01;
                    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, idx, start, end);
                    arena_t* full = allocator.arena;
                    allocator.arena = NULL;
                    alloc_init();
                    allocator.arena->next = full;
                    __sync(slot, NULL);
                    return allocate(bytes);
                }
                address = allocator.arena->res;
                size_t offset = (uintptr_t)slot->arena->res - (uintptr_t)allocator.arena->chunk;
                slot->maht = set(slot->maht, offset, bytes, 0x01, address);
                memset(address, 0, bytes);
                return address;
            }
        }
    }
    else debug_allocator();
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
    if (!ptr) return;

    bucket_t* slot = NULL;
    slot = find_slot(ptr);
    if (!slot) return;

    unsigned char* base = (unsigned char*)slot->arena->chunk;
    if ((unsigned char*)ptr < base || (unsigned char*)ptr >= base + ARENA_SIZE) {
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
    offset_entries_t* bytes = get_entry_t_by_offset(slot->maht, offset, 0x01);
    if (bytes) {
        push_to_bucket(slot, offset);
        memset(ptr, 0xFF, bytes->bytes->bytes);
        __rewind(slot);
    }
}

[[gnu::cold]]
void init_allocator_t() {
    if (!allocator.bitmap.bits)  {
        init_logger_t();
        allocator.bitmap = init_bitmap_t(BITMAP_SIZE);
        alloc_init();
    }

    if (!allocator.allocate) {
        allocator.allocate   = allocate;
        allocator.deallocate = deallocate;
    }
}

[[gnu::cold]]
[[gnu::destructor]]
FORCE_INLINE void allocator_dctor() {
    for (unsigned int i = 0;  i < ALLOC_THREAD_POOL_SIZE; i++) {
        clean_threads(allocator.pool[i]);
    }
    munmap_address(allocator.pool, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t));
    clear_buckets();
    if (allocator.arena) {
        //if (allocator.arena->chunk) munmap_address(allocator.arena->chunk, ARENA_SIZE);
        munmap_address(allocator.arena, sizeof(arena_t));
    }
    clean_bitmap(allocator.bitmap);   
}