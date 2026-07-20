#include "hash_table.h"
#include "../logger/buffer.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdalign.h>
#include <sys/mman.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline
#define HASH_BUCKETS  64

/* bucket_count is dynamic now (post-resize), so the shift amount is
 * derived from it via ctz instead of a compile-time constant. */
FORCE_INLINE unsigned int hash_mix(unsigned int x, unsigned int bucket_count) {
    unsigned int log2_buckets = (unsigned int)__builtin_ctz(bucket_count);
    return (x * 2654435769u) >> (32 - log2_buckets);
}

FORCE_INLINE memory_address_hash_table_t* hash_table_init(void) {
    memory_address_hash_table_t* table = aligned_alloc(alignof(memory_address_hash_table_t), sizeof(memory_address_hash_table_t));
    memset(table, 0, sizeof(memory_address_hash_table_t));
    table->byte_map = mmap(NULL, HASH_BUCKETS * sizeof(byte_entries_t*), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (table->byte_map == MAP_FAILED) return NULL;
    table->offset_map = mmap(NULL, HASH_BUCKETS * sizeof(offset_entries_t*), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (table->offset_map == MAP_FAILED) return NULL;
    int res = madvise(table->byte_map, HASH_BUCKETS * sizeof(byte_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
    if (res == -1) {
        munmap(table->byte_map, sizeof(byte_entries_t*) * HASH_BUCKETS);
        return NULL;
    }
    res = madvise(table->offset_map, HASH_BUCKETS * sizeof(offset_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
    if (res == -1) {
        munmap(table->offset_map, sizeof(offset_entries_t*) * HASH_BUCKETS);
        return NULL;
    }
    memset(table->byte_map, 0, sizeof(byte_entries_t*) * HASH_BUCKETS);
    memset(table->offset_map, 0, sizeof(offset_entries_t*) * HASH_BUCKETS);
    table->bucket_count = HASH_BUCKETS;
    return table;
}

/* Doubles bucket_count and rehashes every existing node into new arrays.
 * Nodes aren't reallocated, only relinked -- doesn't invalidate any
 * pointer a caller is already holding. */
FORCE_INLINE void resize_table(memory_address_hash_table_t* table) {
    unsigned int new_bucket_count = table->bucket_count * 2;

    byte_entries_t** new_byte_map =  mmap(NULL, new_bucket_count * sizeof(byte_entries_t*), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    offset_entries_t** new_offset_map = mmap(NULL, new_bucket_count * sizeof(offset_entries_t*), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (new_byte_map == MAP_FAILED) return;
    else if (table->offset_map == MAP_FAILED) return;
    int res = madvise(new_byte_map, new_bucket_count * sizeof(byte_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
    if (res == -1) {
        munmap(new_byte_map, sizeof(byte_entries_t*) * new_bucket_count);
        return;
    }
    res = madvise(new_offset_map, new_bucket_count * sizeof(offset_entries_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);
    if (res == -1) {
        munmap(new_offset_map, sizeof(offset_entries_t*) * new_bucket_count);
        return;
    }
    memset(new_byte_map, 0, sizeof(byte_entries_t*) * new_bucket_count);
    memset(new_offset_map, 0, sizeof(offset_entries_t*) * new_bucket_count);
    for (unsigned int i = 0; i < table->bucket_count; i++) {
        byte_entries_t* n = table->byte_map[i];
        while (__builtin_expect(n != NULL, 1)) {
            __builtin_prefetch(n->next, 0, 1);
            byte_entries_t* next = n->next;
            unsigned int nb = hash_mix(n->bytes, new_bucket_count);
            n->next = new_byte_map[nb];
            new_byte_map[nb] = n;
            n = next;
        }
    }
    for (unsigned int i = 0; i < table->bucket_count; i++) {
        offset_entries_t* n = table->offset_map[i];
        while (__builtin_expect(n != NULL, 1)) {
            __builtin_prefetch(n->next, 0, 1);
            offset_entries_t* next = n->next;
            unsigned int nb = hash_mix(n->offset, new_bucket_count);
            n->next = new_offset_map[nb];
            new_offset_map[nb] = n;
            n = next;
        }
    }

    munmap(table->byte_map, sizeof(byte_entries_t*) * table->bucket_count);
    munmap(table->offset_map, sizeof(offset_entries_t*) * table->bucket_count);
    table->byte_map = new_byte_map;
    table->offset_map = new_offset_map;
    table->bucket_count = new_bucket_count;
}

/* Resize (if load factor >= 1.0) happens BEFORE inserting, so the new
 * entry hashes against the up-to-date bucket_count. */
memory_address_hash_table_t* set(memory_address_hash_table_t* table, const unsigned int offset, const unsigned int bytes, const unsigned char inuse, void* ptr) {
    if (!table) table = hash_table_init();

    if (table->entry_count >= table->bucket_count) {
        resize_table(table);
    }

    byte_entries_t* bnode = aligned_alloc(alignof(byte_entries_t), sizeof(byte_entries_t));
    memset(bnode, 0, sizeof(byte_entries_t));
    bnode->bytes = bytes;
    bnode->inuse = inuse;
    bnode->ptr = ptr;

    offset_entries_t* onode = aligned_alloc(alignof(offset_entries_t), sizeof(offset_entries_t));
    memset(onode, 0, sizeof(offset_entries_t));
    onode->offset = offset;
    onode->inuse = inuse;
    onode->ptr = ptr;

    bnode->offset = onode;
    onode->bytes = bnode;

    unsigned int bb = hash_mix(bytes, table->bucket_count);
    bnode->next = table->byte_map[bb];
    table->byte_map[bb] = bnode;

    unsigned int ob = hash_mix(offset, table->bucket_count);
    onode->next = table->offset_map[ob];
    table->offset_map[ob] = onode;

    table->entry_count++;
    return table;
}

byte_entries_t* get_entry_t_by_bytes(memory_address_hash_table_t* table, const unsigned int bytes, const unsigned char inuse) {
    byte_entries_t* n = NULL; 
    if (table) n = table->byte_map[hash_mix(bytes, table->bucket_count)];
    while (__builtin_expect(n != NULL, 1)) {
        __builtin_prefetch(n->next, 0, 1);
        unsigned int matches = (n->bytes == bytes) & (n->inuse == inuse);
        if (matches) return n;
        n = n->next;
    }
    return NULL;
}

offset_entries_t* get_entry_t_by_offset(memory_address_hash_table_t* table, const unsigned int offset, const unsigned char inuse) {
    offset_entries_t* n = table->offset_map[hash_mix(offset, table->bucket_count)];
    while (__builtin_expect(n != NULL, 1)) {
        __builtin_prefetch(n->next, 0, 1);
        unsigned int matches = (n->offset == offset) & (n->inuse == inuse);
        if (matches) return n;
        n = n->next;
    }
    return NULL;
}

/* update(): ONLY changes inuse (on the found entry AND its bijected pair).
 * Never frees anything -- that's destroy()'s job now.
 * Contract: exactly one of offset/bytes must be nonzero (the other 0),
 * selecting which map to search by. Both zero or both nonzero -> no-op.
 * bytes-only mode has no way to disambiguate among same-size duplicates
 * (that's the whole reason the bijection exists for offset-mode), so it
 * takes the first match in that bucket's chain -- flagging this as an
 * assumption, since the four-argument signature can't fully specify which
 * of several same-size entries to target without going through offset. */
memory_address_hash_table_t* update(memory_address_hash_table_t* table, const unsigned int offset, const unsigned int bytes, const unsigned char inuse) {
    if (!table) return table;
    if ((offset == 0) == (bytes == 0)) return table; /* invalid: need exactly one */

    if (offset != 0) {
        offset_entries_t* on = table->offset_map[hash_mix(offset, table->bucket_count)];
        while (__builtin_expect(on != NULL, 1)) {
            __builtin_prefetch(on->next, 0, 1);
            if (on->offset == offset) {
                on->inuse = inuse;
                if (on->bytes) on->bytes->inuse = inuse;
                break;
            }
            on = on->next;
        }
    } else {
        byte_entries_t* bn = table->byte_map[hash_mix(bytes, table->bucket_count)];
        while (__builtin_expect(bn != NULL, 1)) {
            __builtin_prefetch(bn->next, 0, 1);
            if (bn->bytes == bytes) {
                bn->inuse = inuse;
                if (bn->offset) bn->offset->inuse = inuse;
                break;
            }
            bn = bn->next;
        }
    }

    return table;
}

/* destroy(): unlinks + frees the entry AND its bijected pair from both
 * maps. Same offset-XOR-bytes selector contract as update(). */
memory_address_hash_table_t* destroy(memory_address_hash_table_t* table, const unsigned int offset, const unsigned int bytes) {
    if (!table) return table;
    if ((offset == 0) == (bytes == 0)) return table;

    //sem_wait(&table->lock);

    offset_entries_t* on = NULL;
    byte_entries_t* bn = NULL;

    if (offset != 0) {
        unsigned int ob = hash_mix(offset, table->bucket_count);
        offset_entries_t* prev = NULL;
        offset_entries_t* cur = table->offset_map[ob];
        while (__builtin_expect(cur != NULL, 1)) {
            __builtin_prefetch(cur->next, 0, 1);
            if (cur->offset == offset) { on = cur; break; }
            prev = cur;
            cur = cur->next;
        }
        if (on) {
            if (prev) prev->next = on->next; else table->offset_map[ob] = on->next;
            bn = on->bytes;
        }
    } else {
        unsigned int bb = hash_mix(bytes, table->bucket_count);
        byte_entries_t* prev = NULL;
        byte_entries_t* cur = table->byte_map[bb];
        while (__builtin_expect(cur != NULL, 1)) {
            __builtin_prefetch(cur->next, 0, 1);
            if (cur->bytes == bytes) { bn = cur; break; }
            prev = cur;
            cur = cur->next;
        }
        if (bn) {
            if (prev) prev->next = bn->next; else table->byte_map[bb] = bn->next;
            on = bn->offset;
        }
    }

    //if (!on && !bn) { sem_post(&table->lock); return table; } /* nothing found */

    /* unlink whichever side we haven't already unlinked above */
    if (offset != 0 && bn) {
        unsigned int bb = hash_mix(bn->bytes, table->bucket_count);
        byte_entries_t* prev = NULL;
        byte_entries_t* cur = table->byte_map[bb];
        while (__builtin_expect(cur != NULL, 1)) {
            __builtin_prefetch(cur->next, 0, 1);
            if (cur == bn) break;
            prev = cur;
            cur = cur->next;
        }
        if (cur) { if (prev) prev->next = cur->next; else table->byte_map[bb] = cur->next; }
    } else if (bytes != 0 && on) {
        unsigned int ob = hash_mix(on->offset, table->bucket_count);
        offset_entries_t* prev = NULL;
        offset_entries_t* cur = table->offset_map[ob];
        while (__builtin_expect(cur != NULL, 1)) {
            __builtin_prefetch(cur->next, 0, 1);
            if (cur == on) break;
            prev = cur;
            cur = cur->next;
        }
        if (cur) { if (prev) prev->next = cur->next; else table->offset_map[ob] = cur->next; }
    }

    if (bn) free(bn);
    if (on) free(on);
    table->entry_count--;

    //sem_post(&table->lock);
    return table;
}

void clean(memory_address_hash_table_t *table) {
    if (!table) return;

    if (table->byte_map) {
        for (unsigned int i = 0; i < table->bucket_count; i++) {
            byte_entries_t* n = table->byte_map[i];
            while (n) {
                byte_entries_t* next = n->next;
                reset_and_free_cstr(1, n);
                n = next;
            }
        }
        unmap_cstr(1, table->byte_map);
    }

    if (table->offset_map) {
        for (unsigned int i = 0; i < table->bucket_count; i++) {
            offset_entries_t* n = table->offset_map[i];
            while (n) {
                offset_entries_t* next = n->next;
                reset_and_free_cstr(1, n);
                n = next;
            }
        }
        unmap_cstr(1, table->offset_map);
    }
    reset_and_free_cstr(1, table);
}