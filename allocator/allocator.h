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
    // we use alignment numerical value for indexing from the allocator function during runtime to return a block_t variable/object that contains:
    // unode: unode is a free list sorted as filo. these nodes are not in use 
    // size: the size of the chain variable/object
    // depth: the depth of the free list i.e unode
    // TODO: In the coalescing function. 
    // -----------------------------------
    // TODO: There are function that will coalesce from the unode, we use bins_t_set function to set the coalesced node instead of storing it in here
    // and de-increment depth, we return the memory address which should fit between a colosseum's or arena's chunk without collission.  
    struct blocks_t** chain;
    _Atomic(struct byte_entries_t*) unode; // universal node and nodes that are not in use get stored here, we use filo on it so if unode is not null, we move the node in the back 
    size_t                          size;
    size_t                          bytes; // the total amount of bytes calculated by each unode 
    int                             depth; // the depth of the not inuse nodes i.e unodes
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