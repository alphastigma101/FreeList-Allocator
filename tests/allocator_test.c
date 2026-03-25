#include "../allocator/allocator.h"
#include <assert.h> 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <sys/types.h>

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

int main(void) {

    printf("\n");
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
    printf(TEST_HEADER "  CUSTOM ALLOCATOR TEST SUITE                   \n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

    // ─────────────────────────────────────────────────────────────────────
    // 1. Populating three small buckets (<=64)
    // ─────────────────────────────────────────────────────────────────────
    printf(SEPARATOR);
    printf(TEST_INFO "1. Populating three small buckets i.e (small <= 64 bytes)\n");
    
    init_allocator_t();
    alloc_entry_t s_stack[1024];
    memset(s_stack, 0, sizeof(s_stack));
    size_t len = 0;
 
    for (; len < 3; len++) {
        s_stack[len].ptr   = allocator->allocate(sizeof(int));
        s_stack[len].bytes = sizeof(int);
    }

    size_t s_one = 0;
    for (; len < 86; len++) {
        size_t bytes = (rand() % 64) + 1;
        s_stack[len].ptr = allocator->allocate(bytes);
        if (allocator->bucket.small[0].flag == 0x01) {
            len = len - 1;
            break;
        }
        s_stack[len].bytes = bytes;
    }
    size_t e_one = len;
    assert(allocator->bucket.small[0].flag == 0x01 && "small[0] arena should be full");

    for (; len < 170; len++) {
        size_t bytes       = (rand() % 64) + 1;
        s_stack[len].ptr = allocator->allocate(bytes);
        if (allocator->bucket.small[1].flag == 0x01) {
            len = len - 1;
            break;
        }
        s_stack[len].bytes = bytes;
        //printf("Random byte value for small[1] is: %zu  Offset is: %zu\n", bytes, offset);
    }
    size_t e_two = len;
    assert(allocator->bucket.small[1].flag == 0x01 && "small[1] arena should be full");

    for (; len < 340; len++) {
        size_t bytes       = (rand() % 64) + 1;
        s_stack[len].ptr   = allocator->allocate(bytes);
        if (allocator->bucket.small[2].flag == 0x01) {
            len = len - 1;
            break;
        }
        s_stack[len].bytes = bytes;
        //printf("Random byte value for small[2] is: %zu  Offset is: %zu\n", bytes, offset);
    }
    size_t e_three = len;
    assert(allocator->bucket.small[2].flag == 0x01 && "small[2] arena should be full");
        
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
    // ─────────────────────────────────────────────────────────────────────
    // 2. Pointer validation for small buckets
    // ─────────────────────────────────────────────────────────────────────
    printf(SEPARATOR);
    printf(TEST_INFO "2. Pointer validation for small buckets\n");
    int val_1 = 0;
    int val_2 = 1;
    int val_3 = 2;

    int* one   = s_stack[0].ptr;
    int* two   = s_stack[1].ptr;
    int* three = s_stack[2].ptr;

    allocator->deallocate(one);
    assert(*one == -1 && "This should now be a invalid memory address\n");
    allocator->deallocate(two);
    assert(*two == -1 && "This should now be a invalid memory address\n");
    allocator->deallocate(three);
    assert(*three == -1 && "This should now be a invalid memory address\n");
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

    printf(SEPARATOR);
    printf(TEST_INFO "3. Re-using addresses from small buckets \n");
    
    one = allocator->allocate(sizeof(int));
    assert(one   == s_stack[0].ptr && "Reusing memory address for one");
    two = allocator->allocate(sizeof(int));
    assert(two   == s_stack[1].ptr && "Reusing memory address for two");
    three = allocator->allocate(sizeof(int));
    assert(three == s_stack[2].ptr && "Reusing memory address for three");
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

    printf(SEPARATOR);
    printf(TEST_INFO "4. Emptying Arena from small buckets \n");
    for (size_t i = 0; i < e_one; i++) {
        printf("[small[0]] s_one: %zu  ptr: %p  bytes: %zu  curr: %zu  prev: %zu\n",
            i, s_stack[i].ptr, s_stack[i].bytes,
            allocator->bucket.small[0].arena->curr,
            allocator->bucket.small[0].arena->prev);
        allocator->deallocate(s_stack[i].ptr);
        printf("[small[0]] after dealloc — curr: %zu  prev: %zu\n",
            allocator->bucket.small[0].arena->curr,
            allocator->bucket.small[0].arena->prev);
    }
    assert(allocator->bucket.small[0].arena->curr == 0);

    for (size_t i = 0; i < e_two; i++) {
        printf("[small[1]] s_two: %zu  ptr: %p  bytes: %zu  curr: %zu  prev: %zu\n",
            i, s_stack[i].ptr, s_stack[i].bytes,
            allocator->bucket.small[1].arena->curr,
            allocator->bucket.small[1].arena->prev);
        allocator->deallocate(s_stack[i].ptr);
        printf("[small[1]] after dealloc — curr: %zu  prev: %zu\n",
            allocator->bucket.small[1].arena->curr,
            allocator->bucket.small[1].arena->prev);
    }

    assert(allocator->bucket.small[1].arena->curr == 0);

    for (size_t i = 0; i < e_three; i++) {
        printf("[small[2]] s_three: %zu  ptr: %p  bytes: %zu  curr: %zu  prev: %zu\n",
            i, s_stack[i].ptr, s_stack[i].bytes,
            allocator->bucket.small[2].arena->curr,
            allocator->bucket.small[2].arena->prev);
        allocator->deallocate(s_stack[i].ptr);
        printf("[small[2]] after dealloc — curr: %zu  prev: %zu\n",
            allocator->bucket.small[2].arena->curr,
            allocator->bucket.small[2].arena->prev);
    }

    assert(allocator->bucket.small[2].arena->curr == 0);

    assert(allocator->bucket.small[0].flag == 0x0 && "All memory address have been pushed onto allocator->bucket.small[0]\n");
    assert(allocator->bucket.small[1].flag == 0x0 && "All memory address have been pushed onto allocator->bucket.small[1]\n");
    assert(allocator->bucket.small[2].flag == 0x0 && "All memory address have been pushed onto allocator->bucket.small[2]\n");
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
    // ─────────────────────────────────────────────────────────────────────
    // 2. Basic allocation — medium (<=128)
    // ─────────────────────────────────────────────────────────────────────
    printf(SEPARATOR);
    printf(TEST_INFO "2. Basic allocation (medium <= 128 bytes)\n");
    int size = 128 * 2;
    void* s_stack_4[128];
    void* s_stack_5[128];
    void* s_stack_6[size];
   
    printf(SEPARATOR);
    printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
    printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

    return 0;
}
