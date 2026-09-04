#include "../allocator/allocator.h"
#include "./tests.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <sys/types.h>

/*
#include "../allocator/allocator.h"
#include "./tests.h"
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <sys/types.h>
*/

typedef struct {
    void*  ptr;
    size_t bytes;
    int    prev_idx;
    int    idx[4];
} alloc_entry_t;
alloc_entry_t s_stack[1024];

typedef struct {
    void*  ptr;
    size_t bytes;
} alloc_huge_entry_t;
static alloc_huge_entry_t* h_arr = NULL;

typedef struct metadata_t {
    size_t entry_table_size;
    size_t block_chain_size;
    int expected_amount_of_resizes;
} metadata_t;
static metadata_t meta = {0};

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
} entry_table_t;

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

typedef struct bucket_t {
    blocks_t      blocks;
    arena_t*       arena;
    unsigned char  flag;
} bucket_t;

typedef struct huge_slot_t {
    blocks_t       blocks;  
    size_t         capacity; /* Represents the amount of memory it can hold */
    size_t         space;   /* capacity >= space otherwise slot is full */
} huge_slot_t;

typedef struct huge_block_allocator_t {
    bitmap_t        bitmap;
    char*           region;
    huge_slot_t*    slots;
    size_t          slot_cap;
    size_t          allocator_cap;
    atomic_size_t   done; /* Used for lock free syncing threads */
} huge_block_allocator_t;

FORCE_INLINE size_t hash_mix(size_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}


/////////////////////////
/// INIT FUNCTIONS   ///
///////////////////////

static inline void init_allocator_huge_entries(alloc_huge_entry_t* huge, const size_t size);
[[gnu::nonnull(1)]]
static inline void resize_allocator_small_entries(alloc_huge_entry_t* huge, const size_t old_size, const size_t new_size);
[[gnu::nonnull(1)]]
static inline void resize_allocator_huge_entries(alloc_huge_entry_t* small, const size_t old_size, const size_t new_size);
static inline void init_allocator_small_entries(alloc_entry_t* small, const size_t size);
static inline void clean_allocator_entries();


static inline void init_allocator_huge_entries(alloc_huge_entry_t* huge, const size_t size) {
    huge = aligned_alloc(alignof(alloc_huge_entry_t), size * sizeof(alloc_huge_entry_t));
    if (!huge) return;
    memset(huge, 0, size * sizeof(alloc_huge_entry_t));
    h_arr = huge;
}

static inline void init_allocator_small_entries(alloc_entry_t* small, const size_t size) {
    small = aligned_alloc(alignof(alloc_huge_entry_t), size * sizeof(alloc_huge_entry_t));
    if (!small) return;
    memset(small, 0, size * sizeof(alloc_huge_entry_t));
}


static inline void resize_allocator_huge_entries(alloc_huge_entry_t* huge, const size_t old_size, const size_t new_size) {
    void* h_new = aligned_alloc(alignof(alloc_huge_entry_t), new_size * sizeof(alloc_huge_entry_t));
    if (!h_new) return;

    memcpy(h_new, huge, old_size * sizeof(alloc_huge_entry_t));
    void* old = huge;
    memset(huge, 0, old_size * sizeof(alloc_huge_entry_t));
    free(old);
    h_arr = h_new;
}

static inline void resize_allocator_small_entries(alloc_huge_entry_t* small, const size_t old_size, const size_t new_size) {
    void* h_new = aligned_alloc(alignof(alloc_huge_entry_t), new_size * sizeof(alloc_huge_entry_t));
    if (!h_new) return;

    memcpy(h_new, small, old_size * sizeof(alloc_huge_entry_t));
    void* old = small;
    memset(small, 0, old_size * sizeof(alloc_huge_entry_t));
    free(old);
    small = h_new;
}

[[gnu::destructor]]
static inline void clean_allocator_entries() {
    memset(&meta, 0, sizeof(metadata_t));
    size_t size = 0;
    alloc_huge_entry_t* entry = &h_arr[size];
    while(!entry) { size++; entry = &h_arr[size]; }
    void* old = h_arr;
    memset(h_arr, 0, size * sizeof(alloc_huge_entry_t));
    free(old);
}


/////////////////////////
/// HELPER FUNCTIONS ///
///////////////////////


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
        size_t bytes = (size_t)(rand() % 63) + 65;
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
        size_t bytes = (size_t)(rand() % 127) + 129;
        if (bytes > 128 && bytes != 256) {
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

TEST(BitmapSuite, Small) {
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 0, 0, BUCKET_SMALL_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 1, 0, BUCKET_SMALL_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 2, 0, BUCKET_SMALL_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 3, 0, BUCKET_SMALL_CAP);
    const size_t index = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
    EXPECT_EQ(index, 4);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 0, 0, BUCKET_SMALL_CAP);
    const size_t zero = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
    EXPECT_EQ(zero, 0);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 0, 0, BUCKET_SMALL_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 2, 0, BUCKET_SMALL_CAP);
    const size_t two = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
    EXPECT_EQ(two, 2);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 2, 0, BUCKET_SMALL_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 3, 0, BUCKET_SMALL_CAP);
    const size_t three = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
    EXPECT_EQ(three, 3);
    for (unsigned int i = 0; i < BUCKET_SMALL_CAP; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_SMALL_CAP);
}

TEST(PopulateSuite, Small) {
    for (int i = 0; i < 3; i++) {
        s_stack[i].ptr   = allocator.allocate(sizeof(int));
        s_stack[i].bytes = sizeof(int);
    }
    s_stack->idx[0] = populate_small_buckets(0, 3);
    s_stack->idx[0]++;
    EXPECT_EQ(allocator.bucket.small[0].flag, 0x01);
    EXPECT_EQ(allocator.bucket.small[0].arena->flag, 0x01);
    s_stack->idx[1] = populate_small_buckets(1, s_stack->idx[0]);
    s_stack->idx[1]++;
    EXPECT_EQ(allocator.bucket.small[1].flag, 0x01);
    EXPECT_EQ(allocator.bucket.small[1].arena->flag, 0x01);
    s_stack->idx[2] = populate_small_buckets(2, s_stack->idx[1]);
    s_stack->idx[2]++;
    EXPECT_EQ(allocator.bucket.small[2].flag, 0x01);
    EXPECT_EQ(allocator.bucket.small[2].arena->flag, 0x01);
}

TEST(ValidationSuite, Small) {
    int val_1 = 0;
    int val_2 = 1;
    int val_3 = 2;
    int* one   = s_stack[0].ptr;
    int* two   = s_stack[1].ptr;
    int* three = s_stack[2].ptr;
    *one = val_1;
    *two = val_2;
    *three = val_3;
    allocator.deallocate(one);
    EXPECT_EQ(*one, -1);
    allocator.deallocate(two);
    EXPECT_EQ(*two, -1);
    allocator.deallocate(three);
    EXPECT_EQ(*three, -1);
}

TEST(ReuseSuite, Small) {
    int* one   = NULL;
    int* two   = NULL;
    int* three = NULL;
    three = allocator.allocate(sizeof(int));
    EXPECT_EQ(three, s_stack[2].ptr);
    two = allocator.allocate(sizeof(int));
    EXPECT_EQ(two, s_stack[1].ptr);
    one = allocator.allocate(sizeof(int));
    EXPECT_EQ(one, s_stack[0].ptr);
}

TEST(CleanSuite, Small) {
    clean_small_buckets(0, s_stack->idx[0]);
    EXPECT_EQ(allocator.bucket.small[0].arena->curr, 1);
    EXPECT_EQ(allocator.bucket.small[0].arena->flag, 0X0);
    clean_small_buckets(s_stack->idx[0], s_stack->idx[1]);
    EXPECT_EQ(allocator.bucket.small[1].arena->curr, 1);
    clean_small_buckets(s_stack->idx[1], s_stack->idx[2]);
    EXPECT_EQ(allocator.bucket.small[2].arena->curr, 1);
}

TEST(Coalescing, Small) {
    int** arr[4];
    arr[0] = allocator.allocate(sizeof(int));
    arr[1] = allocator.allocate(sizeof(int));
    arr[2] = allocator.allocate(sizeof(int));
    const size_t curr = allocator.bucket.small[0].arena->curr;

    const entry_table_t table = atomic_load_explicit(&allocator.bucket.small[0].blocks.table, memory_order_relaxed);
    const size_t hs_byte = hash_mix(sizeof(int)) % table.inner_count[0];
    EXPECT_NE(table.byte_entries[0][hs_byte], NULL); 
    EXPECT_NE(table.byte_entries[0][hs_byte]->next, NULL);
    if (table.byte_entries[0][hs_byte]->next) EXPECT_NE(table.byte_entries[0][hs_byte]->next->next, NULL);  
    
    allocator.deallocate(arr[0]);
    EXPECT_NE(table.byte_entries[0][hs_byte]->next, NULL);
    EXPECT_NE(table.byte_entries[0][hs_byte]->next->next, NULL);  
    allocator.deallocate(arr[1]);

    EXPECT_EQ(arr[2], table.byte_entries[0][hs_byte]->ptr);
    EXPECT_NE(table.byte_entries[0][hs_byte]->inuse, 0x0);
    EXPECT_NE(table.byte_entries[0][hs_byte]->next, NULL); /* Make sure __rewind does not clear byte_entries. */ 

    arr[3] = allocator.allocate(8);
    EXPECT_EQ(curr, allocator.bucket.small[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (size_t i = 0; i < 4; i++) {
        if (arr[i]) allocator.deallocate(arr[i]);
    } 
    memset(arr, 0, sizeof(int*) * 4);
    EXPECT_EQ(allocator.bucket.small[0].arena->curr, 1);
}

TEST(Bitmap, Medium) {
    for (size_t i = 0; i < 64; i++) allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, i, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 64, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 65, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 66, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 67, 0, BUCKET_MEDIUM_CAP);
    const size_t index = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(index, 68);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 64, 0, BUCKET_MEDIUM_CAP);
    const size_t zero = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(zero, 64);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 64, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 66, 0, BUCKET_MEDIUM_CAP);
    const size_t two = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(two, 66);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 66, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 67, 0, BUCKET_MEDIUM_CAP);
    const size_t three = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(three, 67);
    for (unsigned int i = 0; i < 64; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_MEDIUM_CAP);
    size_t res = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);;
    EXPECT_EQ(res, 0);
}

TEST(PopulateSuite, Medium) {
    for (int i = s_stack->idx[2]; i < (s_stack->idx[2] + 4); i++) {
        /* Increment an extra one, so __rewind does not wipe the stack */
        s_stack[i].ptr   = allocator.allocate(127);
        s_stack[i].bytes = 127;
    }
    s_stack->prev_idx = s_stack->idx[2];
    s_stack->idx[0] = populate_medium_buckets(0, s_stack->idx[2] + 4);
    s_stack->idx[0]++;
    EXPECT_EQ(allocator.bucket.medium[0].flag, 0x01);
    EXPECT_EQ(allocator.bucket.medium[0].arena->flag, 0x01);
    s_stack->idx[1] = populate_medium_buckets(1, s_stack->idx[0]);
    s_stack->idx[1]++;
    EXPECT_EQ(allocator.bucket.medium[1].flag, 0x01);
    EXPECT_EQ(allocator.bucket.medium[1].arena->flag, 0x01);
    s_stack->idx[2] = populate_medium_buckets(2, s_stack->idx[1]);
    s_stack->idx[2]++;
    EXPECT_EQ(allocator.bucket.medium[2].flag, 0x01);
    EXPECT_EQ(allocator.bucket.medium[2].arena->flag, 0x01);
}

TEST(ValidationSuite, Medium) {
    const int idx = s_stack->prev_idx;
    
    int val_1 = 0;
    int val_2 = 1;
    int val_3 = 2;
    
    int* one   = s_stack[idx].ptr;
    int* two   = s_stack[idx + 1].ptr;
    int* three = s_stack[idx + 2].ptr;
    
    *one = val_1;
    *two = val_2;
    *three = val_3;
    
    allocator.deallocate(one);
    EXPECT_EQ(*one, -1);
    
    allocator.deallocate(two);
    EXPECT_EQ(*two, -1);
    
    allocator.deallocate(three);
    EXPECT_EQ(*three, -1);
}

TEST(ReuseSuite, Medium) {
    int* one   = NULL;
    int* two   = NULL;
    int* three = NULL;
    
    const int idx = s_stack->prev_idx + 2;
    const entry_table_t table = atomic_load_explicit(&allocator.bucket.medium[0].blocks.table, memory_order_relaxed);
    const size_t bytes = alignment(s_stack[idx].bytes, alignof(max_align_t));
    const size_t hs_byte = hash_mix(bytes) % table.inner_count[0];
    
    EXPECT_NE(table.byte_entries[0][hs_byte], NULL); 
    EXPECT_NE(table.byte_entries[0][hs_byte]->next, NULL);
    if (table.byte_entries[0][hs_byte]->next) EXPECT_NE(table.byte_entries[0][hs_byte]->next->next, NULL); 
    three = allocator.allocate(127);
    EXPECT_NE(table.byte_entries[0][hs_byte], NULL); 
    EXPECT_EQ(three, s_stack[idx].ptr);
    two = allocator.allocate(127);
    EXPECT_EQ(two, s_stack[idx - 1].ptr);
    one = allocator.allocate(127);
    EXPECT_EQ(one, s_stack[idx - 2].ptr);
}

TEST(CleanSuite, Medium) {
    clean_medium_buckets(0, s_stack->idx[0]);
    EXPECT_EQ(allocator.bucket.medium[0].arena->curr, 1);
    clean_medium_buckets(s_stack->idx[0], s_stack->idx[1]);
    EXPECT_EQ(allocator.bucket.medium[1].arena->curr, 1);
    clean_medium_buckets(s_stack->idx[1], s_stack->idx[2]);
    EXPECT_EQ(allocator.bucket.medium[2].arena->curr, 1);
}

TEST(Coalescing, Medium) {
    int** arr[4];
    arr[0] = allocator.allocate(BUCKET_SMALL_CAP);
    arr[1] = allocator.allocate(BUCKET_SMALL_CAP);
    arr[2] = allocator.allocate(BUCKET_SMALL_CAP); /* An extra allocation will keep the entries alive and well. */
    const size_t curr = allocator.bucket.medium[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(127);
    EXPECT_EQ(curr, allocator.bucket.medium[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (size_t i = 0; i < 4; i++) allocator.deallocate(arr[i]); 
    memset(arr, 0, sizeof(int*) * 4);
    EXPECT_EQ(allocator.bucket.medium[0].arena->curr, 1);
}

TEST(Bitmap, Large) {
    for (size_t i = 0; i < 192; i++) allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, i, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 192, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 193, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 194, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 195, 0, BUCKET_LARGE_CAP);
    const size_t index = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(index, 196);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 192, 0, BUCKET_LARGE_CAP);
    const size_t zero = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(zero, 192);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 192, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 194, 0, BUCKET_LARGE_CAP);
    const size_t two = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(two, 194);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 194, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 195, 0, BUCKET_LARGE_CAP);
    const size_t three = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(three, 195);
    for (size_t i = 0; i < BUCKET_LARGE_CAP; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_LARGE_CAP);
}

TEST(PopulateSuite, Large) {
    const int start = s_stack->idx[2];
    for (int i = start; i < start + 4; i++) {
        s_stack[i].ptr   = allocator.allocate(255);
        s_stack[i].bytes = 255;
    }
    s_stack->prev_idx = start;
    s_stack->idx[0] = populate_large_buckets(0, start + 4);
    s_stack->idx[0]++;
    EXPECT_EQ(allocator.bucket.large[0].flag, 0x01);
    EXPECT_EQ(allocator.bucket.large[0].arena->flag, 0x01);
    s_stack->idx[1] = populate_large_buckets(1, s_stack->idx[0]);
    s_stack->idx[1]++;
    EXPECT_EQ(allocator.bucket.large[1].flag, 0x01);
    EXPECT_EQ(allocator.bucket.large[1].arena->flag, 0x01);
    s_stack->idx[2] = populate_large_buckets(2, s_stack->idx[1]);
    s_stack->idx[2]++;
    EXPECT_EQ(allocator.bucket.large[2].flag, 0x01);
    EXPECT_EQ(allocator.bucket.large[2].arena->flag, 0x01);
}

TEST(ValidationSuite, Large) {
    const int idx = s_stack->prev_idx;
    int val_1 = 0;
    int val_2 = 1;
    int val_3 = 2;

    int* one   = s_stack[idx].ptr;
    int* two   = s_stack[idx + 1].ptr;
    int* three = s_stack[idx + 2].ptr;
    
    *one = val_1;
    *two = val_2;
    *three = val_3;
    
    allocator.deallocate(one);
    EXPECT_EQ(*one, -1);
    
    allocator.deallocate(two);
    EXPECT_EQ(*two, -1);
    
    allocator.deallocate(three);
    EXPECT_EQ(*three, -1);
}

TEST(ReuseSuite, Large) {
    int* one   = NULL;
    int* two   = NULL;
    int* three = NULL;
    const int idx = s_stack->prev_idx + 2;
    three = allocator.allocate(255);
    EXPECT_EQ(three, s_stack[idx].ptr);
    two = allocator.allocate(255);
    EXPECT_EQ(two, s_stack[idx - 1].ptr);
    one = allocator.allocate(255);
    EXPECT_EQ(one, s_stack[idx - 2].ptr);
}

TEST(CleanSuite, Large) {
    clean_large_buckets(0, s_stack->idx[0]);
    EXPECT_EQ(allocator.bucket.large[0].arena->curr, 1);
    clean_large_buckets(s_stack->idx[0], s_stack->idx[1]);
    EXPECT_EQ(allocator.bucket.large[1].arena->curr, 1);
    clean_large_buckets(s_stack->idx[1], s_stack->idx[2]);
    EXPECT_EQ(allocator.bucket.large[2].arena->curr, 1);
}

TEST(Coalescing, Large) {
    int** arr[4];
    arr[0] = allocator.allocate(128);
    arr[1] = allocator.allocate(128);
    arr[2] = allocator.allocate(128); /* An extra allocation will keep the entries alive and well. */
    const size_t curr = allocator.bucket.large[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(256);
    EXPECT_EQ(curr, allocator.bucket.large[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (size_t i = 0; i < 4; i++) allocator.deallocate(arr[i]); 
    memset(arr, 0, sizeof(int*) * 4);
    EXPECT_EQ(allocator.bucket.large[0].arena->curr, 1);
}

TEST(Coalescing, Any) {
    int** arr[4];
    arr[0] = allocator.allocate(255);
    arr[1] = allocator.allocate(255);
    arr[2] = allocator.allocate(255); // An extra allocation will keep the entries alive and well.
    const size_t offset = allocator.bucket.large[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(510);
    EXPECT_EQ(offset, allocator.bucket.large[0].arena->curr);
    EXPECT_NE(arr[3], NULL);

    for (size_t i = 0; i < 4; i++) allocator.deallocate(arr[i]); 
    EXPECT_EQ(allocator.bucket.large[0].arena->curr, 1);
    memset(arr, 0, sizeof(int*) * 4);
}

TEST(Bitmap, HUGE) {
    bitmap_t bitmap;
    bitmap.n_bytes = 4096;
    init_bitmap_t(&bitmap, bitmap.n_bytes);
    for (size_t i = BUCKET_LARGE_CAP; i < bitmap.n_bytes; i++) {
        const unsigned char open = bitmap.bits[i / CHAR_BIT];
        bitmap = bitmap.bitmap_set(bitmap, i, 0, bitmap.n_bytes);
        const unsigned char close = bitmap.bits[i / CHAR_BIT];
        EXPECT_NE(open, close);
    }
    for (size_t i = BUCKET_LARGE_CAP; i < bitmap.n_bytes; i++) {
        const size_t idx = bitmap.bitmap_test(bitmap, i, bitmap.n_bytes - 1);
        if (idx != (size_t)-1) printf("Value of index i is: [ %zu ]\n ", i);
        else EXPECT_EQ(idx, (size_t)-1);
    }
    clean_bitmap(&bitmap);
}

TEST(Allocate, HUGE) {
    init_allocator_huge_entries(h_arr, MAX_HUGE_SLOTS);

    size_t space = MAX_HUGE_SLOTS, accumulated = 0;
    int amount_of_slots = 0;
    
    size_t index = 0;
    size_t actual_bitmap_index = 0;
    size_t bitmap_index_next = 1, current_bitmap_index = allocator.bitmap.bitmap_test(allocator.bitmap, 0, allocator.bitmap.n_bytes);    
    for (size_t i = 512; amount_of_slots <= 3; i+=2, index++, amount_of_slots++) {
        if (accumulated < space) accumulated = accumulated + i; 
        
        if (accumulated >= space) {
            EXPECT_LT(current_bitmap_index, bitmap_index_next);
            EXPECT_LE(allocator.huge->slot_cap, space);
            
            bitmap_index_next = bitmap_index_next + 1;
            actual_bitmap_index = actual_bitmap_index + 1;
            current_bitmap_index = allocator.bitmap.bitmap_test(allocator.bitmap, actual_bitmap_index, allocator.bitmap.n_bytes);
            
            resize_allocator_huge_entries(h_arr, space, space * 2);
            space = space * 2;       
            accumulated = 0;
        }
        else {
            /* Force duplicated entries */
            #pragma GCC unroll 3
            for (int j = 0; j < 3; j++, index++) {
                if (accumulated + i < space) {
                    h_arr[index].ptr = allocator.allocate(i);
                    h_arr[index].bytes = alignment(i, alignof(max_align_t));
                    accumulated = accumulated + i;
                } else break;
            }
        }

        if (accumulated < space) {
            h_arr[index].ptr = allocator.allocate(i);
            h_arr[index].bytes = alignment(i, alignof(max_align_t));
        }
    }
}


TEST(Free, HUGE) {
    alloc_huge_entry_t* huge = &h_arr[0];
    EXPECT_NE(huge, NULL);

    size_t index = 0, accumulated = 0;
    while (!huge || accumulated != MAX_HUGE_SLOTS) {
        allocator.deallocate(huge->ptr);
        accumulated = accumulated + huge->bytes;
        index++;
        huge = &h_arr[index];
    }
}

TEST(Coalescing, HUGE) {
    
}

TEST(Allocator, Overload) {
    //size_t small = BUCKET_SMALL_CAP, medium = BUCKET_MEDIUM_CAP, large = BUCKET_LARGE_CAP;
    //bucket_t b_small = allocator.bucket.small[small - 1], b_medium = allocator.bucket.medium[medium - 1], b_large = allocator.bucket.large[large - 1];
    //huge_slot_t h_slot = allocator.huge->slots[allocator.huge->allocator_cap - 1];
    // 1. Fill up all of the buckets such as small, medium, and large
    // 2. Fill up allocator huge all the way

}

TEST(Allocator, HugeResize) {
    // We are going to test and see the code for resizing is valid and works for huge 
    // Either A) we can find the closest slot that is full, unmark it, and resize that, or B) resize the slots
    // Going with option B as it seems more fitting based on how everything is built right now 8/23/26.  
}

TEST(Allocator, FREELIST) {
    // A Brand new feature that works for `bytes < LARGE_BUCKET_CAP`, since instead of using a raw memory pointer that points to arena. 
    // It will only will work iff a specific region of buckets is full. 
        // We are going to use a function that returns a thread_local 
    // It will:
        // require a handful of functions and a data member field called `depth`.
        // In order for this to work, we need to go with option A) which is: we can find the closest slot that is full, unmark it, and resize the slot
        // But instead of resizing it, we create a new arena node, move the full to the back, increment the data member field.
    // `find_slot` will be used. Since we have T0, if `depth` is not zero, we find a thread and thread through the nodes. If there is a match found by T1
        // We need to signal to T0 to stop and return the atomic type. Otherwise, continue. 
}




int main(void) {
    srand(time(NULL));
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
    return RUN_ALL_TESTS();
}