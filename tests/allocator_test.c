#include "../allocator/allocator.h"
#include "./tests.h"
#include <stddef.h>
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
} alloc_entry_t;
alloc_entry_t s_stack[1024];

typedef struct byte_entries_t {
    void* ptr;
    struct offset_entries_t* offset;
    struct byte_entries_t* next;
    size_t                 bytes;
    unsigned char inuse;
} byte_entries_t;

typedef struct offset_entries_t {
    void* ptr;
    struct byte_entries_t* bytes;
    struct offset_entries_t* next;
    size_t        offset;
    unsigned char inuse;
} offset_entries_t;

typedef struct entry_table_t {
    byte_entries_t** entries;
    size_t           bucket_count;
} entry_table_t;

typedef struct blocks_t {
    entry_table_t   table;
    struct blocks_t** chain;
    struct blocks_t* next;
    void*            ptr;
    size_t           bytes;
    size_t           offset;
    size_t           size;
    unsigned char    inuse;
} blocks_t;

typedef struct bucket_t {
    blocks_t      blocks;
    arena_t*       arena;
    unsigned char  flag;
} bucket_t;


// -- HELPER FUNCTIONS & Variables
static int indexes[3];
static inline int populate_small_buckets(const int idx, const int start_idx) {
    int new_idx = start_idx;
    for (;;) {
        size_t bytes = (rand() % 62) + 1; // TODO: Change this out. 
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
        size_t bytes = (rand() % 64) + 65;
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
        size_t bytes = (rand() % 128) + 129;
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

static inline void debug_entry_table_t(const unsigned char mode, entry_table_t* table, const int idx) {
    if (mode == 0x0) {
        byte_entries_t* bn = table->entries[idx];
        while (bn->next != NULL) {
            if (bn->offset->offset) {
                printf("Offset value is: %zu\n", bn->offset->offset);
                printf("Inuse Value is: %#0x\n", bn->inuse);
            }
            bn = bn->next;
        }
    }
    else if (mode == 0x01) {
        byte_entries_t* n = table->entries[idx];
        while (n->next != NULL) {
            printf("Offset value is: %zu\n", n->bytes);
            n = n->next;
        }
    }
}

static inline void debug_entry_table_full(entry_table_t* table, const int idx) {
    byte_entries_t* bn = table->entries[idx];
    int count = 0;
    while (bn) {
        printf("entry: offset=%zu  inuse=%u  bytes=%zu\n", bn->offset->offset, bn->inuse, bn->bytes);
        bn = bn->next;
        count++;
    }
    printf("total entries remaining: %d\n", count);
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
    const int two = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
    EXPECT_EQ(two, 2);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 2, 0, BUCKET_SMALL_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 3, 0, BUCKET_SMALL_CAP);
    const int three = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_SMALL_CAP);
    EXPECT_EQ(three, 3);
    for (unsigned int i = 0; i < BUCKET_SMALL_CAP; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_SMALL_CAP);
}

TEST(PopulateSuite, Small) {
    for (int i = 0; i < 3; i++) {
        s_stack[i].ptr   = allocator.allocate(sizeof(int));
        s_stack[i].bytes = sizeof(int);
    }
    indexes[0] = populate_small_buckets(0, 3);
    indexes[0]+= 3;
    EXPECT_EQ(allocator.bucket.small[0].flag, 0x01);
    EXPECT_EQ(allocator.bucket.small[0].arena->flag, 0x01);
    indexes[1] = populate_small_buckets(1, indexes[0]);
    indexes[1]++;
    EXPECT_EQ(allocator.bucket.small[1].flag, 0x01);
    EXPECT_EQ(allocator.bucket.small[1].arena->flag, 0x01);
    indexes[2] = populate_small_buckets(2, indexes[1]);
    indexes[2]++;
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
    clean_small_buckets(0, indexes[0]);
    if (allocator.bucket.small[0].arena->curr != 1) debug_entry_table_full( &allocator.bucket.small[1].blocks.table, 0);
    EXPECT_EQ(allocator.bucket.small[0].arena->curr, 1);
    EXPECT_EQ(allocator.bucket.small[0].arena->flag, 0X0);
    clean_small_buckets(indexes[0], indexes[1]);
    if (allocator.bucket.small[1].arena->curr != 1) debug_entry_table_full( &allocator.bucket.small[1].blocks.table, 1);
    EXPECT_EQ(allocator.bucket.small[1].arena->curr, 1);
    clean_small_buckets(indexes[1], indexes[2]);
    if (allocator.bucket.small[2].arena->curr != 1) debug_entry_table_full( &allocator.bucket.small[1].blocks.table, 2);
    EXPECT_EQ(allocator.bucket.small[2].arena->curr, 1);
}

TEST(Coalescing, Small) {
    int** arr[4];
    arr[0] = allocator.allocate(sizeof(int));
    arr[1] = allocator.allocate(sizeof(int));
    arr[2] = allocator.allocate(sizeof(int));
    const unsigned int curr = allocator.bucket.small[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(8);
    EXPECT_EQ(curr, allocator.bucket.small[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (unsigned int i = 0; i < 4; i++) {
        if (arr[i]) allocator.deallocate(arr[i]);
    } 
    memset(arr, 0, sizeof(int*) * 4);
    EXPECT_EQ(allocator.bucket.small[0].arena->curr, 1);
}

TEST(Bitmap, Medium) {
    for (int i = 0; i < 64; i++) allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, i, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 64, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 65, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 66, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 67, 0, BUCKET_MEDIUM_CAP);
    const int index = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(index, 68);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 64, 0, BUCKET_MEDIUM_CAP);
    const int zero = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(zero, 64);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 64, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 66, 0, BUCKET_MEDIUM_CAP);
    const int two = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(two, 66);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 66, 0, BUCKET_MEDIUM_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 67, 0, BUCKET_MEDIUM_CAP);
    const int three = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);
    EXPECT_EQ(three, 67);
    for (unsigned int i = 0; i < 64; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_MEDIUM_CAP);
    int res = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_MEDIUM_CAP);;
    EXPECT_EQ(res, 0);
}

TEST(PopulateSuite, Medium) {
    for (int i = 64; i < 67; i++) {
        s_stack[i].ptr   = allocator.allocate(128);
        s_stack[i].bytes = 128;
    }
    indexes[0] = populate_medium_buckets(0, 67);
    indexes[0] += 3;
    EXPECT_EQ(allocator.bucket.medium[0].flag, 0x01);
    EXPECT_EQ(allocator.bucket.medium[0].arena->flag, 0x01);
    indexes[1] = populate_medium_buckets(1, indexes[0]);
    indexes[1]++;
    EXPECT_EQ(allocator.bucket.medium[1].flag, 0x01);
    EXPECT_EQ(allocator.bucket.medium[1].arena->flag, 0x01);
    indexes[2] = populate_medium_buckets(2, indexes[1]);
    indexes[2]++;
    EXPECT_EQ(allocator.bucket.medium[2].flag, 0x01);
    EXPECT_EQ(allocator.bucket.medium[2].arena->flag, 0x01);
}

TEST(ValidationSuite, Medium) {
    int val_1 = 0;
    int val_2 = 1;
    int val_3 = 2;
    int* one   = s_stack[64].ptr;
    int* two   = s_stack[65].ptr;
    int* three = s_stack[66].ptr;
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
    three = allocator.allocate(128);
    EXPECT_EQ(three, s_stack[66].ptr);
    two = allocator.allocate(128);
    EXPECT_EQ(two, s_stack[65].ptr);
    one = allocator.allocate(128);
    EXPECT_EQ(one, s_stack[64].ptr);
}

TEST(CleanSuite, Medium) {
    clean_medium_buckets(0, indexes[0]);
    if (allocator.bucket.medium[0].arena->curr != 1) debug_entry_table_full(&allocator.bucket.medium[0].blocks.table, 0);
    EXPECT_EQ(allocator.bucket.medium[0].arena->curr, 1);
    clean_medium_buckets(indexes[0], indexes[1]);
    if (allocator.bucket.medium[1].arena->curr != 1) debug_entry_table_full(&allocator.bucket.medium[1].blocks.table, 1);
    EXPECT_EQ(allocator.bucket.medium[1].arena->curr, 1);
    clean_medium_buckets(indexes[1], indexes[2]);
    if (allocator.bucket.medium[2].arena->curr != 1) debug_entry_table_full(&allocator.bucket.medium[2].blocks.table, 2);
    EXPECT_EQ(allocator.bucket.medium[2].arena->curr, 1);
}

TEST(Coalescing, Medium) {
    int** arr[4];
    arr[0] = allocator.allocate(BUCKET_SMALL_CAP);
    arr[1] = allocator.allocate(BUCKET_SMALL_CAP);
    arr[2] = allocator.allocate(BUCKET_SMALL_CAP); /* An extra allocation will keep the entries alive and well. */
    const unsigned int curr = allocator.bucket.medium[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(128);
    EXPECT_EQ(curr, allocator.bucket.medium[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (unsigned int i = 0; i < 4; i++) allocator.deallocate(arr[i]); 
    memset(arr, 0, sizeof(int) * 4);
    EXPECT_EQ(allocator.bucket.medium[0].arena->curr, 1);
}

TEST(Bitmap, Large) {
    for (unsigned int i = 0; i < 192; i++) allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, i, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 192, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 193, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 194, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 195, 0, BUCKET_LARGE_CAP);
    const int index = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(index, 196);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 192, 0, BUCKET_LARGE_CAP);
    const int zero = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(zero, 192);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 192, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 194, 0, BUCKET_LARGE_CAP);
    const int two = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(two, 194);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 194, 0, BUCKET_LARGE_CAP);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 195, 0, BUCKET_LARGE_CAP);
    const int three = allocator.bitmap.bitmap_test(allocator.bitmap, 0, BUCKET_LARGE_CAP);
    EXPECT_EQ(three, 195);
    for (unsigned int i = 0; i < BUCKET_LARGE_CAP; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, 0, BUCKET_LARGE_CAP);
}

TEST(PopulateSuite, Large) {
    for (int i = 128; i < 131; i++) {
        s_stack[i].ptr   = allocator.allocate(256);
        s_stack[i].bytes = 256;
    }
    indexes[0] = populate_large_buckets(0, 131);
    indexes[0] += 3;
    EXPECT_EQ(allocator.bucket.large[0].flag, 0x01);
    EXPECT_EQ(allocator.bucket.large[0].arena->flag, 0x01);
    indexes[1] = populate_large_buckets(1, indexes[0]);
    indexes[1]++;
    EXPECT_EQ(allocator.bucket.large[1].flag, 0x01);
    EXPECT_EQ(allocator.bucket.large[1].arena->flag, 0x01);
    indexes[2] = populate_large_buckets(2, indexes[1]);
    indexes[2]++;
    EXPECT_EQ(allocator.bucket.large[2].flag, 0x01);
    EXPECT_EQ(allocator.bucket.large[2].arena->flag, 0x01);
}

TEST(ValidationSuite, Large) {
    int val_1 = 0;
    int val_2 = 1;
    int val_3 = 2;
    int* one   = s_stack[128].ptr;
    int* two   = s_stack[129].ptr;
    int* three = s_stack[130].ptr;
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
    three = allocator.allocate(256);
    EXPECT_EQ(three, s_stack[130].ptr);
    two = allocator.allocate(256);
    EXPECT_EQ(two, s_stack[129].ptr);
    one = allocator.allocate(256);
    EXPECT_EQ(one, s_stack[128].ptr);
}

TEST(CleanSuite, Large) {
    clean_large_buckets(0, indexes[0]);
    EXPECT_EQ(allocator.bucket.large[0].arena->curr, 1);
    if (allocator.bucket.large[0].arena->curr != 1) debug_entry_table_full(&allocator.bucket.large[0].blocks.table, 0);
    clean_large_buckets(indexes[0], indexes[1]);
    if (allocator.bucket.large[1].arena->curr != 1) debug_entry_table_full(&allocator.bucket.large[0].blocks.table, 1);
    EXPECT_EQ(allocator.bucket.large[1].arena->curr, 1);
    clean_large_buckets(indexes[1], indexes[2]);
    if (allocator.bucket.large[2].arena->curr != 1) debug_entry_table_full(&allocator.bucket.large[2].blocks.table, 2);
    EXPECT_EQ(allocator.bucket.large[2].arena->curr, 1);
}

TEST(Coalescing, Large) {
    int** arr[4];
    arr[0] = allocator.allocate(128);
    arr[1] = allocator.allocate(128);
    arr[2] = allocator.allocate(128); /* An extra allocation will keep the entries alive and well. */
    const unsigned int curr = allocator.bucket.large[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(256);
    EXPECT_EQ(curr, allocator.bucket.large[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (unsigned int i = 0; i < 4; i++) allocator.deallocate(arr[i]); 
    memset(arr, 0, sizeof(int) * 4);
    EXPECT_EQ(allocator.bucket.large[0].arena->curr, 1);
}

TEST(Coalescing, Any) {
    int** arr[4];
    unsigned int curr[4];
    arr[0] = allocator.allocate(256);
    arr[1] = allocator.allocate(256);
    arr[2] = allocator.allocate(256); /* An extra allocation will keep the entries alive and well. */
    curr[0] = allocator.bucket.large[0].arena->curr;
    
    allocator.deallocate(arr[0]);
    allocator.deallocate(arr[1]);

    arr[3] = allocator.allocate(512);
    EXPECT_NE(arr[3], NULL);
    //arr[0] = allocator.allocate(1000);
    //EXPECT_EQ(arr[0], NULL);
    //EXPECT_EQ(curr[0], allocator.bucket.large[0].arena->curr); /* If it stays the same, the blocks have been merged */ 
    for (unsigned int i = 0; i < 4; i++) allocator.deallocate(arr[i]); 
    //EXPECT_EQ(allocator.bucket.large[0].arena->curr, 1);
    memset(arr, 0, sizeof(int) * 4);
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
    return RUN_ALL_TESTS();
}