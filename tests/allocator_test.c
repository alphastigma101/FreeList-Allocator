#include "../allocator/allocator.h"
#include "../hash_table/hash_table.h"
#include <assert.h> 
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_MAGENTA "\033[35m"

#define TEST_PASS    ANSI_BOLD ANSI_GREEN  "  [✔] " ANSI_RESET
#define TEST_FAIL    ANSI_BOLD ANSI_RED    "  [✘] " ANSI_RESET
#define TEST_INFO    ANSI_BOLD ANSI_CYAN   "  [~] " ANSI_RESET
#define TEST_HEADER  ANSI_BOLD ANSI_MAGENTA
#define SEPARATOR    ANSI_CYAN "  ────────────────────────────────────────────\n" ANSI_RESET

typedef struct {
    void*  ptr;
    size_t bytes;
} alloc_entry_t;
alloc_entry_t s_stack[1024];

typedef struct bucket_t {
    unsigned char       flag;
    unsigned char       _pad[7];
    arena_t*            arena;       
    memory_address_hash_table_t* maht;
} bucket_t;

static inline void test_bitmap_small_range() {
    printf(SEPARATOR);
    printf(TEST_INFO "1. Testing small range with bitmap\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 0, SMALL_BIT_START, SMALL_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 1, SMALL_BIT_START, SMALL_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 2, SMALL_BIT_START, SMALL_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 3, SMALL_BIT_START, SMALL_BIT_END);
    const int index = allocator.bitmap.bitmap_test(allocator.bitmap, SMALL_BIT_START, SMALL_BIT_END);
    assert(index == 4 && "bitmap_test: Function should have returned 4\n");
    printf(TEST_PASS "bitmap_test: Function returned 4\n");
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 0, SMALL_BIT_START, SMALL_BIT_END);
    const int zero = allocator.bitmap.bitmap_test(allocator.bitmap, SMALL_BIT_START, SMALL_BIT_END);
    assert(zero == 0 && "bitmap_test: Function should have returned 0\n");
    printf(TEST_PASS "bitmap_test: Function returned 0\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 0, SMALL_BIT_START, SMALL_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 2, SMALL_BIT_START, SMALL_BIT_END);
    const int two = allocator.bitmap.bitmap_test(allocator.bitmap, SMALL_BIT_START, SMALL_BIT_END);
    assert(two == 2 && "bitmap_test: Function should have returned 2\n");
    printf(TEST_PASS "bitmap_test: Function returned 2\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 2, SMALL_BIT_START, SMALL_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 3, SMALL_BIT_START, SMALL_BIT_END);
    const int three = allocator.bitmap.bitmap_test(allocator.bitmap, SMALL_BIT_START, SMALL_BIT_END);
    assert(three == 3 && "bitmap_test: Function should have returned 3\n");
    printf(TEST_PASS "bitmap_test: Function returned 3\n");
    for (int i = 0; i < 5; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, SMALL_BIT_START, SMALL_BIT_END);
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
}

static inline void test_bitmap_medium_range() {
    printf(SEPARATOR);
    printf(TEST_INFO "2. Testing medium range with bitmap\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 64, MEDIUM_BIT_START, MEDIUM_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 65, MEDIUM_BIT_START, MEDIUM_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 66, MEDIUM_BIT_START, MEDIUM_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 67, MEDIUM_BIT_START, MEDIUM_BIT_END);
    const int index = allocator.bitmap.bitmap_test(allocator.bitmap, MEDIUM_BIT_START, MEDIUM_BIT_END);
    assert(index == 68 && "bitmap_test: Function should have returned 68\n");
    printf(TEST_PASS "bitmap_test: Function returned 68\n");
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 64, MEDIUM_BIT_START, MEDIUM_BIT_END);
    const int zero = allocator.bitmap.bitmap_test(allocator.bitmap, MEDIUM_BIT_START, MEDIUM_BIT_END);
    assert(zero == 64 && "bitmap_test: Function should have returned 64\n");
    printf(TEST_PASS "bitmap_test: Function returned 64\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 64, MEDIUM_BIT_START, MEDIUM_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 66, MEDIUM_BIT_START, MEDIUM_BIT_END);
    const int two = allocator.bitmap.bitmap_test(allocator.bitmap, MEDIUM_BIT_START, MEDIUM_BIT_END);
    assert(two == 66 && "bitmap_test: Function should have returned 66\n");
    printf(TEST_PASS "bitmap_test: Function returned 66\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 66, MEDIUM_BIT_START, MEDIUM_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 67, MEDIUM_BIT_START, MEDIUM_BIT_END);
    const int three = allocator.bitmap.bitmap_test(allocator.bitmap, MEDIUM_BIT_START, MEDIUM_BIT_END);
    assert(three == 67 && "bitmap_test: Function should have returned 67\n");
    printf(TEST_PASS "bitmap_test: Function returned 67\n");
    for (int i = MEDIUM_BIT_START; i < MEDIUM_BIT_END; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, MEDIUM_BIT_START, MEDIUM_BIT_END);
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
}

static inline void test_bitmap_large_range() {
    printf(SEPARATOR);
    printf(TEST_INFO "3. Testing large range with bitmap\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 192, LARGE_BIT_START, LARGE_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 193, LARGE_BIT_START, LARGE_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 194, LARGE_BIT_START, LARGE_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 195, LARGE_BIT_START, LARGE_BIT_END);
    const int index = allocator.bitmap.bitmap_test(allocator.bitmap, LARGE_BIT_START, LARGE_BIT_END);
    assert(index == 196 && "bitmap_test: Function should have returned 196\n");
    printf(TEST_PASS "bitmap_test: Function returned 196\n");
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 192, LARGE_BIT_START, LARGE_BIT_END);
    const int zero = allocator.bitmap.bitmap_test(allocator.bitmap, LARGE_BIT_START, LARGE_BIT_END);
    assert(zero == 192 && "bitmap_test: Function should have returned 192\n");
    printf(TEST_PASS "bitmap_test: Function returned 192\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 192, LARGE_BIT_START, LARGE_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 194, LARGE_BIT_START, LARGE_BIT_END);
    const int two = allocator.bitmap.bitmap_test(allocator.bitmap, LARGE_BIT_START, LARGE_BIT_END);
    assert(two == 194 && "bitmap_test: Function should have returned 2\n");
    printf(TEST_PASS "bitmap_test: Function returned 194\n");
    allocator.bitmap = allocator.bitmap.bitmap_set(allocator.bitmap, 194, LARGE_BIT_START, LARGE_BIT_END);
    allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, 195, LARGE_BIT_START, LARGE_BIT_END);
    const int three = allocator.bitmap.bitmap_test(allocator.bitmap, LARGE_BIT_START, LARGE_BIT_END);
    assert(three == 195 && "bitmap_test: Function should have returned 3\n");
    printf(TEST_PASS "bitmap_test: Function returned 195\n");
    for (int i = LARGE_BIT_START; i < LARGE_BIT_END; i++) allocator.bitmap = allocator.bitmap.bitmap_clear(allocator.bitmap, i, LARGE_BIT_START, LARGE_BIT_END);
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
}

static int populate_small_buckets(const int idx, const int start_idx) {
    int new_idx = start_idx;
    for (;;) {
        size_t bytes = (rand() % 62) + 1; // TODO: Change this out. 
        s_stack[new_idx].ptr = allocator.allocate(bytes);
        if (allocator.bucket.small[idx].flag == 0x01) break;
        s_stack[new_idx].bytes = bytes;
        new_idx++;
    }
    return new_idx;
}

static void clean_small_buckets(const int idx, const int start_idx, const int idx_end) {
    for (int i = start_idx; i < idx_end; i++) {
        allocator.deallocate(s_stack[i].ptr);
    }
    assert(allocator.bucket.small[idx].arena->curr == 1 && "allocator.deallocate: Arena has not been successfully rewinded\n");
}



int main(void) {
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
    printf("\n");
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
    printf(TEST_HEADER "  CUSTOM ALLOCATOR TEST SUITE                   \n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
    init_allocator_t();
    test_bitmap_small_range();
    test_bitmap_medium_range();
    test_bitmap_large_range();
    
    printf(SEPARATOR);
    printf(TEST_INFO "4. Populating three small buckets i.e (small <= 64 bytes)\n");
    memset(s_stack, 0, sizeof(s_stack));
    for (int i = 0; i < 3; i++) {
        s_stack[i].ptr   = allocator.allocate(sizeof(int));
        s_stack[i].bytes = sizeof(int);
    }
    const int b0 = populate_small_buckets(0, 3);
    const int b1 = populate_small_buckets(1, b0);
    const int b2 = populate_small_buckets(2, b1);
    {
        assert((allocator.bucket.small[0].flag & allocator.bucket.small[0].arena->flag) == 0x01 && "allocator.allocate: Expected 0x01 to be set for arena and bucket at index 0\n");
        printf(TEST_PASS "allocator.allocate: both flag fields are 0x01 at index 0\n");
        assert((allocator.bucket.small[1].flag & allocator.bucket.small[1].arena->flag) == 0x01&& "allocator.allocate: Expected bucket 0x01 to be set for arena and bucket at index 1\n");
        printf(TEST_PASS "allocator.allocate: both flag fields are 0x01 at index 1\n");
        assert((allocator.bucket.small[2].flag & allocator.bucket.small[2].arena->flag) == 0x01 && "allocator.allocate: Expected bucket 0x01 to be set for arena and bucket at index 2\n");
        printf(TEST_PASS "allocator.allocate: both flag fields are 0x01 at index 2\n");
        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
    }

    {
        printf(SEPARATOR);
        printf(TEST_INFO "5. Pointer validation for small buckets\n");
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
        assert(*one == -1 && "allocator.deallocate: Not a invalid memory address\n");
        printf(TEST_PASS "allocator.deallocate: *one == -1\n");
        allocator.deallocate(two);
        assert(*two == -1 && "allocator.deallocate: Not a invalid memory address\n");
        printf(TEST_PASS "allocator.deallocate: *two == -1\n");
        allocator.deallocate(three);
        assert(*three == -1 && "allocator.deallocate: Not a invalid memory address\n");
        printf(TEST_PASS "allocator.deallocate: *three == -1\n");
        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

        printf(SEPARATOR);
        printf(TEST_INFO "6. Re-using addresses from small buckets \n");
        
        one = allocator.allocate(sizeof(int));
        assert(one == s_stack[0].ptr && "allocator.allocate: Must return the same exact memory address\n");
        printf(TEST_PASS "allocator.allocate: one == s_stack[0]\n");
        two = allocator.allocate(sizeof(int));
        assert(two == s_stack[1].ptr && "allocator.allocate: Must return the same exact memory address\n");
        printf(TEST_PASS "allocator.allocate: two == s_stack[1]\n");
        three = allocator.allocate(sizeof(int));
        assert(three == s_stack[2].ptr && "allocator.allocate: Must return the same exact memory address\n");
        printf(TEST_PASS "allocator.allocate: three == s_stack[2]\n");
        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
    }

    /*printf(SEPARATOR);
    printf(TEST_INFO "7. Emptying Arena from small buckets \n");
    clean_small_buckets(0, 0, b0);
    clean_small_buckets(1, b0, b1);
    clean_small_buckets(2, b1, b2);
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);*/
    // ─────────────────────────────────────────────────────────────────────
    // 2. Basic allocation — medium (<=128)
    // ─────────────────────────────────────────────────────────────────────
    /*printf(SEPARATOR);
    printf(TEST_INFO "2. Basic allocation (medium <= 128 bytes)\n");
    int size = 128 * 2;
    void* s_stack_4[128];
    void* s_stack_5[128];
    void* s_stack_6[size];
   
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);*/

    return 0;
}
