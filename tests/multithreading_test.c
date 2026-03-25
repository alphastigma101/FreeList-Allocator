#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h> // Development
#include <stdalign.h> // Development
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "../threads/threads.h" // Production
#include "../DataStructures/C/structures.h" // Development


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


threads_t* init_threads_t() {
    threads_t* t = aligned_alloc(alignof(threads_t), sizeof(threads_t));
    if (!t) {
        printf("Failed to allocate memory for thread struct! returning null!");
        return NULL;
    }
    memset(t, 0, sizeof(threads_t));
    t->flag = 0x0;
    return t;
    
}

// Numerical Value Test
static void* addition(int* i) {
    for (int j = 0; *i < 1000; j+=3) *i = j;
    return NULL;
}

// String Traversal Test mode can be either forward or backwards
static void* traversal(const char* _str, uint8_t* mode) {
    const uint8_t d_mode = *mode;
    if (d_mode == 0x01) {
        while (*_str != '\0') {
            const char* copy_str = _str;
            _str++;
            assert((strlen(copy_str) - strlen(_str)) == 1);
        }
    }
    else if (d_mode == 0x02) {
        while (*_str != '\0') {
            const char* copy_str = _str;
            _str++;
            assert((strlen(copy_str) - strlen(_str)) == 1);
        }
    }
    return (void*)0;
}

void* thread_arguments(args_t *args) {
    switch (args->size) {
        case 1:
            addition(args->arr[0]);
            break;
        case 2:
            traversal(args->arr[0], args->arr[1]);

        default:
            break;
    }    

    return (void*)0;
}


// TODO: ZSTD Also has its own threading library, so that needs to be integrated into busybox's configuration

int main(void) {
    threads_t* threads[8];
    for (int i = 0; i < 8; i++)
        threads[i] = init_threads_t();
    

    {
        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  NUMERICAL VALUE TEST  ·  Thread Pool           \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

        int* i = malloc(sizeof(int));
        if (!i) {
            printf(TEST_FAIL "ERROR 74: Failed to allocate memory for i\n");
            return 1;
        }
        printf(TEST_INFO "Allocated shared int @ %p\n", (void*)i);
        *i = 0;

        threads[0]->addr = shared_address(i, sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        i = threads[0]->addr;
        threads[0]->args = aligned_alloc(alignof(args_t), sizeof(args_t));
        threads[0]->args->size = 1;
        threads[0]->args->arr = aligned_alloc(alignof(void), sizeof(void*));
        threads[0]->args->arr[0] = i;
        threads[0] = create_thread(threads[0], 0x01, thread_arguments);
        printf(TEST_INFO "Thread spawned — shared address mapped @ %p\n", threads[0]->addr);

        printf(SEPARATOR);
        printf(TEST_INFO "Running " ANSI_YELLOW "1000" ANSI_RESET " iteration monotonicity check...\n");

        int main_value     = 0;
        int last_thread_value = -1;
        // We are gonna need to either adjust the thread pool or have it contain a hash map to store the virtual memory addresses that are not 
        // in use. I am planning on creating an arena and an allocator 

        for (int j = 0; j < 1000; j++) {
            main_value = j + 1;
            int current_thread_value = *i;

            assert(main_value == j + 1 &&
                   "Main thread: should increment by 1");

            assert(current_thread_value >= last_thread_value &&
                   "Thread value should be monotonically increasing or stable");

            last_thread_value = current_thread_value;
        }

        printf(TEST_PASS "Monotonicity check passed across all 1000 iterations\n");
        printf(SEPARATOR);

        pthread_mutex_unlock(threads[0]->mutex);
        join_thread(*(threads[0]->id), (void*)0);
        printf(TEST_INFO "Thread joined successfully\n");

        assert(main_value == 1000 && "Main thread value should be 1000");
        printf(TEST_PASS "Main thread final value : " ANSI_YELLOW "%d" ANSI_RESET " / expected " ANSI_YELLOW "1000\n" ANSI_RESET, main_value);

        assert(*i == 1002 && "Thread final value should be 1002");
        printf(TEST_PASS "Thread final value      : " ANSI_YELLOW "%d" ANSI_RESET " / expected " ANSI_YELLOW "1002\n" ANSI_RESET, *i);

        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
    }
 
    {
        // TODO: Right now as of 3/4/26, I have achieved concurrency threading i.e it can utilize one core 
        // We should also off support for parrellism, but that would require the cpu to have more than one core 
        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  STRING TRAVERSAL TEST  ·  Thread Pool          \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

        char* local_one = "This is a short string";
        char* local_two = "This is a very very very very long string"; 

        printf(TEST_INFO "Short string : " ANSI_YELLOW "\"%s\"\n" ANSI_RESET, local_one);
        printf(TEST_INFO "Long string  : " ANSI_YELLOW "\"%s\"\n" ANSI_RESET, local_two);
        printf(SEPARATOR);

        uint8_t* mode_1 = malloc(sizeof(uint8_t));
        memset(mode_1, 0, sizeof(uint8_t)); 
        mode_1 = shared_address(mode_1, sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        *mode_1 = 0x01;

        threads[1]->addr = shared_address(local_one, sizeof(char), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        char* shared_one = threads[1]->addr;
        threads[1]->args = aligned_alloc(alignof(args_t), sizeof(args_t));
        threads[1]->args->size = 2;
        threads[1]->args->arr = aligned_alloc(alignof(void), sizeof(void*) * 2);
        threads[1]->args->arr[0] = shared_one;
        threads[1]->args->arr[1] = mode_1;
        
        shared_one = strcpy(shared_one, local_one);
        if (*shared_one != ' ') {
            threads[1] = create_thread(threads[1], 0x01, thread_arguments);
            printf(TEST_INFO "Thread[1] spawned — short string mapped @ %p\n", threads[1]->addr);
        }

        uint8_t* mode_2 = malloc(sizeof(uint8_t));
        memset(mode_2, 0, sizeof(uint8_t));
        mode_2 = shared_address(mode_2, sizeof(uint8_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        *mode_2 = 0x02;

        threads[2]->addr = shared_address(local_two, sizeof(char), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        char* shared_two = threads[2]->addr; // ( Get the mapped region of the memory block. This allows us to increment here and in the other thread.)
        shared_two = strcpy(shared_two, local_two);
        threads[2]->args = aligned_alloc(alignof(args_t), sizeof(args_t));
        threads[2]->args->size = 2;
        threads[2]->args->arr = aligned_alloc(alignof(void), sizeof(void*) * 2);
        threads[2]->args->arr[0] = shared_two;
        threads[2]->args->arr[1] = mode_2;
        
        if (*shared_two != ' ') {
            threads[2] = create_thread(threads[2], 0x01, thread_arguments);
            printf(TEST_INFO "Thread[2] spawned — long string mapped  @ %p\n", threads[2]->addr);
        }
        
        // TOOD: Store one and two's memory addresses in a map so they can be used again
        // Useful the allocator we will eventually make  
        printf(SEPARATOR);
        printf(TEST_INFO "Entering traversal loop — monitoring both threads...\n");

        uint8_t esc = 0;
        for (;;) {
            if (esc == 1) break;
            else {
                if (*shared_one != '\0') {
                    assert(*shared_one >= 0x20 && *shared_one <= 0x7E);
                    assert((void*)shared_one >= threads[1]->addr);

                    const char* cpy_so = shared_one; 
                    shared_one+=2; // This is thread zero (this is the main thread)
                    assert((strlen(cpy_so) - strlen(shared_one) == 2));

                }
                if (*shared_two != '\0') {
                    assert(*shared_two >= 0x20 && *shared_two <= 0x7E);
                    assert((void*)shared_two >= threads[2]->addr);
                    const char* cpy_so = shared_two;
                    shared_two+=2; 
                    assert((strlen(cpy_so) - strlen(shared_two) == 2));
                }
                else if (*shared_one == '\0' && *shared_two == '\0') esc = 1;
            }
        }
        pthread_mutex_unlock(threads[1]->mutex);
        pthread_mutex_unlock(threads[2]->mutex);
        printf(TEST_PASS "Thread[1] — all chars valid printable ASCII, pointer stayed in bounds\n");
        printf(TEST_PASS "Thread[2] — all chars valid printable ASCII, pointer stayed in bounds\n");
        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

        for (int i = 1; i < 3; i++) {
            join_thread(*(threads[i]->id), (void*)0);
        }
    } 

    // User defined data structures / Objects 
    // Create a queue, linked lists, binary search tree, and a couple other data structures 
    // that will really test to see if multi-threading is working or not 
    { 
        threads_t** arr = aligned_alloc(alignof(threads_t), 4);
        if (arr) memset(arr, 0, 4);
        queue_t* q = (void*)0;
        QUEUE_INIT(q);
        
        // --------
        // Add the data members that can be operated on by the data structure for queue 
        // Add in the asserts
        // --------

        list_t* llist = (void*)0;
        LIST_INIT(llist); 
        // --------
        // Add the data members that can be operated on by the data structure for linked lists
        // Add in the asserts
        // --------

        bst_t* bst = (void*)0;
        BST_INIT(bst);

        // --------
        // Add the data members that can be operated on by the data structure for binary search tree
        // Add in the asserts
        // --------

        hash_table_t* ht = (void*)0;
        HASH_INIT(ht);

        // --------
        // Add the data members that can be operated on by the data structure for hash table
        // Add in the asserts
        // --------

        for (int i = 0; i < 4; i++) clean_threads(arr[i]);
    }

    {
        // An array of threads that is not threads_t 
    }

    {
        // Singular thread testing 

    }

    for (int i = 0; i <  8; i++ ) clean_threads(threads[i]);


    return 0;
}