#include "../arena/arena.h"
#include <assert.h> 
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>

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

arena_t* init_arena_t() {
    arena_t* arena;
    arena = aligned_alloc(alignof(arena_t), sizeof(arena_t));
    if (!arena) {
        printf("ERROR 10 IN ARENA.C, FAILED TO ALLOCATE MEMORY FOR ARENA!\n");
        return (void*)0;
    }
    memset(arena, 0, sizeof(arena_t));
    arena->chunk = shared_address(arena->chunk, 4096, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!arena->chunk) {
        printf("ERROR 15 IN ARENA.C, FAILED TO ALLOCATE A CHUNK OF MEMORY!\n");
        free(arena);
        return (void*)0;
    }
    memset(arena->chunk, 0, 4096);
    arena->size = 4096;
    arena->curr = 1;
    return arena;
}


int main(void) {
    
    // ─────────────────────────────────────────────────────────────────────────────
    // Arena Allocator Test Suite
    // ─────────────────────────────────────────────────────────────────────────────
    {
        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  ARENA ALLOCATOR TEST SUITE                    \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

        // ─────────────────────────────────────────────────────────────────────
        // alignment() tests
        // ─────────────────────────────────────────────────────────────────────
        printf(SEPARATOR);
        printf(TEST_INFO "Testing alignment()\n");

        // Non-power-of-two alignment must return 0 (invalid)
        assert(alignment(125, 3)  == 126 && "alignment: non-power-of-two must return 128");
        assert(alignment(123, 5)  == 125 && "alignment: non-power-of-two must return 128");
        assert(alignment(248, 6)  == 252 && "alignment: non-power-of-two must return 256");
        printf(TEST_PASS "alignment: non-power-of-two inputs successfully aligned address\n");

        // Already aligned pointer must be returned as-is
        assert(alignment(256, 4)  == 256 && "alignment: already aligned ptr should return 0 (no adjustment needed)");
        assert(alignment(512, 8)  == 512 && "alignment: already aligned ptr should return 0");
        printf(TEST_PASS "alignment: already-aligned pointers return 0 (no padding needed)\n");

        // Unaligned pointer must be pushed forward to next aligned address
        unsigned long int result = alignment(1, 4);
        assert(result == 4 && "alignment: 4 aligned to 1 should give 4");
        result = alignment(3, 4);
        assert(result == 4 && "alignment: 4 aligned to 3 should give 4");
        result = alignment(5, 8);
        assert(result == 8 && "alignment: 8 aligned to 5 should give 16");
        result = alignment(9, 8);
        assert(result == 16 && "alignment: 8 aligned to 9 should give 16");
        printf(TEST_PASS "alignment: unaligned pointers correctly pushed to next boundary\n");

        // Result must always be a multiple of align
        result = alignment(7, 4);
        assert(result % 8 == 0   && "alignment: result must be divisible by align");
        result = alignment(13, 8);
        assert(result % 16 == 0   && "alignment: result must be divisible by align");
        printf(TEST_PASS "alignment: results are always multiples of the requested alignment\n");

        // ─────────────────────────────────────────────────────────────────────
        // push() tests
        // ─────────────────────────────────────────────────────────────────────
        printf(SEPARATOR);
        printf(TEST_INFO "Testing push()\n");

        arena_t* arena = init_arena_t();
        assert(arena != NULL && "push: arena must initialise successfully");
        assert(arena->chunk != NULL && "push: arena chunk must not be NULL after init");
        assert(arena->curr == 0 && "push: cursor must start at 0");
        assert(arena->size > 0  && "push: arena size must be non-zero");
        printf(TEST_PASS "push: arena initialised correctly\n");

        // Basic push must succeed and return non-null
        arena_t* r = push(arena, 8);
        assert(r != NULL && "push: valid push must not return NULL");
        assert(arena->res != NULL && "push: res must point to allocated region");
        assert(arena->curr > 0 && "push: cursor must advance after push");
        printf(TEST_PASS "push: basic allocation succeeded, cursor advanced\n");

        // Allocated region must be zeroed
        unsigned char* region = (unsigned char*)arena->res;
        int zeroed = 1;
        for (unsigned int b = 0; b < 8; b++) if (region[b] != 0) { zeroed = 0; break; }
        assert(zeroed == 1  && "push: allocated region must be zero-initialised");
        printf(TEST_PASS "push: allocated region is zero-initialised\n");

        // res must sit within chunk bounds
        assert((char*)arena->res >= (char*)arena->chunk && "push: res must not be before chunk start");
        assert((char*)arena->res <  (char*)arena->chunk + arena->size && "push: res must not exceed chunk bounds");
        printf(TEST_PASS "push: res sits within valid chunk bounds\n");

        // Cursor must never exceed arena size
        assert(arena->curr <= arena->size && "push: cursor must not exceed arena size");
        printf(TEST_PASS "push: cursor within arena size after allocation\n");

        // Overflow push must return NULL and leave arena unchanged
        unsigned int saved_curr = arena->curr;
        arena_t* overflow = push(arena, arena->size + 1);
        assert(overflow == NULL && "push: overflow allocation must return NULL");
        assert(arena->curr == saved_curr && "push: cursor must not change on failed push");
        printf(TEST_PASS "push: overflow correctly rejected, arena state unchanged\n");

        // Zero-byte push must be rejected or handled safely
        unsigned int curr_before_zero = arena->curr;
        arena_t* zero_push = push(arena, 0);
        assert(arena->curr     == curr_before_zero && "push: zero-byte push must not advance cursor");
        printf(TEST_PASS "push: zero-byte push does not corrupt cursor\n");

        // ─────────────────────────────────────────────────────────────────────
        // pop() tests
        // ─────────────────────────────────────────────────────────────────────
        printf(SEPARATOR);
        printf(TEST_INFO "Testing pop()\n");

        // NULL arena must return NULL safely
        assert(pop(NULL, 0) == NULL && "pop: NULL arena must return NULL");
        printf(TEST_PASS "pop: NULL arena safely rejected\n");

        // Push then pop — cursor must roll back
        push(arena, 16);
        unsigned int pre_pop_prev = arena->prev;
        arena_t* popped = pop(arena, 16);
        assert(popped != NULL && "pop: valid pop must not return NULL");
        assert(arena->curr == pre_pop_prev && "pop: cursor must roll back to prev after pop");
        assert(arena->res != NULL && "pop: res must point to popped region");
        printf(TEST_PASS "pop: cursor correctly rolled back to prev position\n");

        // res after pop must sit within chunk bounds
        assert((char*)arena->res >= (char*)arena->chunk && "pop: res must not be before chunk start");
        assert((char*)arena->res < (char*)arena->chunk + arena->size && "pop: res must not exceed chunk bounds");
        printf(TEST_PASS "pop: res after pop sits within valid chunk bounds\n");

        // ─────────────────────────────────────────────────────────────────────
        // resize() tests
        // ─────────────────────────────────────────────────────────────────────
        /*printf(SEPARATOR);
        printf(TEST_INFO "Testing resize()\n");

        // Non-power-of-two alignment must return NULL
        void* bad_align = resize(arena, NULL, 0, 16, 3);
        assert(bad_align == NULL && "resize: non-power-of-two alignment must return NULL");
        printf(TEST_PASS "resize: non-power-of-two alignment correctly rejected\n");

        // NULL old_memory must behave as a fresh push
        void* fresh = resize(arena, NULL, 0, 32, 4);
        assert(fresh != NULL     && "resize: NULL old_memory must allocate fresh region");
        assert(arena->res != NULL && "resize: res must be set after fresh allocation");
        printf(TEST_PASS "resize: NULL old_memory triggers fresh allocation\n");

        // Grow in-place: last allocation should resize without moving
        push(arena, 16);
        void* last = arena->res;
        unsigned int prev_snapshot = arena->prev;
        void* grown = resize(arena, last, 16, 32, 4);
        assert(grown != NULL  && "resize: grow in-place must succeed");
        assert(grown == last  && "resize: in-place grow must not move the pointer");
        assert(arena->curr == prev_snapshot + 32 && "resize: cursor must reflect new size after grow");
        printf(TEST_PASS "resize: in-place grow succeeded without moving pointer\n");

        // Shrink in-place: cursor must contract
        void* shrunk = resize(arena, last, 32, 8, 4);
        assert(shrunk != NULL  && "resize: shrink in-place must succeed");
        assert(arena->curr == prev_snapshot + 8 && "resize: cursor must contract after shrink");
        printf(TEST_PASS "resize: in-place shrink correctly contracted cursor\n");

        // Non-last allocation must copy to new region
        push(arena, 16);
        void* not_last = arena->res;
        push(arena, 8); // push again so not_last is no longer the last alloc
        void* relocated = resize(arena, not_last, 16, 32, 4);
        assert(relocated != NULL && "resize: non-last resize must return new region");
        assert(relocated != not_last && "resize: non-last resize must copy to new address");
        printf(TEST_PASS "resize: non-last allocation correctly copied to new region\n");

        // Out-of-bounds old_memory must return NULL
        void* fake = (void*)0xDEADBEEF;
        void* oob  = resize(arena, fake, 8, 16, 4);
        assert(oob == NULL && "resize: out-of-bounds old_memory must return NULL");
        printf(TEST_PASS "resize: out-of-bounds pointer correctly rejected\n");

        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);*/
        free(r);
    }
    return 0;
}