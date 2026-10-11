#ifndef _ALLOCATOR_H_
#define _ALLOCATOR_H_
#include "../colosseum/colosseum.h"
#include <stddef.h>

typedef struct bins_t {
    struct offset_entries_t*** offset_entries;
    struct byte_entries_t*** byte_entries;
    size_t                   size;
} bins_t;

typedef struct blocks_t {
    struct blocks_t** chain;    /* TODO: We can tag/untag chain[i] instead of using inuse. */
    _Atomic(struct blocks_t*)   next;
    _Atomic(void*)              ptr;
    _Atomic(size_t)             bytes;
    _Atomic(size_t)             offset;
    _Atomic(size_t)             size;
    _Atomic(unsigned char)      inuse;
} blocks_t;

typedef struct __attribute__((aligned(64))) allocator_t {
    blocks_t            blocks;    
    bins_t              bins;
    colosseum_t**       colosseum;
    size_t              size;
    void*               (*allocate)(size_t);
    void                (*deallocate)(void*);
    struct threads_t*   pool;
    unsigned char       _pad[16];
} allocator_t;

extern allocator_t allocator;
#if BENCHMARK_ENV == 1
    struct entry_table_t;
    struct byte_entries_t;
    struct offset_entries_t;
    struct blocks_t;
    struct bucket_t;

    typedef struct benchmark_allocator_t {
        /* Entry functions */
        struct byte_entries_t* (*get_entry_t_by_bytes)(struct entry_table_t* table, const size_t idx, const size_t bytes, const unsigned char inuse);
        struct offset_entries_t* (*get_entry_t_by_offset)(struct entry_table_t* table, const size_t idx, const size_t offset, const unsigned char inuse);
        void (*update)(struct entry_table_t* table, const size_t idx, const size_t offset, const size_t bytes, const unsigned char inuse);
        void (*destroy)(struct entry_table_t* table, const size_t idx, const size_t offset, const size_t bytes);
        void (*clean)(struct entry_table_t *table);

        unsigned char (*is_mergeable)(struct entry_table_t* table, const size_t idx, const size_t bytes);
        void (*merge)(struct blocks_t* blocks, struct entry_table_t* table, const size_t idx, const size_t bytes);
        void (*update_block_t_by_offset)(struct blocks_t* blocks, const size_t idx, const size_t offset, const unsigned char inuse);
        struct blocks_t* (*get_block_t_by_offset)(struct blocks_t* blocks, const size_t idx, const size_t offset, const unsigned char inuse);
        void (*block_t_dctor)(struct blocks_t* blocks);

        /* coalescing */
        void* (*coalescing)(const size_t bytes);

        /* bucket functions */
        struct bucket_t* (*find_slot)(void* ptr);
        void (*push_to_bucket)(struct bucket_t* slot, size_t offset);
        void* (*pop_from_bucket)(struct bucket_t* slot, size_t bytes);
        void (*bucket_t_dctor)();
        void (*__rewind)(struct bucket_t* slot);

    } benchmark_allocator_t;
    extern benchmark_allocator_t benchmark_allocator;
#endif

extern void init_allocator_t();



#endif