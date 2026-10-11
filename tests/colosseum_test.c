#include "../colosseum/colosseum.h"
#include "../tests/tests.h"
#include "threads.h"
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

static colosseum_t colosseum = {0};

typedef struct node_stack_t {
    colosseum_t** array_of_coloseums_t;
    void*** coloseum_t_addresses;
    arena_t** array_of_arena_t;
    void*** arena_t_addresses;
    void** addresses; // This gets stored into arena_t_address or colosseum_t_address
    size_t coloseum_t_index;
    size_t arena_t_index; 
} node_stack_t;

node_stack_t stack = {0};

FORCE_INLINE void init_node_stack_t() {
    stack.addresses = calloc(ARENA_T_SIZE, 1);
    if (!stack.addresses) return;
    
    stack.array_of_coloseums_t = calloc(5, sizeof(colosseum_t));
    if (!stack.array_of_coloseums_t) return;

    stack.coloseum_t_addresses = private_address(nullptr, 2000000UL * sizeof(void**), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
    if (stack.coloseum_t_addresses == MAP_FAILED) return;

    stack.array_of_arena_t = calloc(5, sizeof(arena_t));
    if (!stack.array_of_arena_t) return;
    
    stack.arena_t_addresses = private_address(nullptr, ARENA_T_SIZE * sizeof(void**), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0);
    if (stack.arena_t_addresses == MAP_FAILED) return;
}

FORCE_INLINE void init_addresses(const size_t size) {
    // NOTE: We store the previous address of stack.addresses at either coloseum_t_addresses[x] or arena_t_addresses[x] before we create a new memory address
    if (size <= ARENA_T_SIZE) stack.addresses = calloc(size,  1);
    else stack.addresses = private_address(nullptr, size * sizeof(void**), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0); 
}

[[gnu::destructor]]
FORCE_INLINE void dctor_node_stack_t() {
    for (size_t i = 0; i < stack.coloseum_t_index; i++) {

    }
    for (size_t i = 0; i < stack.arena_t_index; i++) {

    }
}

TEST(Arena, Alignment) {
    EXPECT_EQ(alignment(125, 3), 126);
    EXPECT_EQ(alignment(123, 5) , 125);
    EXPECT_EQ(alignment(248, 6), 252);
    //printf(TEST_PASS "alignment: non-power-of-two inputs successfully aligned address\n");

    // Already aligned pointer must be returned as-is
    EXPECT_EQ(alignment(256, 4), 256);
    EXPECT_EQ(alignment(512, 8), 512);
    //printf(TEST_PASS "alignment: already-aligned pointers return 0 (no padding needed)\n");

    // Unaligned pointer must be arena_t_pushed forward to next aligned address
    unsigned long int result = alignment(1, 4);
    EXPECT_EQ(result, 4);
    result = alignment(3, 4);
    EXPECT_EQ(result, 4);
    result = alignment(5, 8);
    EXPECT_EQ(result, 8);
    result = alignment(9, 8);
    EXPECT_EQ(result, 16);
    //printf(TEST_PASS "alignment: unaligned pointers correctly arena_t_pushed to next boundary\n");

    // Result must always be a multiple of align
    result = alignment(7, 4);
    EXPECT_EQ(result % 8, 0);
    result = alignment(13, 8);
    EXPECT_EQ(result % 16, 0);
}

TEST(Arena, arena_t_push) {
    init_arena_t(&colosseum.arena);
    EXPECT_NE(colosseum.arena.next, nullptr);
    EXPECT_NE(colosseum.arena.chunk, nullptr);
    //printf(TEST_PASS "arena_t_push: arena initialised correctly\n");

    // Basic arena_t_push must succeed and return non-null
    colosseum_t_push(&colosseum, 8UL); 
    EXPECT_NE(colosseum.arena.res, nullptr);
    EXPECT_GE(colosseum.arena.curr, 0); 
    //printf(TEST_PASS "arena_t_push: basic allocation succeeded, cursor advanced\n");

    // Allocated region must be zeroed
    unsigned char* region = (unsigned char*)colosseum.arena.res;
    int zeroed = 1;
    for (unsigned int b = 0; b < 8; b++) if (region[b] != 0) { zeroed = 0; break; }
    EXPECT_EQ(zeroed,1);
    //printf(TEST_PASS "arena_t_push: allocated region is zero-initialised\n");

    // res must sit within chunk bounds
    EXPECT_GE((char*)colosseum.arena.res, (char*)colosseum.arena.chunk); 
    EXPECT_LE((char*)colosseum.arena.res, (char*)colosseum.arena.chunk + ARENA_T_SIZE);
    //printf(TEST_PASS "arena_t_push: res sits within valid chunk bounds\n");

    // Cursor must never exceed arena size
    EXPECT_LT(colosseum.arena.curr, ARENA_T_SIZE);
    //printf(TEST_PASS "arena_t_push: cursor within arena size after allocation\n");

    // Overflow arena_t_push must return nullptr and leave arena unchanged
    unsigned int saved_curr = colosseum.arena.curr;
    colosseum_t_push(&colosseum, ARENA_T_SIZE + 1);
    //EXPECT_EQ(overflow, nullptr); 
    EXPECT_EQ(colosseum.arena.curr, saved_curr);
    //printf(TEST_PASS "arena_t_push: overflow correctly rejected, arena state unchanged\n");
}

TEST(Arena, Pop) {
    // nullptr arena must return NULL safely
    colosseum_t_pop(nullptr, 0UL);
    //printf(TEST_PASS "pop: nullptr arena safely rejected\n");

    // arena_t_push then pop — cursor must roll back
    colosseum_t_push(&colosseum, 16UL);
    unsigned int pre_pop_prev = colosseum.arena.prev;
    colosseum_t_pop(&colosseum, 16UL);
    EXPECT_EQ(colosseum.arena.curr, pre_pop_prev);
    EXPECT_EQ(colosseum.arena.res, nullptr);
    //printf(TEST_PASS "pop: cursor correctly rolled back to prev position\n");

    // res after pop must sit within chunk bounds
    EXPECT_GE((char*)colosseum.arena.res, (char*)colosseum.arena.chunk);
    EXPECT_LT((char*)colosseum.arena.res, (char*)colosseum.arena.chunk + ARENA_T_SIZE);
    //printf(TEST_PASS "pop: res after pop sits within valid chunk bounds\n");
}

TEST(Arena, Full) {
    int multiple_entries = 0;
    bool enable_multiple_entries = true;
    for (size_t i = 1UL; colosseum.arena.state == ARENA_T_IS_FULL || i < 1000000UL; i *= 2UL) {
        size_t bytes = (size_t)(rand() % i) + 1;
        if (multiple_entries < 5 && enable_multiple_entries) {
            multiple_entries++;
            if (multiple_entries == 5) enable_multiple_entries = false;
            else colosseum_t_push(&colosseum, bytes);
        }
        else {
            if (!enable_multiple_entries && multiple_entries != 0) { colosseum_t_push(&colosseum, bytes); multiple_entries--; }
            else { enable_multiple_entries = true; }
        }
    }
    EXPECT_EQ(colosseum.arena.curr, 1UL);
    EXPECT_NE(colosseum.arena.next, nullptr);
}

TEST(Arena, Resize) {
    const size_t size = colosseum.arena.size;
    colosseum_t_resize(&colosseum, size);
    EXPECT_EQ(colosseum.arena.size, size);
    colosseum_t_resize(&colosseum, size * 2);
    EXPECT_EQ(colosseum.arena.size, size);
}

TEST(Colosseum, Push) {
    colosseum_t_push(&colosseum, 8UL);
    EXPECT_EQ(colosseum.total_bytes, 0UL);
    EXPECT_EQ(colosseum.res, nullptr);
    colosseum_t_push(&colosseum, 4096UL);
    EXPECT_EQ(colosseum.total_bytes, 4096UL);
    // Rule of thumb: Modern CPU pages which are considered as a block since they map the va to the pa.
    // so consider the pa as the begining of the memory region and each time you increment the va is mapped to it.
    // The size of these pages are 4096.
    // Each block can fit into the page that doesn't equal over 4096. 
    // It is easier to increase the size of the page instead of allocating more entries since the mmu (memory management unit) should handle so many at a time.
    // So basically if the caches are completely filled, then the mmu takes over.
    char* buffer = colosseum.res;
    for (size_t i = 0; i < 4096UL; i++) buffer[i] = ' ';
}

TEST(Colosseum, Pop) {
    colosseum_t_pop(&colosseum, 8UL);
    EXPECT_EQ(colosseum.total_bytes, 4096UL);
    // 1MB == 1000000UL
    colosseum_t_pop(&colosseum, 4096UL);
    EXPECT_EQ(colosseum.total_bytes, 0UL);
}

TEST(Colosseum, Full) {
    int multiple_entries = 0;
    bool enable_multiple_entries = true;
    for (size_t i = 4096UL; colosseum.state == COLOSSEUM_T_IS_FULL; i += 8UL) {
        size_t bytes = (size_t)(rand() % i) + 1;
        if (multiple_entries < 5 && enable_multiple_entries) {
            multiple_entries++;
            if (multiple_entries == 5) enable_multiple_entries = false;
            else colosseum_t_push(&colosseum, bytes);
        }
        else {
            if (!enable_multiple_entries && multiple_entries != 0) { colosseum_t_push(&colosseum, bytes); multiple_entries--; }
            else { enable_multiple_entries = true; }
        }
    }
    EXPECT_EQ(colosseum.total_bytes, 0UL);
    EXPECT_NE(colosseum.next, nullptr);
}

TEST(Colosseum, Resize) {
    const int size = colosseum.pages;
    colosseum_t_resize(&colosseum, size * 2);
    ASSERT_GT(colosseum.pages, size);
}

TEST(Arena, Addresses) {
    for (int i = 0; i < 5; i++) {
        for (size_t i = 0; i < ARENA_T_SIZE; i += 8UL) {
            // TOOD: We need to fill up the arena->res and store each of the addresses in stack.addresses.
            // Example: stack.addresses[0 ... n - 1] = colosseum.arena.res;
            // If we are full, then we do this: stack.arena_t_addresses[stack.arena_t_index] = stack.addresses;
            // stack.array_of_arena_t[stack.arena_t_index] = &colosseum.arena;
            // stack.arena_t_index++;
            // init_addresses(ARENA_T_SIZE);
            // And we continue until each node
        }
    }
    
    
    // And then we call in the iterator functions to test the instant lookups for the arena nodes.
}

TEST(Colosseum, Addresses) {}


int main(void) {
    srand(time(NULL));
    init_node_stack_t();
    return RUN_ALL_TESTS(); 
}