#include "../arena/arena.h"
#include "../tests/tests.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <time.h>

static colosseum_t colosseum = {0};

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
    arena_t_push(&colosseum.arena, 8U); 
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
    arena_t_push(&colosseum.arena, ARENA_T_SIZE + 1);
    //EXPECT_EQ(overflow, nullptr); 
    EXPECT_EQ(colosseum.arena.curr, saved_curr);
    //printf(TEST_PASS "arena_t_push: overflow correctly rejected, arena state unchanged\n");
}

TEST(Arena, Pop) {
    // nullptr arena must return NULL safely
    arena_t_pop(nullptr, 0);
    //printf(TEST_PASS "pop: nullptr arena safely rejected\n");

    // arena_t_push then pop — cursor must roll back
    arena_t_push(&colosseum.arena, 16UL);
    unsigned int pre_pop_prev = colosseum.arena.prev;
    arena_t_pop(&colosseum.arena, 16UL);
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
            else arena_t_push(&colosseum.arena, bytes);
        }
        else {
            if (!enable_multiple_entries && multiple_entries != 0) { arena_t_push(&colosseum.arena, bytes); multiple_entries--; }
            else { enable_multiple_entries = true; }
        }
    }
    // TODO: the move functions are going to be declared/defined in the .c file instead of it being in the .h file.
    EXPECT_EQ(colosseum.arena.curr, 1UL);
    EXPECT_NE(colosseum.arena.next, nullptr);
}

TEST(Arena, Resize) {
    const size_t size = colosseum.arena.size;
    arena_t_resize(&colosseum.arena, size);
    EXPECT_EQ(colosseum.arena.size, size);
    arena_t_resize(&colosseum.arena, size * 2);
    EXPECT_EQ(colosseum.arena.size, size);
}

TEST(Colosseum, Push) {
    colosseum_t_push(&colosseum, 8UL);
    EXPECT_EQ(colosseum.bytes, 0UL);
    EXPECT_EQ(colosseum.res, nullptr);
    colosseum_t_push(&colosseum, 4096UL);
    EXPECT_EQ(colosseum.bytes, 4096UL);
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
    colosseum_t_pop_bytes(&colosseum, 8UL);
    EXPECT_EQ(colosseum.bytes, 4096UL);
    // 1MB == 1000000UL
    colosseum_t_pop_bytes(&colosseum, 4096UL);
    EXPECT_EQ(colosseum.bytes, 0UL);
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
    // TODO: the move functions are going to be declared/defined in the .c file instead of it being in the .h file.
    EXPECT_EQ(colosseum.bytes, 0UL);
    EXPECT_NE(colosseum.next, nullptr);
}

TEST(Colosseum, Resize) {
    const int size = colosseum.pages;
    colosseum_t_resize(&colosseum, size * 2);
    ASSERT_GT(colosseum.pages, size);
}


int main(void) {
    srand(time(NULL));
    return RUN_ALL_TESTS(); 
}