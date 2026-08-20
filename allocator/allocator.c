#include "allocator.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#if BENCHMARK_ENV == 1
    FORCE_INLINE void init_benchmark_allocator_t();
    benchmark_allocator_t benchmark_allocator = {0};
#endif
/*
#include "allocator.h"
#include <stdalign.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
*/


typedef struct byte_entries_t {
    void* ptr;
    struct offset_entries_t* offset;
    struct byte_entries_t* next;
    size_t                 bytes;
    unsigned char inuse;
} byte_entries_t;

typedef struct offset_entries_t {
    struct byte_entries_t* bytes;
    struct offset_entries_t* next;
    size_t        offset;
    unsigned char inuse;
} offset_entries_t;

typedef struct entry_table_t {
    offset_entries_t*** offset_entries;
    byte_entries_t*** byte_entries;
    size_t*          inner_count;
    size_t           bucket_count; /* distance of arena->next is determined by this field member. */
    size_t           depth; /* gets incremented to use other slots */
} entry_table_t;


/////////////////////////
// ENTRY TABLE SECTION //
////////////////////////

FORCE_INLINE void entry_table_inner_init(entry_table_t* table, size_t idx, const unsigned char mode);
FORCE_INLINE void entry_table_resize_inner(entry_table_t* table, const size_t idx, const unsigned char mode);
FORCE_INLINE void table_entry_free_pages(entry_table_t* table);

FORCE_INLINE size_t hash_mix(size_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/** 
    * @description: Free function that sets the size of the outer and inner 
    * @param table: the hash user defined function
    * @param idx: idx can be: [0, BITMAP_SIZE - 1] or [0, (BITMAP_SIZE + MAX_HUGE_SLOTS) - 1]
    * @return: Nothing
*/
FORCE_INLINE void entry_table_init(entry_table_t* table, const size_t idx) {
    const unsigned char is_regular = (!RAM_TIER_ULTRA_CONSTRAINED && !RAM_TIER_EMBEDDED) || (idx < BITMAP_SIZE);

    const size_t outer_count   = is_regular ? 128 : 8;
    const size_t initial_inner = is_regular ? 64 : 8;

    const size_t st_entries = outer_count * sizeof(offset_entries_t**);
    table->offset_entries = shared_address(NULL, st_entries, PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
    if (table->offset_entries == MAP_FAILED) { table->offset_entries = NULL; return; }
    else {
        int res = madvise(table->offset_entries, st_entries, MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) { munmap_address(table->offset_entries, st_entries, __FILE__,  __LINE__); table->offset_entries = NULL; return; }
    }

    table->byte_entries = shared_address(NULL, outer_count * sizeof(byte_entries_t**), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
    if (table->byte_entries == MAP_FAILED) { table->byte_entries = NULL; return; }
    else {
        int res = madvise(table->byte_entries, outer_count * sizeof(byte_entries_t**), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) { 
            munmap_address(table->byte_entries, outer_count * sizeof(byte_entries_t**), __FILE__,  __LINE__);
            munmap_address(table->offset_entries, st_entries, __FILE__,  __LINE__); 
            table->offset_entries = NULL; 
            table->byte_entries = NULL; 
            return; 
        }
    }

    const size_t st_inner_count = outer_count * sizeof(size_t);
    table->inner_count = shared_address(NULL, st_inner_count, PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
    if (table->inner_count == MAP_FAILED) {
        munmap_address(table->offset_entries, st_entries, __FILE__,  __LINE__);
        munmap_address(table->byte_entries, outer_count * sizeof(byte_entries_t**), __FILE__,  __LINE__);
        table->byte_entries = NULL; 
        table->offset_entries = NULL;
        return;
    }
    else {
        int res = madvise(table->inner_count, st_inner_count, MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) {
            munmap_address(table->offset_entries, st_entries, __FILE__,  __LINE__);
            munmap_address(table->byte_entries, outer_count * sizeof(byte_entries_t**), __FILE__,  __LINE__);
            munmap_address(table->inner_count, st_inner_count, __FILE__,  __LINE__);
            table->byte_entries = NULL; 
            table->offset_entries = NULL;
            table->inner_count = NULL;
            return;
        }
    }
    table->inner_count[idx] = initial_inner;
    table->bucket_count = outer_count;
    entry_table_inner_init(table, idx, 0x0);
    entry_table_inner_init(table, idx, 0x01);
}

FORCE_INLINE void entry_table_inner_init(entry_table_t* table, size_t idx, const unsigned char mode) {
    if (mode == 0x0 && table->offset_entries[idx]) return;
    else if (mode == 0x01 && table->byte_entries[idx]) return;

    if (mode == 0x0) {
        table->offset_entries[idx] = shared_address(NULL, table->inner_count[idx] * sizeof(byte_entries_t*), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
        if (table->offset_entries[idx] == MAP_FAILED) { table->offset_entries[idx] = NULL; return; }
        else {
            int res = madvise(table->offset_entries[idx], table->inner_count[idx] * sizeof(byte_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                munmap_address(table->offset_entries[idx], table->inner_count[idx] * sizeof(byte_entries_t*), __FILE__,  __LINE__);
                table->offset_entries[idx] = NULL;
                return;
            }
        }
    }
    else if (mode == 0x01) {
        table->byte_entries[idx] = shared_address(NULL, table->inner_count[idx] * sizeof(byte_entries_t*), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
        if (table->byte_entries[idx] == MAP_FAILED) { table->byte_entries[idx] = NULL; return; }
        else {
            int res = madvise(table->byte_entries[idx], table->inner_count[idx] * sizeof(byte_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                munmap_address(table->byte_entries[idx], table->inner_count[idx] * sizeof(byte_entries_t*), __FILE__,  __LINE__);
                table->byte_entries[idx] = NULL;
                return;
            }
        }
    }
    return;
}

/** 
    * @desription: Free function that resizes `offset_entries` and `byte_entries` for regular allocation and huge allocation
    * @param table: a user defined field member that is apart of `allocate->bucket` or `allocate.huge`
    * @param idx: A index that can be apart of `allocator->bucket` or `allocator.huge->slots`
    * @param idx: The mode to choose to resize the field 
    * @return: Nothing 
*/
FORCE_INLINE void entry_table_resize_inner(entry_table_t* table, const size_t idx, const unsigned char mode) {
    size_t new_inner_count = ((table->inner_count[idx] * 2) > idx) ? table->inner_count[idx] * 2 : (table->inner_count[idx] + idx) * 2;
    if (mode == 0x0) {
        offset_entries_t** new_rows = shared_address(NULL, new_inner_count * sizeof(offset_entries_t*), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
        if (new_rows == MAP_FAILED) return;
        else {
            int res = madvise(new_rows, new_inner_count * sizeof(offset_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                munmap_address(new_rows, new_inner_count * sizeof(offset_entries_t*), __FILE__,  __LINE__);
                return;
            }
        }
        memcpy( new_rows, table->offset_entries[idx], table->inner_count[idx]);
        munmap_address(table->offset_entries[idx], table->inner_count[idx] * sizeof(offset_entries_t*), __FILE__,  __LINE__);
        table->offset_entries[idx] = NULL;
        table->offset_entries[idx] = new_rows;
        table->inner_count[idx] = new_inner_count;
    }
    else if (mode == 0x01) {
        byte_entries_t** new_rows = shared_address(NULL, new_inner_count * sizeof(byte_entries_t*), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
        if (new_rows == MAP_FAILED) return;
        else {
            int res = madvise(new_rows, new_inner_count * sizeof(byte_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                munmap_address(new_rows, new_inner_count * sizeof(byte_entries_t*), __FILE__,  __LINE__);
                return;
            }
        }
        memcpy(new_rows, table->byte_entries[idx], table->inner_count[idx]);
        munmap_address(table->byte_entries[idx], table->inner_count[idx] * sizeof(byte_entries_t*), __FILE__,  __LINE__);
        table->byte_entries[idx] = NULL;
        table->byte_entries[idx] = new_rows;
        table->inner_count[idx] = new_inner_count;
    }
}

/**
    * @description: Resizes the data member field copying over the old data to the new memory block.
    * @param table: A stack allocated addres variable 
    * @note: It resizes itself by the multiple of a numerical value that's a power of 2 i.e 64
    * @return: Nothing
*/
FORCE_INLINE void entry_table_resize_table(entry_table_t* table, const unsigned char mode) {
    if (mode == 0x0 && !table->offset_entries) return;
    else if (mode == 0x01 && !table->byte_entries) return;

    size_t new_bucket_count = table->bucket_count * 2;
    if (mode == 0x01) {
        offset_entries_t*** new_entries =  shared_address(NULL, new_bucket_count * sizeof(offset_entries_t**), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
        if (new_entries == MAP_FAILED) return;
        else {
            int res = madvise(new_entries, new_bucket_count * sizeof(offset_entries_t**), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                munmap_address(new_entries, sizeof(offset_entries_t**) * new_bucket_count, __FILE__,  __LINE__);
                return;
            }
        }
        const size_t size = table->bucket_count * sizeof(offset_entries_t**);
        memcpy(new_entries, table->offset_entries, size);
        munmap_address(table->offset_entries, size, __FILE__,  __LINE__);
        table->offset_entries = NULL;
        table->offset_entries = new_entries;
        table->bucket_count = new_bucket_count;
    }
    else if (mode == 0x01) {
        byte_entries_t*** new_entries =  shared_address(NULL, new_bucket_count * sizeof(byte_entries_t**), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
        if (new_entries == MAP_FAILED) return;
        else {
            int res = madvise(new_entries, new_bucket_count * sizeof(byte_entries_t**), MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                munmap_address(new_entries, sizeof(byte_entries_t**) * new_bucket_count, __FILE__,  __LINE__);
                return;
            }
        }
        const size_t size = table->bucket_count * sizeof(byte_entries_t**);
        memcpy(new_entries, table->byte_entries, size);
        munmap_address(table->byte_entries, size, __FILE__,  __LINE__);
        table->byte_entries = NULL;
        table->byte_entries = new_entries;
        table->bucket_count = new_bucket_count;
    }
}

FORCE_INLINE byte_entries_t* create_byte_entry(const size_t bytes, const unsigned char inuse, void* ptr) {
    byte_entries_t* bnode = aligned_alloc(alignof(byte_entries_t), sizeof(byte_entries_t));
    if (bnode) {
        memset(bnode, 0, sizeof(byte_entries_t)); 
        bnode->ptr = ptr; 
        bnode->offset = NULL; 
        bnode->next = NULL; 
        bnode->bytes = bytes; 
        bnode->inuse = inuse;
        return bnode;
    }
    return NULL;
}

FORCE_INLINE offset_entries_t* create_offset_entry(const size_t offset, const unsigned char inuse) {
    offset_entries_t* onode = aligned_alloc(alignof(offset_entries_t), sizeof(offset_entries_t));
    if (onode) {
        memset(onode, 0, sizeof(offset_entries_t)); 
        onode->bytes = NULL; 
        onode->next = NULL; 
        onode->offset = offset; 
        onode->inuse = inuse;
        return onode;
    }
    return NULL;
}

/**
    * @description: Free function that adds a new bijection pair, and adds the existing entries to the back.
    * @param table: A stack address variable apart of the bucket_t
    * @param idx: A variable that has been already pre-computed by the allocator's bitmap functions or a bucket function
    * @param offset: An offset generated by the arena 
    * @param bytes: The requested bytes 
    * @param inuse: Mark the entry as 0x0 not in use or 0x01 as in use 
    * @return: Return's nothing
*/
FORCE_INLINE void set(_Atomic(entry_table_t)* atomic_table, const size_t idx, const size_t offset, const size_t bytes, const unsigned char inuse, void* ptr) {
    if (offset == 0 || bytes == 0) return;
    
    entry_table_t table = atomic_load_explicit(atomic_table, memory_order_relaxed);
    if (!table.offset_entries || !table.byte_entries) entry_table_init(&table, idx);
    if (!table.inner_count[idx]) {
        entry_table_resize_table(&table, 0x0);
        entry_table_resize_table(&table, 0x01);
    }

    byte_entries_t* bnode = create_byte_entry(bytes, inuse, ptr);
    offset_entries_t* onode = create_offset_entry(offset, inuse);
    if (!bnode || !onode) { free(bnode); free(onode); }

    bnode->offset = onode;
    onode->bytes = bnode;

    size_t hs_offset = hash_mix(offset) % table.inner_count[idx];
    if (table.offset_entries[idx][hs_offset]) {
        onode->next = table.offset_entries[idx][hs_offset];
        table.offset_entries[idx][hs_offset] = onode;
    } else table.offset_entries[idx][hs_offset] = onode;

    size_t hs_bytes = hash_mix(bytes) % table.inner_count[idx];
    if (table.byte_entries[idx][hs_bytes]) {
        bnode->next = table.byte_entries[idx][hs_bytes];
        table.byte_entries[idx][hs_bytes] = bnode;
    } else table.byte_entries[idx][hs_bytes] = bnode;

    atomic_exchange_explicit(atomic_table, table, memory_order_relaxed);
    return;
}

/**
    * @description: Free function that gets the bytes node based on the bytes and the inuse 
    * @param table: the table that might have the desired byte node
    * @param idx: the numerical value used for indexing into `entries`
    * @param bytes: The requested bytes 
    * @param inuse: The desired flag which can be 0x0 or 0x01 
*/
FORCE_INLINE byte_entries_t* get_entry_t_by_bytes(_Atomic(entry_table_t)* atomic_table, const size_t idx, const size_t bytes, const unsigned char inuse) {
    const entry_table_t table = atomic_load_explicit(atomic_table, memory_order_relaxed);
    if (__builtin_expect(!table.byte_entries, 0)) return NULL;
    else if (__builtin_expect(idx >= table.bucket_count, 0)) return NULL;
    else if (__builtin_expect(!table.byte_entries[idx], 0)) return NULL;
    else if (__builtin_expect(!table.inner_count[idx], 0)) return NULL;

    const size_t hs_bytes = hash_mix(bytes) % table.inner_count[idx];

    byte_entries_t* bnode = table.byte_entries[idx][hs_bytes];
    while (__builtin_expect(bnode != NULL, 1)) {
        __builtin_prefetch(bnode->next, 0, 1);
        if ((bnode->bytes == bytes) && (bnode->inuse == inuse)) return bnode;
        bnode = bnode->next;
    }
    return NULL;
}

/**
    * @description: Free function that gets the offset node based on the offset and the inuse 
    * @param table: the table that might have the desired offset node
    * @param idx: the numerical value used for indexing into `offset_entries`
    * @param offset: The requested offset 
    * @param inuse: The desired flag which can be 0x0 or 0x01 
*/
FORCE_INLINE offset_entries_t* get_entry_t_by_offset(_Atomic(entry_table_t)* atomic_table, const size_t idx, const size_t offset, const unsigned char inuse) {
    const entry_table_t table = atomic_load_explicit(atomic_table, memory_order_relaxed);
    if (__builtin_expect(!table.offset_entries, 0)) return NULL;
    else if (__builtin_expect(idx >= table.bucket_count, 0)) return NULL;
    else if (__builtin_expect(!table.offset_entries[idx], 0)) return NULL;
    else if (__builtin_expect(!table.inner_count[idx], 0)) return NULL;

    const size_t hs_offset = hash_mix(offset) % table.inner_count[idx];

    offset_entries_t* onode = table.offset_entries[idx][hs_offset];
    while (__builtin_expect(onode != NULL, 1)) {
        __builtin_prefetch(onode->next, 0, 1);
        if ((onode->offset == offset) && (onode->inuse == inuse)) return onode;
        onode = onode->next;
    }
    return NULL;
}

/**
    * @description: Free function that updates a bijection nodes
    * @param table: A stack address variable apart of the bucket_t
    * @param idx: A variable that has been already pre-computed by the allocator's bitmap functions or a bucket function
    * @param offset: An offset generated by the arena 
    * @param bytes: The requested bytes 
    * @param inuse: Mark the entry as 0x0 not in use or 0x01 as in use 
    * @return: Return's nothing
*/
FORCE_INLINE void update(_Atomic(entry_table_t)* atomic_table, const size_t idx, const size_t offset, const size_t bytes, const unsigned char inuse) {
    if (offset == 0 || bytes == 0) return;

    entry_table_t table = atomic_load_explicit(atomic_table, memory_order_relaxed);
    if (idx >= table.bucket_count || !table.inner_count[idx]) return;

    const size_t hs_offset = hash_mix(offset) % table.inner_count[idx];
    offset_entries_t* onode = table.offset_entries[idx][hs_offset];
    while (__builtin_expect(onode != NULL, 1)) {
        __builtin_prefetch(onode->next, 0, 1);
        if (onode->offset == offset) {
            __builtin_prefetch(onode->bytes, 0, 1);
            byte_entries_t* bnode = onode->bytes;
            if (bnode && bnode->bytes == bytes) {
                bnode->inuse = inuse;
                onode->inuse = inuse;
                atomic_exchange_explicit(atomic_table, table, memory_order_relaxed);
                return;
            }
        }
        onode = onode->next;
    }
}

/**
    * @description: Free function that destroys a bijection node
    * @param table: A stack address allocated variable apart of the bucket_t
    * @param idx: A variable that has been already pre-computed by the allocator's bitmap functions or a bucket function
    * @param offset: An offset generated by the arena 
    * @param bytes: The requested bytes 
    * @param inuse: Mark the entry as 0x0 not in use or 0x01 as in use 
    * @return: Return's nothing
*/
FORCE_INLINE void destroy(_Atomic(entry_table_t)* atomic_table, const size_t idx, const size_t offset, const size_t bytes) {
    entry_table_t table = atomic_load_explicit(atomic_table, memory_order_relaxed);
    if (offset == 0 || bytes == 0) return;
    else if (idx >= table.bucket_count || !table.inner_count[idx]) return;

    const size_t hs_offset = hash_mix(offset) % table.inner_count[idx];
    offset_entries_t* cur = table.offset_entries[idx][hs_offset];

    offset_entries_t* prev = NULL;
    offset_entries_t* bn = NULL;

    while (__builtin_expect(cur != NULL, 1)) {
        __builtin_prefetch(cur->next, 0, 1);
        __builtin_prefetch(cur->bytes, 0, 1);
        const byte_entries_t* bnode = cur->bytes;
        if ((cur->offset == offset) && (bnode->bytes == bytes)) { bn = cur; break; }
        prev = cur;
        cur = cur->next;
    }
    if (!bn) return;

    if (prev) prev->next = bn->next; else table.offset_entries[idx][hs_offset] = bn->next;

    byte_entries_t* bne = bn->bytes;

    if (bne) {
        const size_t hs_bytes = hash_mix(bytes) % table.inner_count[idx];
        byte_entries_t* bcur = table.byte_entries[idx][hs_bytes];
        byte_entries_t* bprev = NULL;
        while (bcur) {
            if (bcur == bne) {
                if (bprev) bprev->next = bcur->next; else table.byte_entries[idx][hs_bytes] = bcur->next;
                break;
            }
            bprev = bcur;
            bcur = bcur->next;
        }
    }
    
    void* old = bn;
    memset(bn, 0, offsetof(offset_entries_t, next));
    memset((char*)bn + offsetof(offset_entries_t, next) + sizeof(offset_entries_t*), 0,
           sizeof(offset_entries_t) - offsetof(offset_entries_t, next) - sizeof(offset_entries_t*));
    free(old);

    if (bne) {
        old = bne;
        memset(bne, 0, offsetof(byte_entries_t, next));
        memset((char*)bne + offsetof(byte_entries_t, next) + sizeof(byte_entries_t*), 0,
               sizeof(byte_entries_t) - offsetof(byte_entries_t, next) - sizeof(byte_entries_t*));
        free(old);
    }
    atomic_store_explicit(atomic_table, table, memory_order_relaxed);
}

FORCE_INLINE void clean(entry_table_t *table) {
    if (table->byte_entries && table->offset_entries) {
        for (size_t i = 0; i < table->bucket_count; i++) {
            byte_entries_t** arr = table->byte_entries[i];
            if (arr) {
                for (size_t slot = 0; slot < table->inner_count[i]; slot++) {
                    byte_entries_t* n = arr[slot];
                    while (__builtin_expect(n != NULL, 1)) {
                        __builtin_prefetch(n->next, 0, 1);
                        __builtin_prefetch(n->offset, 0, 1);
                        byte_entries_t* next = n->next;
                        offset_entries_t* on = n->offset;
                        
                        void* old = n;
                        memset(n, 0, sizeof(byte_entries_t));
                        free(old);

                        if (on) {
                            old = on;
                            memset(on, 0, sizeof(offset_entries_t));
                            free(old);
                        }
                        n = next;
                    }
                }
                const size_t byte_row_size = table->inner_count[i] * sizeof(byte_entries_t*);
                munmap_address(table->byte_entries[i], byte_row_size, __FILE__,  __LINE__);
                table->byte_entries[i] = NULL;
            }

            if (table->offset_entries[i]) {
                const size_t offset_row_size = table->inner_count[i] * sizeof(offset_entries_t*);
                munmap_address(table->offset_entries[i], offset_row_size, __FILE__,  __LINE__);
                table->offset_entries[i] = NULL;
            }
        }

        const size_t byte_outer_size   = (table->bucket_count / 2) * sizeof(byte_entries_t**);
        const size_t offset_outer_size = (table->bucket_count / 2) * sizeof(offset_entries_t**);
        munmap_address(table->byte_entries, byte_outer_size, __FILE__,  __LINE__);
        munmap_address(table->offset_entries, offset_outer_size, __FILE__,  __LINE__);
        table->byte_entries = NULL;
        table->offset_entries = NULL;
    }
    memset(table, 0, sizeof(entry_table_t));
}

FORCE_INLINE void table_entry_free_inner_pages(entry_table_t* table, const size_t idx) {
    if (!table->offset_entries && !table->byte_entries) return table_entry_free_pages(table);
    else if (idx >= table->bucket_count) return;

    if (table->offset_entries && table->offset_entries[idx]) {
        const size_t offset_size = table->inner_count[idx] * sizeof(offset_entries_t*);
        madvise(table->offset_entries[idx], offset_size, MADV_DONTNEED);
    }
    if (table->byte_entries && table->byte_entries[idx]) {
        const size_t byte_size = table->inner_count[idx] * sizeof(byte_entries_t*);
        madvise(table->byte_entries[idx], byte_size, MADV_DONTNEED);
    }
}

FORCE_INLINE void table_entry_free_pages(entry_table_t* table) {
    if (table->byte_entries) {
        const size_t byte_size = (table->bucket_count / 2) * sizeof(byte_entries_t**);
        madvise(table->byte_entries, byte_size, MADV_DONTNEED);
    }
    if (table->offset_entries) {
        const size_t offset_size = (table->bucket_count / 2) * sizeof(offset_entries_t**);
        madvise(table->offset_entries, offset_size, MADV_DONTNEED);
    }
}

FORCE_INLINE void debug_entry_table_t(const unsigned char mode, entry_table_t* table, const size_t idx) {
    if (!table->byte_entries) return;
    else if (idx >= table->bucket_count) return;
    else if (!table->byte_entries[idx]) return;

    if (mode == 0x0) {
        byte_entries_t** arr = table->byte_entries[idx];
        for (size_t slot = 0; slot < table->inner_count[idx]; slot++) {
            byte_entries_t* bn = arr[slot];
            while (__builtin_expect(bn != NULL, 1)) {
                __builtin_prefetch(bn->next, 0, 1);
                __builtin_prefetch(bn->offset, 0, 1);
                offset_entries_t* onode = bn->offset;
                if (onode && onode->offset) {
                    printf("Offset value is: %zu\n", onode->offset);
                    printf("Inuse Value is: %#0x\n", bn->inuse);
                }
                bn = bn->next;
            }
        }
    }
    else if (mode == 0x01) {
        byte_entries_t** arr = table->byte_entries[idx];
        for (size_t slot = 0; slot < table->inner_count[idx]; slot++) {
            byte_entries_t* bnode = arr[slot];
            while (__builtin_expect(bnode != NULL, 1)) {
                __builtin_prefetch(bnode->next, 0, 1);
                printf("Bytes value is: %zu\n", bnode->bytes);
                bnode = bnode->next;
            }
        }
    }
}

typedef struct blocks_t {
    _Atomic(entry_table_t)      table;
    struct blocks_t** chain;    /* TODO: We can tag/untag chain[i] instead of using inuse. */
    _Atomic(struct blocks_t*)   next;
    _Atomic(void*)              ptr;
    _Atomic(size_t)             bytes;
    _Atomic(size_t)             offset;
    _Atomic(size_t)             size;
    _Atomic(unsigned char)      inuse;
} blocks_t;

/////////////////////////
// BLOCK CHAIN SECTION //
////////////////////////


FORCE_INLINE void init_blocks_t(blocks_t* blocks) {
    blocks->size = 64;
    blocks->chain = shared_address(NULL, blocks->size * sizeof(blocks_t*), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
    if (blocks->chain == MAP_FAILED) return;
    else {
        int res = madvise(blocks->chain, blocks->size * sizeof(blocks_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) {
            if (blocks->chain) munmap_address(blocks->chain, blocks->size * sizeof(blocks_t*), __FILE__,  __LINE__);
            blocks->chain = NULL;
            return;
        }
    }
    return;
}

FORCE_INLINE blocks_t* create_block_t() {
    blocks_t* block = aligned_alloc(alignof(blocks_t), sizeof(blocks_t));
    if (!block) return NULL;
    memset(block, 0, sizeof(blocks_t));
    return block;
}

/**
    * @description: Free function that resizes `blocks->chain` by doubling it
    * @param blocks: A pointer type consisting of free adjacent blocks that are in use or not inuse.
    * @return: Nothing
*/
FORCE_INLINE void resize_blocks(blocks_t* blocks) {
    if (!blocks->chain) return; 
    const size_t old = blocks->size;
    const size_t new = (blocks->size * 2) * sizeof(blocks_t*);
    
    blocks_t** chain = shared_address(NULL, new * sizeof(blocks_t*), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
    if (chain == MAP_FAILED) return;
    else {
        int res = madvise(chain, new * sizeof(blocks_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) {
            if (chain) { munmap_address(chain, new * sizeof(blocks_t*), __FILE__,  __LINE__); chain = NULL; }
            return;
        }
    }

    void* ptr = blocks->chain;
    memcpy(chain, ptr, old);
    munmap_address(ptr, old, __FILE__,  __LINE__);
    blocks->size = new;
    
    return;
}

/**
    * @description: Free function that checks to see if any free entries can be merged or not before merging
    * @param table: User defined type that contains a table of entries that are inuse or not inuse
    * @param idx: The location where the search should begin at.
    * @param bytes: The requested bytes the user desires
    * @return: Returns 0x01 if successfull, otherwise 0x0
*/
FORCE_INLINE unsigned char is_mergeable(_Atomic(entry_table_t)* atomic_table, const size_t idx, const size_t bytes) {
    const entry_table_t table = atomic_load_explicit(atomic_table, memory_order_relaxed);
    if (bytes == 0 || !table.byte_entries) return 0x0;
    else if (idx >= table.bucket_count) return 0x0;

    byte_entries_t** arr = table.byte_entries[idx];
    if (!arr) return 0x0;

    for (size_t slot = 0; slot < table.inner_count[idx]; slot++) {
        byte_entries_t* seed = arr[slot];
        while (__builtin_expect(seed != NULL, 1)) {
            __builtin_prefetch(seed->next, 0, 1);
            if (seed->inuse == 0x0 && seed->offset) {
                __builtin_prefetch(seed->offset, 0, 1);
                if (seed->offset->offset) {
                    size_t accumulated = seed->bytes;
                    size_t probe_offset = seed->offset->offset + seed->bytes;
                    while (accumulated < bytes) {
                        offset_entries_t* nxt = get_entry_t_by_offset(atomic_table, idx, probe_offset, 0x0);
                        if (!nxt) break;
                        accumulated += nxt->bytes->bytes;
                        probe_offset += nxt->bytes->bytes;
                    }
                    if (accumulated >= bytes) return 0x01;
                }
            }
            seed = seed->next;
        }
    }
    return 0x0;
}

FORCE_INLINE void merge(blocks_t* blocks, const size_t idx, const size_t bytes) {
    if (!blocks->chain) init_blocks_t(blocks);
    else if (idx >= blocks->size) resize_blocks(blocks);

    entry_table_t table = atomic_load_explicit(&blocks->table, memory_order_relaxed);
    if (idx >= table.bucket_count || !table.byte_entries) return;

    byte_entries_t** arr = table.byte_entries[idx];
    if (!arr) return;

    byte_entries_t* seed = NULL;
    unsigned char found = 0x0;

    for (size_t slot = 0; slot < table.inner_count[idx] && !found; slot++) {
        seed = arr[slot];
        while (__builtin_expect(seed != NULL, 1)) {
            __builtin_prefetch(seed->next, 0, 1);
            if (seed->inuse == 0x0 && seed->offset) {
                __builtin_prefetch(seed->offset, 0, 1);
                size_t accumulated = seed->bytes;
                size_t probe_offset = seed->offset->offset + seed->bytes;
                while (accumulated < bytes) {
                    offset_entries_t* nxt = get_entry_t_by_offset(&blocks->table, idx, probe_offset, 0x0);
                    if (!nxt) break;
                    accumulated += nxt->bytes->bytes;
                    probe_offset += nxt->bytes->bytes;
                }
                if (accumulated >= bytes) { found = 0x01; break; }
            }
            seed = seed->next;
        }
    }
    if (!seed) return;

    __builtin_prefetch(seed->offset, 0, 1);

    size_t       total;
    size_t       cur_offset;
    const size_t merge_start_offset = seed->offset->offset;
    void*        merged_ptr = NULL;

    total      = 0;
    cur_offset = merge_start_offset;

    while (total < bytes) {
        offset_entries_t* oe = get_entry_t_by_offset(&blocks->table, idx, cur_offset, 0x0);
        if (!oe) break;
        __builtin_prefetch(oe->bytes, 0, 1);
        byte_entries_t* piece_entry = oe->bytes;
        size_t piece_bytes = piece_entry->bytes;
        size_t need = bytes - total;

        if (!merged_ptr) merged_ptr = piece_entry->ptr;

        if (piece_bytes > need) {
            size_t leftover_bytes = piece_bytes - need;
            size_t leftover_offset = cur_offset + need;
            void* leftover_ptr = (char*)piece_entry->ptr + need;

            destroy(&blocks->table, idx, cur_offset, piece_bytes);
            set(&blocks->table, idx, leftover_offset, leftover_bytes, 0x0, leftover_ptr);

            total += need;
            cur_offset += need;
        } else {
            destroy(&blocks->table, idx, cur_offset, piece_bytes);

            total += piece_bytes;
            cur_offset += piece_bytes;
        }
    }

    if (total < bytes) return;

    blocks_t* node = create_block_t();
    if (!node) return;

    node->offset = merge_start_offset;
    node->inuse  = 0x01;
    node->ptr    = merged_ptr;
    node->bytes  = bytes;
    node->next   = blocks->chain[idx];
    blocks->chain[idx] = node;
}

FORCE_INLINE void update_block_t_by_offset(blocks_t* blocks, const size_t idx, const size_t offset, const unsigned char inuse) {
    if (offset == 0) return;
    else if (!blocks->chain || !blocks->chain[idx]) return;

    blocks_t* seed = blocks->chain[idx];
    while (__builtin_expect(seed != NULL, 1)) {
        __builtin_prefetch(seed->next, 0, 1);
        if ((seed->offset == offset) && (seed->inuse == inuse)) {
            seed->inuse = 0x0;
            return;
        }
        seed = seed->next;
    }
}

FORCE_INLINE blocks_t* get_block_t_by_offset(blocks_t* blocks, const size_t idx, const size_t offset, const unsigned char inuse) {
    if (offset == 0) return NULL;
    else if (!blocks->chain || !blocks->chain[idx]) return NULL;

    blocks_t* seed = blocks->chain[idx];
    while (__builtin_expect(seed != NULL, 1)) {
        __builtin_prefetch(seed->next, 0, 1);
        if ((seed->offset == offset) && (seed->inuse == inuse)) return seed;
        seed = seed->next;
    }
    return NULL;
}

FORCE_INLINE void blocks_t_free_pages(blocks_t* blocks) {
    if (!blocks->chain) return;
    int res = madvise(blocks->chain, blocks->size * sizeof(blocks_t*), MADV_DONTNEED);
    if (res == -1) return;
}

FORCE_INLINE void block_t_dctor(blocks_t* blocks) {
    blocks_t* seed = blocks;
    while (__builtin_expect(seed != NULL, 1)) {
        blocks_t* node = seed->next;
        if (seed) {
            void* old = seed;
            memset(seed, 0, sizeof(blocks_t));
            free(old);
        }
        seed = node;
    }
    blocks = NULL;
}

typedef struct bucket_t {
    blocks_t       blocks;
    arena_t*       arena;
    unsigned char  flag;
} bucket_t;

typedef struct huge_slot_t {
    blocks_t       blocks;  
    size_t         capacity; /* Represents the amount of memory it can hold */
    size_t         space;   /* capacity >= space otherwise slot is full */
} huge_slot_t;
FORCE_INLINE void* coalescing(size_t bytes); /* Defined in Allocator Section */
FORCE_INLINE size_t find_bucket_index(bucket_t* slot);


/////////////////////
// BUCKET SECTION //
///////////////////


/**
    * @description: A Free function that finds a free slot based on the size at O(n). 
    * @param sz: can be a numeric value of size: 64, 128, or 256. 
    * @return: Returns a slot right after checking the bucket's bitmap.
    * @note: if nothing is returned, that means all of the slots from small, medium and large are occupied. 
*/
FORCE_INLINE bucket_t* find_free_slot(size_t sz) {
    size_t idx = SIZE_MAX;
    
    if (sz < BUCKET_SMALL_CAP) {
        idx = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP - 1);
        #if LOGGING == 1
            #if LOGLEVEL == 0  
                printf("[find_free_slot] small idx: %d\n", idx);
            #elif LOGLEVEL > 1
                // TODO: Include the logger variable here and its functions
            #endif
        #endif
        if (idx <= USHRT_MAX && idx != SIZE_MAX) return &allocator.bucket.small[idx];
    }
    else if (sz < BUCKET_MEDIUM_CAP) {
        idx = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP - 1);
        #if LOGGING == 1
            #if LOGLEVEL == 0 
                printf("[find_free_slot] medium idx: %d\n", idx);
            #elif LOGLEVEL > 1
                // TODO: Include the logger variable here and its functions
            #endif
        #endif 
        if (idx <= USHRT_MAX && idx != SIZE_MAX) return &allocator.bucket.medium[idx];
    }
    else if (sz < BUCKET_LARGE_CAP) {
        idx = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP - 1);
        #if LOGGING == 1
            #if LOGLEVEL == 0 
                printf("[find_free_slot] large idx: %d\n", idx);
            #elif LOGLEVEL > 1
                // TODO: Include the logger variable here and its functions
            #endif
        #endif 
        if (idx <= USHRT_MAX && idx != SIZE_MAX) return &allocator.bucket.large[idx];
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

    for (size_t i = 0; i < BUCKET_SMALL_CAP; i++) {
        bucket_t* b = &allocator.bucket.small[i];
        if (b->arena && p >= (uintptr_t)b->arena->chunk && p < (uintptr_t)b->arena->chunk + ARENA_SIZE) {
            size_t offset = (size_t)(p - (uintptr_t)b->arena->chunk);
            if (get_entry_t_by_offset(&b->blocks.table, i, offset, 0x01)) {
                if (b->flag == 0x01 && b->arena->flag == 0x01) {
                    b->flag = 0x0; b->arena->flag = 0x0;
                    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_SMALL_CAP - 1);
                }
                return b;
            }
            blocks_t* blocks = get_block_t_by_offset(&b->blocks, i, offset, 0x01);
            if (blocks) return b;
        }
    }

    for (size_t i = 0; i < BUCKET_MEDIUM_CAP; i++) {
        bucket_t* b = &allocator.bucket.medium[i];
        if (b->arena && p >= (uintptr_t)b->arena->chunk && p < (uintptr_t)b->arena->chunk + ARENA_SIZE) {
            size_t offset = (size_t)(p - (uintptr_t)b->arena->chunk);
            if (get_entry_t_by_offset(&b->blocks.table, i, offset, 0x01)) {
                if (b->flag == 0x01 && b->arena->flag == 0x01) {
                    b->flag = 0x0; b->arena->flag = 0x0;
                    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_MEDIUM_CAP - 1);
                }
                return b;
            }
            blocks_t* blocks = get_block_t_by_offset(&b->blocks, i, offset, 0x01);
            if (blocks) return b;
        }
    }

    for (size_t i = 0; i < BUCKET_LARGE_CAP; i++) {
        bucket_t* b = &allocator.bucket.large[i];
        if (b->arena && p >= (uintptr_t)b->arena->chunk && p < (uintptr_t)b->arena->chunk + ARENA_SIZE) {
            size_t offset = (size_t)(p - (uintptr_t)b->arena->chunk);
            if (get_entry_t_by_offset(&b->blocks.table, i, offset, 0x01)) {
                if (b->flag == 0x01 && b->arena->flag == 0x01) {
                    b->flag = 0x0; b->arena->flag = 0x0;
                    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_LARGE_CAP - 1);
                }
                return b;
            }
            blocks_t* blocks = get_block_t_by_offset(&b->blocks, i, offset, 0x01);
            if (blocks) return b;
        }
    }
    return NULL;
}

FORCE_INLINE size_t find_bucket_index(bucket_t* slot) {
    if (!slot) return SIZE_MAX;
    if (slot >= allocator.bucket.small && slot < allocator.bucket.small + BUCKET_SMALL_CAP) return (size_t)(slot - allocator.bucket.small);
    if (slot >= allocator.bucket.medium && slot < allocator.bucket.medium + BUCKET_MEDIUM_CAP) return (size_t)(slot - allocator.bucket.medium);
    if (slot >= allocator.bucket.large && slot < allocator.bucket.large + BUCKET_LARGE_CAP) return (size_t)(slot - allocator.bucket.large);
    return SIZE_MAX;
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

    uintptr_t large_start = (uintptr_t)allocator.bucket.large;
    uintptr_t large_end   = large_start + BUCKET_LARGE_CAP * sizeof(bucket_t);
    if (addr >= large_start && addr < large_end) return BUCKET_LARGE_CAP;
    return 0;
}

/**
    * @description: A Free Function that pushes the unused memory addresses to bucket.
        It is used with deallocation function
    * @param b: A specific bucket that will now have been updated 
*/
FORCE_INLINE void push_to_bucket(bucket_t* slot, size_t offset) {
    size_t idx = find_bucket_index(slot);
    if (idx == SIZE_MAX) return;
    
    offset_entries_t* onode = get_entry_t_by_offset(&slot->blocks.table, idx, offset, 0x01);
    if (!onode) return;
    update(&slot->blocks.table, idx, offset, onode->bytes->bytes, 0x0);
    return;
}

/**
    * @description: A free function that pops off a memory address that is not in use based on the requested size
    * @param slot: The slot which is bucket_t that has memory addresses to be used. 
    * @param bytes: the requested bytes 
    * @param address: An atomic void* type that will be swapped by `t1` if there if there is a match.  
    * @return: Returns null if bucket field is null or if bucket == slot->arena->chunk  
*/
FORCE_INLINE void* pop_from_bucket(bucket_t* slot, const size_t bytes) {
    const size_t idx = find_bucket_index(slot);
    if (idx == SIZE_MAX) return NULL;

    byte_entries_t* bnode = get_entry_t_by_bytes(&slot->blocks.table, idx,  bytes, 0x0);
    if (!bnode || bnode->ptr == NULL) return coalescing(bytes);
    
    void* address = NULL;
    if (bnode && bnode->offset) {
        update(&slot->blocks.table, idx, bnode->offset->offset, bnode->bytes, 0x01);
        //slot->arena = push(slot->arena, bytes);
        address = bnode->ptr;
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

[[gnu::cold]]
FORCE_INLINE void bucket_t_dctor() {
    size_t small = 0;
    size_t medium = 0;
    while (small < BUCKET_SMALL_CAP) {
        arena_t* arena = allocator.bucket.small[small].arena;
        bucket_t slot = allocator.bucket.small[small];
        entry_table_t table = atomic_load_explicit(&slot.blocks.table, memory_order_relaxed);
        if (table.offset_entries && table.byte_entries) clean(&table);
        if (arena) {
            if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE + 1, __FILE__,  __LINE__);
            munmap_address(arena, sizeof(arena_t), __FILE__,  __LINE__);
            arena = NULL;
        }
        if (slot.blocks.chain && slot.blocks.chain[small]) block_t_dctor(slot.blocks.chain[small]);
        small = small + 1;
    }
    munmap_address(allocator.bucket.small, BUCKET_SMALL_CAP * sizeof(bucket_t), __FILE__,  __LINE__);
    while (medium < BUCKET_MEDIUM_CAP) {
        arena_t* arena = allocator.bucket.medium[medium].arena;
        bucket_t slot = allocator.bucket.medium[medium];
        entry_table_t table = atomic_load_explicit(&slot.blocks.table, memory_order_relaxed);
        if (table.offset_entries && table.byte_entries) clean(&table);
        if (arena) {
            //if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE + 1, __FILE__,  __LINE__);
            munmap_address(arena, sizeof(arena_t), __FILE__,  __LINE__);
            arena = NULL;
        }
        if (slot.blocks.chain && slot.blocks.chain[medium]) block_t_dctor(slot.blocks.chain[medium]);
        medium = medium + 1;
    }
    munmap_address(allocator.bucket.medium, BUCKET_MEDIUM_CAP * sizeof(bucket_t), __FILE__,  __LINE__);
    size_t large = 0;
    while (large < BUCKET_LARGE_CAP) {
        arena_t* arena = allocator.bucket.large[large].arena;
        bucket_t slot = allocator.bucket.large[large];
        entry_table_t table = atomic_load_explicit(&slot.blocks.table, memory_order_relaxed);
        if (table.offset_entries && table.byte_entries) clean(&table);
        if (arena) {
            //if (arena->chunk) munmap_address(arena->chunk, ARENA_SIZE + 1, __FILE__,  __LINE__);
            munmap_address(arena, sizeof(arena_t), __FILE__,  __LINE__);
        }
        if (slot.blocks.chain && slot.blocks.chain[large]) block_t_dctor(slot.blocks.chain[large]);
        large = large + 1;
    }
    munmap_address(allocator.bucket.large, BUCKET_LARGE_CAP * sizeof(bucket_t), __FILE__,  __LINE__);
}

/**
    * @description: A Free function that rewinds the arena back to a safe spot, based on certain conditions.
        It follows the FILO pattern, and will only start rewinging itself iif `slot.table->offset_entries[idx][0]->inuse == 0x0` 
    * @param slot: A raw pointer that has been allocated on the heap that is apart of the allocator.bucket
    * @return Nothing
*/
FORCE_INLINE void __rewind(bucket_t* slot) {
    const size_t idx = find_bucket_index(slot);
    if (idx == SIZE_MAX) return;

    entry_table_t table = atomic_load_explicit(&slot->blocks.table, memory_order_relaxed);
    if (idx >= table.bucket_count || !table.offset_entries) return;

    byte_entries_t** arr = table.byte_entries[idx];
    if (!arr) return;

    unsigned char all_clear = 0x01;

    for (size_t i = 0; i < table.inner_count[idx]; i++) {
        byte_entries_t* bnode = arr[i];
        while (bnode && bnode->inuse == 0x0) {
            __builtin_prefetch(bnode->offset, 0, 1);
            byte_entries_t* next = bnode->next;
            const size_t offset = bnode->offset->offset;
            const size_t bytes = bnode->bytes;
            slot->arena = pop(slot->arena, bytes);
            destroy(&slot->blocks.table, idx, offset, bytes);
            bnode = next;
        }
        if (bnode) all_clear = 0x0;
    }

    if (all_clear) {
        clear_arena_t(slot->arena);
        table_entry_free_inner_pages(&table, idx);
    }
}


////////////////////////
// THREADING SECTION //
//////////////////////


FORCE_INLINE void allocator_huge_update_slots(const size_t start, const size_t end); /* Defined in Allocator section */
FORCE_INLINE void* thread_update_thread_pool(struct function_t* meta); 

FORCE_INLINE void* thread_update_thread_pool(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    
    atomic_size_t* done = (atomic_size_t*)args[0];

    update_thread_pool(allocator.pool, ALLOC_THREAD_POOL_SIZE);
    atomic_store_explicit(done, 1, memory_order_release);
    pthread_exit(NULL);
}

FORCE_INLINE void* thread_create_thread_pool_range(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    
    unsigned char mode = *(unsigned char*)args[0], attr = *(unsigned char*)args[1], locked = *(unsigned char*)args[2], stack = *(unsigned char*)args[3];
    size_t start = *(size_t*)args[4], end = *(size_t*)args[5];
    atomic_size_t* done = (atomic_size_t*)args[6];

    create_thread_pool_range(allocator.pool, mode, attr, locked, stack, start, end);
    atomic_store_explicit(done, 1, memory_order_release);
    pthread_exit(NULL);
}

FORCE_INLINE void* thread_update_block_t_by_offset(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);

    blocks_t* blocks = (blocks_t*)args[0];
    const size_t index = (size_t)(uintptr_t)args[1];
    const size_t offset = (size_t)(uintptr_t)args[2];
    const unsigned char inuse = (unsigned char)(uintptr_t)args[3];
    atomic_size_t* done = (atomic_size_t*)args[4];

    update_block_t_by_offset(blocks, index, offset, inuse);
    atomic_store_explicit(done, 1, memory_order_release);
    pthread_exit(NULL);
}

FORCE_INLINE void* thread_rewind(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    
    bucket_t* slot = (bucket_t*)args[0];
    atomic_size_t* done = (atomic_size_t*)args[1];
    
    __rewind(slot);
    atomic_store_explicit(done, 1, memory_order_release);
    pthread_exit(NULL);
}

FORCE_INLINE void* thread_allocator_huge_update_slots(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    
    size_t begin = *(size_t*)args[0];
    size_t end   = *(size_t*)args[1];
    atomic_size_t* done = (atomic_size_t*)args[2];

    allocator_huge_update_slots(begin, end);
    atomic_store_explicit(done, 1, memory_order_release);
    pthread_exit(NULL);
}


////////////////////////
// ALLOCATOR SECTION //
//////////////////////


typedef struct huge_block_allocator_t {
    bitmap_t        bitmap;
    char*           region;
    huge_slot_t*    slots;
    size_t          slot_cap;
    size_t          allocator_cap;
    atomic_size_t   done; /* Used for lock free syncing threads */
} huge_block_allocator_t;

FORCE_INLINE void init_huge_allocator(void);
FORCE_INLINE void allocator_huge_resize_slots(huge_slot_t* slot, const size_t size);
FORCE_INLINE void* allocator_huge_push_to_slot(huge_slot_t* slot, const size_t index, const size_t end, size_t bytes);
FORCE_INLINE void allocator_huge_resize_chunk(char* chunk, const size_t old_len, const size_t new_len);
FORCE_INLINE unsigned char overcommit(size_t bytes);

allocator_t allocator = {0};
void* allocate(const size_t bytes);
void deallocate(void* ptr);

[[gnu::cold]]
FORCE_INLINE void allocator_init_arena_t(void) {
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
        munmap_address(allocator.arena, sizeof(arena_t), __FILE__,  __LINE__);
        return;
    }
}

[[gnu::cold]]
// TODO: Need to make sure that MADV_MERGEABLE enabled does not consume a lot of processing power; use with care.
FORCE_INLINE void allocator_init_buckets_t(void) {
    int res = 0;
    if (!allocator.bucket.small) {
        allocator.bucket.large = shared_address(NULL, BUCKET_LARGE_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
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
        allocator.bucket.small = shared_address(NULL, BUCKET_SMALL_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
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
        allocator.bucket.medium = shared_address(NULL, BUCKET_MEDIUM_CAP * sizeof(bucket_t), PROT_WRITE | PROT_READ,  MAP_NORESERVE, -1, 0);
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
    }
}

[[gnu::cold]]
FORCE_INLINE void allocator_init_threads_t(void) {
    int res = 0;
    if (!allocator.pool) {
        allocator.pool = shared_address(NULL, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0);
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

        static const unsigned char mode = 0x0, attr = 0x01, locked = 0x0, stack = 0x0;
        static const size_t start = 1, end = ALLOC_THREAD_POOL_SIZE;
        allocator.pool[0] = init_threads_t(mode, attr, locked, stack);
        routine_metadata(&allocator.pool[0], 7, &mode, &attr, &locked, &stack, &start, &end, &allocator.huge->done);
        create_thread(&allocator.pool[0], thread_create_thread_pool_range);
    }
}


[[gnu::flatten]] /* inlines callee functions if possible */
[[gnu::malloc, gnu::malloc(deallocate, 1)]]
GCC_OPTIMIZE_O0 void* allocator_byte_request(size_t bytes) {
    if (bytes == 0 || !allocator.huge) return NULL;
    else if (!allocator.huge->region) return NULL;
    else if ((bytes & (bytes - 1)) != 0) bytes = alignment(bytes, alignof(max_align_t));

    while (!atomic_load_explicit(&allocator.huge->done, memory_order_acquire));
    
    size_t pages_needed = (size_t)((bytes + HUGE_PAGE_SIZE - 1) / HUGE_PAGE_SIZE);
    if (pages_needed == 0) return NULL;
    else if (!overcommit(bytes)) return NULL;
    
    char* address = NULL;
    if (allocator.huge->allocator_cap < bytes) {
        const size_t old_len = allocator.huge->allocator_cap; 
        const size_t new_len = alignment(allocator.huge->allocator_cap * bytes, alignof(HUGE_PAGE_SIZE));
        allocator_huge_resize_chunk(allocator.huge->region, old_len, new_len);
    }

    size_t size = allocator.huge->slot_cap;
    size_t index = allocator.huge->bitmap.bitmap_test(allocator.huge->bitmap, 0, allocator.huge->slots[size - 1].capacity);
    if (index == SIZE_MAX) {
        bitmap_t_resize(&allocator.huge->bitmap, allocator.huge->bitmap.n_bytes * 2);
        allocator_huge_resize_slots(allocator.huge->slots, allocator.huge->slots[size - 1].capacity * 2);
        if (!allocator.huge->slots || !allocator.huge->bitmap.bits) return NULL;
        size = allocator.huge->slot_cap;
        index = allocator.huge->bitmap.bitmap_test(allocator.huge->bitmap, 0, allocator.huge->slots[size - 1].capacity);
    } 
    else if (is_mergeable(&allocator.huge->slots[index].blocks.table, index, bytes)) {
        merge(&allocator.huge->slots[index].blocks,  index, bytes);
        const size_t rhs = allocator.huge->slots[index].blocks.offset + allocator.huge->slots[index].blocks.bytes;
        if (allocator.huge->slots[index].space >= rhs) {
            allocator.huge->slots[index].space -= rhs; /* Below this line the code is wrong possibly */
            if (!allocator.huge->slots[index].blocks.chain[index]->next) blocks_t_free_pages(allocator.huge->slots[index].blocks.chain[index]);
        }
        else {
            allocator.huge->bitmap = allocator.huge->bitmap.bitmap_set(allocator.huge->bitmap, index, 0, allocator.huge->slots[size - 1].capacity);
            return allocate(bytes);
        }
        
        address = allocator.huge->slots[index].blocks.chain[index]->ptr;
        memset(address, 0, bytes);
        return address;
    }
    else address = allocator_huge_push_to_slot(&allocator.huge->slots[index], index, allocator.huge->slots[size - 1].capacity, bytes);
    if (address) memset(address, 0, bytes);  
    return address;
}

[[gnu::flatten]] /* inlines callee functions if possible */
[[gnu::pure]]
FORCE_INLINE void* coalescing(size_t bytes) {
    if ((bytes & (bytes - 1)) != 0)  bytes = alignment(bytes, alignof(max_align_t));
    
    for (size_t i = 0; i < BUCKET_SMALL_CAP; i++) {
        bucket_t* b = &allocator.bucket.small[i];
        if (b->arena && b->flag == 0x0 && b->arena->flag == 0x0) {
            if (is_mergeable(&b->blocks.table, i, bytes) == 0x01) {
                merge(&b->blocks, i, bytes);
                if (b->blocks.chain[i] && b->blocks.chain[i]->bytes == bytes) return b->blocks.chain[i]->ptr;
            }
        }
    }

    for (size_t i = 0; i < BUCKET_MEDIUM_CAP; i++) {
        bucket_t* b = &allocator.bucket.medium[i];
        if (b->arena && b->flag == 0x0 && b->arena->flag == 0x0) {
            if (is_mergeable(&b->blocks.table, i, bytes) == 0x01) {
                merge(&b->blocks, i, bytes);
                if (b->blocks.chain[i] && b->blocks.chain[i]->bytes == bytes) return b->blocks.chain[i]->ptr;
            }
        }
    }

    for (size_t i = 0; i < BUCKET_LARGE_CAP; i++) {
        bucket_t* b = &allocator.bucket.large[i];
        if (b->arena && b->flag == 0x0 && b->arena->flag == 0x0) {
            if (is_mergeable(&b->blocks.table, i, bytes) == 0x01) {
                merge(&b->blocks,  i, bytes);
                if (b->blocks.chain[i] && b->blocks.chain[i]->bytes == bytes) return b->blocks.chain[i]->ptr;
            }
        }
    }
    return NULL;
}

FORCE_INLINE void debug_allocator(const size_t bytes) {
    printf("allocator.allocate: Error, failed to allocate memory for %zu\n", bytes);
    printf("Printing out information....\n");
    if (allocator.arena) {
        printf("Allocator arena state values are: arena = [ %p ], next = [ %p ], res = [ %p ], curr = [ %zu ], prev = [ %zu ], flag = [ %#0x ]\n", 
            allocator.arena, allocator.arena->next, allocator.arena->res, allocator.arena->curr, allocator.arena->prev, allocator.arena->flag);
    }
    for (size_t i = 0; i < BUCKET_SMALL_CAP; i++) {
        bucket_t b = allocator.bucket.small[i];
        if (b.arena) {
            printf("Allocator Bucket Small [ %zu ] state values are: arena = [ %p ], next = [ %p ], res = [ %p ], curr = [ %zu ], prev = [  %zu ], flag = [ %#0x ] bucket_flag = [ %#0x ]\n", 
            i, b.arena, b.arena->next, b.arena->res, b.arena->curr, b.arena->prev, b.arena->flag, b.flag);
            entry_table_t table = atomic_load_explicit(&b.blocks.table, memory_order_relaxed);
            if (table.byte_entries) {
                printf("Entries at [ %zu ] values are:\n", i);
                debug_entry_table_t(0x0, &table, i);
            }
        }
    }
    for (size_t i = 0; i < BUCKET_MEDIUM_CAP; i++) {
        bucket_t b = allocator.bucket.medium[i];
        if (b.arena) {
            printf("Allocator Bucket Medium [ %zu ] state values are: arena = [ %p ], next = [ %p ], res = [ %p ], curr = [ %zu ], prev = [  %zu ], flag = [ %#0x ] bucket_flag = [ %#0x ]\n", 
            i, b.arena, b.arena->next, b.arena->res, b.arena->curr, b.arena->prev, b.arena->flag, b.flag);
            entry_table_t table = atomic_load_explicit(&b.blocks.table, memory_order_relaxed);
            if (table.byte_entries) {
                printf("Entries at [ %zu ] values are:\n", i);
                debug_entry_table_t(0x0, &table, i);
            }
        }
    }
    for (size_t i = 0; i < BUCKET_LARGE_CAP; i++) {
        bucket_t b = allocator.bucket.large[i];
        if (b.arena) {
            printf("Allocator Bucket Large [ %zu ] state values are: arena = [ %p ], next = [ %p ], res = [ %p ], curr = [ %zu ], prev = [  %zu ], flag = [ %#0x ] bucket_flag = [ %#0x ]\n", 
            i, b.arena, b.arena->next, b.arena->res, b.arena->curr, b.arena->prev, b.arena->flag, b.flag);
            entry_table_t table = atomic_load_explicit(&b.blocks.table, memory_order_relaxed);
            if (table.byte_entries) {
                printf("Entries at [ %zu ] values are:\n", i);
                debug_entry_table_t(0x0, &table, i);
            }
        }
    }
    printf("Printing out threading variable states\n");
    for (size_t i = 0; i < ALLOC_THREAD_POOL_SIZE; i++) {
        printf("Information for thread [ %zu ]\n", i);
        debug_threads(allocator.pool[i]);
    }
}

/**
    * @description: A Free Function that recrusive calls itself if arena is full and moves it forward.
    * @param bytes: The requested bytes.
    * @return: Return a memory address of desired size or big enough to hold x amount of bytes.
               Otherwise, return null, and that will indicate that everything is full.
*/
[[gnu::hot]]
[[gnu::malloc, gnu::malloc(deallocate, 1)]]
[[gnu::flatten]] /* inlines callee functions if possible */
GCC_OPTIMIZE_O0 void* allocate(size_t bytes) {
    char* address = NULL; /* TODO: Transform this into a static atomic type. */
    size_t idx = SIZE_MAX, aligned_bytes = SIZE_MAX;
    int_fast16_t end = 0;
    bucket_t* slot = NULL;

    update_thread_pool(allocator.pool, ALLOC_THREAD_POOL_SIZE);

    if (bytes < BUCKET_LARGE_CAP) slot = find_free_slot(bytes);
    else {
        void* res = coalescing(bytes);
        if (res == NULL) {
            if (!allocator.huge->region) init_huge_allocator();
            return allocator_byte_request(bytes);
        }
        else return res;
    }
    if (slot) {
        if (!slot->arena) {
            if (allocator.arena->curr == 1) slot->arena = allocator.arena;
            else slot->arena = init_arena_t();
        }
        if (slot->flag != 0x01) {
            if (bytes < BUCKET_SMALL_CAP) {
                //if ((bytes & (bytes - 1)) != 0) aligned_bytes = alignment(bytes, alignof(max_align_t)); // TODO: aligning bytes causes a test case to fail
                end = BUCKET_SMALL_CAP;
                address = pop_from_bucket(slot, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes);
                if (address) { memset(address, 0, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes); return address; }
                idx = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
            }
            else if (bytes < BUCKET_MEDIUM_CAP && bytes >= BUCKET_SMALL_CAP) {
                if ((bytes & (bytes - 1)) != 0) aligned_bytes = alignment(bytes, alignof(max_align_t));
                end = BUCKET_MEDIUM_CAP;
                address = pop_from_bucket(slot, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes);
                if (address) { memset(address, 0, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes); return address; }
                idx = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
            }
            else if (bytes < BUCKET_LARGE_CAP && bytes >= BUCKET_MEDIUM_CAP) {
                if ((bytes & (bytes - 1)) != 0) aligned_bytes = alignment(bytes, alignof(max_align_t));
                end = BUCKET_LARGE_CAP;
                address = pop_from_bucket(slot, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes);
                if (address) { memset(address, 0, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes); return address; }
                idx = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
            }

            if (slot->arena->flag != 0x01 && idx != SIZE_MAX) {
                slot->arena = push(slot->arena, bytes);
                if (slot->arena->flag == 0x01) {
                    slot->flag = 0x01;
                    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, idx, 0, (size_t)end);
                    arena_t* full = slot->arena;
                    arena_t* fresh = init_arena_t();
                    fresh->next = full;
                    allocator.arena = fresh;
                    return allocate(bytes);
                }
                address = slot->arena->res;
                size_t offset = (uintptr_t)slot->arena->res - (uintptr_t)slot->arena->chunk;
                set(&slot->blocks.table, idx, offset, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes, 0x01, address);
                memset(address, 0, aligned_bytes != SIZE_MAX ? aligned_bytes : bytes);
                return address;
            }
        }
    }
    else debug_allocator(bytes);
    return NULL;
}

/**
    * @description: A free function that determines if the following parameter that was passed into it is within a specific arena's memory region.
    * @param ptr: A memory address that can or is not apart of a arena. 
    * @return: Returns nothing if 'ptr' is not within any of the arena's memory range.   
*/
[[gnu::hot]]
[[gnu::flatten]] /* inlines callee functions if possible */
[[gnu::nonnull(1)]] /* Compiler might perform optimizations. Disable it using fno-delete-null-pointer-checks */
GCC_OPTIMIZE_O0 void deallocate(void* ptr) {

    update_thread_pool(allocator.pool, ALLOC_THREAD_POOL_SIZE);

    bucket_t* slot = NULL;
    slot = find_slot(ptr);
    if (!slot) {
        if (!allocator.huge || !allocator.huge->region) return;

        const size_t size = allocator.huge->slots[allocator.huge->slot_cap - 1].capacity;
        uintptr_t p = (uintptr_t)ptr, base = (uintptr_t)allocator.huge->region;
        if (p < base || p >= base + (uintptr_t)size) return;
        else if ((p - base) % size != 0) return;

        size_t idx = (size_t)((p - base) / HUGE_PAGE_SIZE);
        size_t offset = p - (uintptr_t)allocator.huge->region;

        if (idx > allocator.huge->slot_cap) return;
        offset_entries_t* onode = get_entry_t_by_offset(&allocator.huge->slots[idx].blocks.table, idx, offset, 0x01);
        
        if (!onode) return;
        else if (allocator.huge->slots[idx].space > (onode->offset + onode->bytes->bytes)) {
            allocator.huge->slots[idx].space -= (onode->offset + onode->bytes->bytes);
            entry_table_t table = atomic_load_explicit(&allocator.huge->slots[idx].blocks.table, memory_order_relaxed);
            const size_t hs_offset = hash_mix(offset) % table.inner_count[idx];
            if (table.offset_entries[idx] && table.offset_entries[idx][hs_offset]->next == NULL) {
                allocator.huge->slots[idx].space = 0;
                destroy(&allocator.huge->slots[idx].blocks.table, idx, onode->offset, onode->bytes->bytes);
                table_entry_free_pages(&table);
            } else {
                update(&allocator.huge->slots[idx].blocks.table, idx, onode->offset, onode->bytes->bytes, 0x0);
            }
        } else {
            entry_table_t table = atomic_load_explicit(&allocator.huge->slots[idx].blocks.table, memory_order_relaxed);
            debug_entry_table_t(0x0, &table, idx);
        }
        return;
    }

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

    const atomic_size_t offset = (size_t)((uintptr_t)ptr - (uintptr_t)slot->arena->chunk);
    const atomic_size_t index = find_bucket_index(slot);
    offset_entries_t* offset_entry = get_entry_t_by_offset(&slot->blocks.table, atomic_load_explicit(&index, memory_order_relaxed), atomic_load_explicit(&offset, memory_order_relaxed), 0x01);
    
    if (offset_entry) {
        const size_t bytes = offset_entry->bytes->bytes;
        push_to_bucket(slot, offset);
        if (slot->arena && slot->arena->chunk) memset(ptr, 0xFF, bytes);

        while(!atomic_load_explicit(&allocator.huge->done, memory_order_acquire)){}
        threads_t* t1 = find_thread_t(allocator.pool, ALLOC_THREAD_POOL_SIZE);
        
        if (!t1) __rewind(slot);
        else {
            atomic_store_explicit(&allocator.huge->done, 0, memory_order_release);
            routine_metadata(t1, 2, slot, &allocator.huge->done);
            create_thread(t1, thread_rewind);  
        }
    } else {
        while(!atomic_load_explicit(&allocator.huge->done, memory_order_acquire)){}
        threads_t* t1 = find_thread_t(allocator.pool, ALLOC_THREAD_POOL_SIZE);
        if (!t1) update_block_t_by_offset(&slot->blocks, atomic_load_explicit(&index, memory_order_relaxed), atomic_load_explicit(&offset, memory_order_relaxed), 0x01);
        else {
            atomic_store_explicit(&allocator.huge->done, 0, memory_order_release);
            routine_metadata(t1, 5, &slot->blocks,
                (void*)(uintptr_t)atomic_load_explicit(&index, memory_order_relaxed),
                (void*)(uintptr_t)atomic_load_explicit(&offset, memory_order_relaxed),
                (void*)(uintptr_t)0x01,
                &allocator.huge->done);
            create_thread(t1, thread_update_block_t_by_offset);
        }
    }
}

[[gnu::cold]]
[[gnu::flatten]]
inline void init_allocator_t() {
    #if BENCHMARK_ENV == 1
        init_benchmark_allocator_t();
    #endif
    if (!allocator.bitmap.bits)  {
        init_logger_t();
        init_bitmap_t(&allocator.bitmap, BITMAP_SIZE);
        allocator_init_arena_t();
        allocator_init_buckets_t();
        allocator.huge = aligned_alloc(alignof(huge_block_allocator_t), sizeof(huge_block_allocator_t));
        memset(allocator.huge, 0, sizeof(huge_block_allocator_t));
        allocator_init_threads_t();
        if (!allocator.huge) return;
    }

    if (!allocator.allocate) {
        allocator.allocate   = allocate;
        allocator.deallocate = deallocate;
    }
}

#if BENCHMARK_ENV == 1
    FORCE_INLINE void init_benchmark_allocator_t() {
        /* Entry functions */
        benchmark_allocator.get_entry_t_by_bytes     = get_entry_t_by_bytes;
        benchmark_allocator.get_entry_t_by_offset    = get_entry_t_by_offset;
        benchmark_allocator.update                   = update;
        benchmark_allocator.destroy                  = destroy;
        benchmark_allocator.clean                    = clean;

        /* Block functions */
        benchmark_allocator.is_mergeable             = is_mergeable;
        benchmark_allocator.merge                    = merge;
        benchmark_allocator.update_block_t_by_offset = update_block_t_by_offset;
        benchmark_allocator.get_block_t_by_offset    = get_block_t_by_offset;
        benchmark_allocator.block_t_dctor            = block_t_dctor;

        /* Coalescing */
        benchmark_allocator.coalescing               = coalescing;

        /* Bucket functions */
        benchmark_allocator.find_slot                = find_slot;
        benchmark_allocator.push_to_bucket           = push_to_bucket;
        benchmark_allocator.pop_from_bucket          = pop_from_bucket;
        benchmark_allocator.bucket_t_dctor           = bucket_t_dctor;
        benchmark_allocator.__rewind                 = __rewind;

    }
#endif

/**
    * @description: Free function that will refuse if this single allocation would eat more than half of currently-free RAM. 
    * @param bytes: The requested amount of bytes 
    * @return returns 0x01 if successful, otherwise, 0x0
*/
FORCE_INLINE unsigned char overcommit(const size_t bytes) {
    struct sysinfo info;
    if (sysinfo(&info) != 0) return 0x0;
    unsigned long available = (unsigned long)info.freeram * (unsigned long)info.mem_unit;
    return (bytes < available / 2) ? 0x1 : 0x0;
}

[[gnu::flatten]]
[[gnu::nonnull(1)]]
FORCE_INLINE void allocator_huge_resize_slots(huge_slot_t* slot, const size_t size) {

    size_t old = slot->capacity;
    huge_slot_t* n_slot = size < ALLOC_THRESHOLD ? aligned_alloc(alignof(huge_slot_t), size * sizeof(huge_slot_t)) : shared_address(NULL, (size_t)MAX_HUGE_SLOTS * sizeof(huge_slot_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0); ;
    if (!n_slot) return;
    else {
        int res = madvise(n_slot,  size * sizeof(huge_slot_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) { 
            if (n_slot) munmap_address(n_slot, size, __FILE__, __LINE__);
            n_slot = NULL;
            return; 
        }
    }

    memset(n_slot, 1, offsetof(huge_slot_t, space) * size);
    memset(n_slot, size, offsetof(huge_slot_t, capacity) * size);
    memcpy(n_slot, slot, old);
    size < ALLOC_THRESHOLD ? free(slot) : munmap_address(slot, old, __FILE__,  __LINE__);
    slot = NULL;
    allocator.huge->slots = n_slot;
    allocator.huge->slot_cap = size;
}

[[gnu::flatten]]
[[gnu::nonnull(1)]]
FORCE_INLINE void allocator_huge_resize_chunk(char* chunk, const size_t old_len, const size_t new_len) { 
    chunk = remap_address(chunk, old_len, new_len);
    if (!chunk) return;
    allocator.huge->allocator_cap = new_len;
}

[[gnu::hot]]
[[gnu::flatten]]
[[gnu::nonnull(1)]]
FORCE_INLINE void* allocator_huge_push_to_slot(huge_slot_t* slot, const size_t index, const size_t end, size_t bytes) {
    char* address = NULL;
    uintptr_t raw = (uintptr_t)allocator.huge->region + (uintptr_t)(slot->space == 0 ? 1 : slot->space);
    uintptr_t offset = alignment(raw, bytes);
    offset -= (uintptr_t)allocator.huge->region;
    if (slot->space < slot->capacity) {
        slot->space = bytes + offset;
        address = allocator.huge->region + offset;
        set(&slot->blocks.table, index, slot->space, bytes, 0x01, address);
        return address;
    }
    else {
        allocator.huge->bitmap = allocator.huge->bitmap.bitmap_set(allocator.huge->bitmap, index, 0, end);
        return allocate(bytes);
    }
}

FORCE_INLINE void allocator_huge_update_slots(const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) { 
        allocator.huge->slots[i].space = 1;
        allocator.huge->slots[i].capacity = MAX_HUGE_SLOTS;
    }
}

[[gnu::cold]]
[[gnu::flatten]]
FORCE_INLINE void init_huge_allocator(void) {
    init_bitmap_t(&allocator.huge->bitmap, MAX_HUGE_SLOTS);
    allocator.huge->region = shared_address(NULL, (size_t)MAX_HUGE_SLOTS * HUGE_PAGE_SIZE, PROT_WRITE | PROT_READ,  MAP_HUGETLB | ((size_t)__builtin_ctzll(HUGE_PAGE_SIZE) << 26) | MAP_NORESERVE, -1, 0);
    if (allocator.huge->region == MAP_FAILED) { allocator.huge->region = NULL; return; }
    
    while(!atomic_load_explicit(&allocator.huge->done, memory_order_acquire)){}
    allocator.huge->allocator_cap = (size_t)MAX_HUGE_SLOTS * HUGE_PAGE_SIZE;
    
    allocator.huge->slots = shared_address(NULL, (size_t)MAX_HUGE_SLOTS * sizeof(huge_slot_t), PROT_WRITE | PROT_READ, MAP_NORESERVE, -1, 0); 
    if (allocator.huge->slots == MAP_FAILED) { 
        if (allocator.huge->region) munmap_address(allocator.huge->region, (size_t)MAX_HUGE_SLOTS * HUGE_PAGE_SIZE, __FILE__,  __LINE__); 
        allocator.huge->region = NULL;
        allocator.huge->slots = NULL; 
        return; 
    } else {
        int res = madvise(allocator.huge->slots, (size_t)MAX_HUGE_SLOTS * sizeof(huge_slot_t), MADV_SEQUENTIAL | MADV_MERGEABLE);
        if (res == -1) {
            munmap_address(allocator.huge->region, (size_t)MAX_HUGE_SLOTS * HUGE_PAGE_SIZE, __FILE__,  __LINE__);
            if (allocator.huge->slots) munmap_address(allocator.huge->slots, (size_t)MAX_HUGE_SLOTS * sizeof(huge_slot_t), __FILE__,  __LINE__);
            allocator.huge->region = NULL;
            allocator.huge->slots = NULL;
            return;
        }
    }

    atomic_store_explicit(&allocator.huge->done, 0, memory_order_release);
    threads_t* t1 = find_thread_t(allocator.pool, ALLOC_THREAD_POOL_SIZE);
    if (!t1) allocator_huge_update_slots(0, MAX_HUGE_SLOTS);
    else {
        routine_metadata(t1, 3, (void*)((uintptr_t)0), (void*)((uintptr_t)MAX_HUGE_SLOTS), &allocator.huge->done);
        create_thread(t1, thread_allocator_huge_update_slots);
    }
    allocator.huge->slot_cap = MAX_HUGE_SLOTS;
}

[[gnu::cold]]
[[gnu::destructor]]
FORCE_INLINE void allocator_dctor() {
    while(!atomic_load_explicit(&allocator.huge->done, memory_order_acquire)){}
    for (size_t i = 0;  i < ALLOC_THREAD_POOL_SIZE; i++) clean_threads(&allocator.pool[i]);
    munmap_address(allocator.pool, ALLOC_THREAD_POOL_SIZE * sizeof(threads_t), __FILE__,  __LINE__);
    allocator.pool = NULL;
    bucket_t_dctor();
    if (allocator.arena) {
        //if (allocator.arena->chunk) munmap_address(allocator.arena->chunk, ARENA_SIZE, __FILE__,  __LINE__);
        munmap_address(allocator.arena, sizeof(arena_t), __FILE__,  __LINE__);
        allocator.arena = NULL;
    }
    clean_bitmap(&allocator.bitmap);
    if (allocator.huge) {
        clean_bitmap(&allocator.huge->bitmap);
        if (allocator.huge->region) munmap_address(allocator.huge->region, allocator.huge->allocator_cap, __FILE__,  __LINE__);
        allocator.huge->region = NULL;
        if (allocator.huge->slots) {
            /*for (size_t i = 0; i < (allocator.huge->slot_cap / sizeof(huge_slot_t*)); i++) {
                if (allocator.huge->slots[i].blocks.chain) {
                    block_t_dctor(&allocator.huge->slots[i].blocks);
                    allocator.huge->slots[i].blocks.chain = NULL;
                }
            }*/
            munmap_address(allocator.huge->slots, allocator.huge->slot_cap, __FILE__,  __LINE__);
        }

        void* old = allocator.huge;
        memset(allocator.huge, 0, sizeof(huge_block_allocator_t));
        if (allocator.huge) free(old);
        allocator.huge = NULL;
    }   
}