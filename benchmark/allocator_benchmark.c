#include "../tests/bench.h"
#include "../allocator/allocator.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/*
#include "../tests/bench.h"
#include "../allocator/allocator.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
*/

typedef struct byte_entries_t {
    void* ptr;
    struct offset_entries_t* offset;
    struct byte_entries_t* next;
    unsigned short bytes;
    unsigned char inuse;
} byte_entries_t;

typedef struct offset_entries_t {
    void* ptr;
    struct byte_entries_t* bytes;
    struct offset_entries_t* next;
    unsigned short offset;
    unsigned char inuse;
} offset_entries_t;

typedef struct entry_table_t {
    byte_entries_t** entries;
    unsigned short bucket_count;
} entry_table_t;

typedef struct blocks_t {
    struct blocks_t** chain;
    struct blocks_t* next;
    entry_table_t*   table;
    void*            ptr;
    unsigned short   bytes;
    unsigned short   offset;
    unsigned short   size;
    unsigned char    inuse;
} blocks_t;

typedef struct bucket_t {
    blocks_t       blocks;
    entry_table_t  table;
    arena_t*       arena;
    unsigned char  flag;
} bucket_t;

typedef struct {
    void*  ptr;
    size_t bytes;
} alloc_entry_t;
alloc_entry_t s_stack[1024];


static inline void print_entries(bucket_t* slot, const int idx) {
    entry_table_t* table = &slot->table;
    int counter = 0;
    if (table) {
        byte_entries_t* bnode = slot->table.entries[idx];
        while (bnode) {
            if (bnode) {
                printf("Entry count: [ %d ], Byte Value is: [ %d ]\n", counter, bnode->bytes);
                if (bnode->offset) {
                    offset_entries_t* onode = bnode->offset;
                    printf("Entry count: [ %d ], Offset Value is: [ %d ]\n", counter, onode->offset);
                }
                bnode = bnode->next;
            }
            counter++;
        }
    }
    else printf("No avialable entries detected!\n");
}

static inline void print_blocks(bucket_t* slot) {
    blocks_t* blocks = &slot->blocks;
    if (blocks) {
        
    }
    else printf("No avialable blocks detected!\n");
}


// Helper function copied from allocator_test.c 
static int indexes[3];
static inline int populate_small_buckets(const int idx, const int start_idx) {
    int new_idx = start_idx;
    for (;;) {
        size_t bytes = (size_t)(rand() % 62) + 1; // TODO: Change this out. 
        s_stack[new_idx].ptr = allocator.allocate(bytes);
        s_stack[new_idx].bytes = bytes;
        if (allocator.bucket.small[idx].flag == 0x01) break;
        new_idx++;
    }
    return new_idx;
}

static inline int populate_medium_buckets(const int idx, const int start_idx) {
    int new_idx = start_idx;
    for (;;) {
        size_t bytes = (size_t)(rand() % 64) + 65;
        if (bytes > 64) {
            s_stack[new_idx].ptr = allocator.allocate(bytes);
            s_stack[new_idx].bytes = bytes;
            if (allocator.bucket.medium[idx].flag == 0x01) break;
            new_idx++;
        }
    }
    return new_idx;
}

static inline int populate_large_buckets(const int idx, const int start_idx) {
    int new_idx = start_idx;
    for (;;) {
        size_t bytes = (size_t)(rand() % 128) + 129;
        if (bytes > 128) {
            s_stack[new_idx].ptr = allocator.allocate(bytes);
            if (allocator.bucket.large[idx].flag == 0x01) break;
            s_stack[new_idx].bytes = bytes;
            new_idx++;
        }
    }
    return new_idx;
}

static inline void clean_small_buckets(const int start_idx, const int idx_end) {
    for (int i = start_idx; i < idx_end; i++) {
        allocator.deallocate(s_stack[i].ptr);
    }
    return;
}

static inline void clean_medium_buckets(const int start_idx, const int idx_end) {
    for (int i = start_idx; i < idx_end; i++) {
        allocator.deallocate(s_stack[i].ptr);
    }
    return;
}

static inline void clean_large_buckets(const int start_idx, const int idx_end) {
    for (int i = start_idx; i < idx_end; i++) {
        allocator.deallocate(s_stack[i].ptr);
    }
    return;
}

BENCHMARK_SCENARIO_REPEATED(SmallBuckets, 30) {
    (void)n;
    byte_entries_t* bnode = NULL;
    offset_entries_t* onode = NULL;
    for (int i = 0; i < indexes[0]; i++) {
        int index = rand() % (indexes[0] - 1);
        
        bnode = BENCH_TIMED("get_entry_t_by_bytes",
            benchmark_allocator.get_entry_t_by_bytes(&allocator.bucket.small[0].table, 0, s_stack[index].bytes, 0x01));
        if (bnode) onode = BENCH_TIMED("get_entry_t_by_offset", benchmark_allocator.get_entry_t_by_offset(&allocator.bucket.small[0].table, 0, (size_t)bnode->offset->offset, 0x01));


        size_t bytes = (size_t)(rand() % 62) + 1;
        bucket_t* slot = BENCH_TIMED("find_slot", benchmark_allocator.find_slot(s_stack[i].ptr));
        if (slot) {
            if (bnode && onode) {
                unsigned char res = BENCH_TIMED("is_mergeable", benchmark_allocator.is_mergeable(&slot->table, 0, bytes));
                if (res) {
                    BENCH_TIMED_VOID("merge", benchmark_allocator.merge(&slot->blocks, &slot->table, 0, bytes));
                    BENCH_TIMED_VOID("update_block_t_by_offset", benchmark_allocator.update_block_t_by_offset(&slot->blocks, 0, onode->offset, 0x0));
                    BENCH_TIMED("get_block_t_by_offset", benchmark_allocator.get_block_t_by_offset(&slot->blocks, 0, onode->offset, 0x0));
                }
                else {
                    if (onode && bnode) {
                        BENCH_TIMED_VOID("update", benchmark_allocator.update(&slot->table, 0, onode->offset, bnode->bytes, 0x0));
                        BENCH_TIMED_VOID("__rewind", benchmark_allocator.__rewind(slot));
                    }
                }
            }
        }
    }

}




int main(void) {
    init_allocator_t();
    printf("\n"
        "  ╔══════════════════════════════════════════════════╗\n"
        "  ║           BUILD CONFIGURATION VALUES             ║\n"
        "  ╚══════════════════════════════════════════════════╝\n"
        "\n"
        "   " ANSI_CYAN "ASAN_STACK_MULTIPLIER" ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "MUTEX_ATTR          "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "THREAD_STATE        "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "USTP                "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "INHERITSCHED        "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "LOGGING             "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "ITEM_SIZE           "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "   " ANSI_CYAN "CLEANER_TIME        "  ANSI_RESET "  →  " ANSI_GREEN "%d\n" ANSI_RESET
        "\n"
        ANSI_YELLOW "  ══════════════════════════════════════════════════\n\n" ANSI_RESET,
        ASAN_STACK_MULTIPLIER,
        MUTEX_ATTR,
        THREAD_STATE,
        USTP,
        INHERITSCHED,
        LOGGING,
        ITEM_SIZE,
        CLEANER_TIME
    );
    
    indexes[0] = populate_small_buckets(0, 0);
    indexes[1] = populate_small_buckets(1, indexes[0]);
    indexes[2] = populate_small_buckets(2, indexes[1]);
    
    /*indexes[0] = populate_medium_buckets(0, indexes[0]);
    indexes[1] = populate_medium_buckets(1, indexes[1]);
    indexes[2] = populate_medium_buckets(2, indexes[2]);
    
    indexes[0] = populate_large_buckets(0, indexes[0]);
    indexes[1] = populate_large_buckets(1, indexes[1]);
    indexes[2] = populate_large_buckets(2, indexes[2]);*/
    return RUN_ALL_BENCHMARKS();
}