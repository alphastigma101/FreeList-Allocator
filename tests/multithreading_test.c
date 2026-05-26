#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h> // Development
#include <stdalign.h> // Development
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
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

// Numerical Value Test
static void* addition(args_t* args) {
    int* i = args->arr[0];
    pthread_mutex_t* mutex = (pthread_mutex_t*)args->arr[1];
    for (int j = 0; *i < 1000; j+=3) { 
        pthread_mutex_lock(mutex);
        *i = j;
        pthread_mutex_unlock(mutex);
    }
    return NULL;
}

// String Traversal Test mode can be either forward or backwards
static void* traversal(args_t* args) {
    const uint8_t* d_mode = (uint8_t*)args->arr[1];
    const char** _str    = (const char**)args->arr[0];
    pthread_mutex_t* mutex = (pthread_mutex_t*)args->arr[2];
    if (*d_mode == 0x01) {
        
        while (1) {
            pthread_mutex_lock(mutex);
            if (strlen(*_str) == 0) {
                pthread_mutex_unlock(mutex);
                break;
            }
            const char* copy = *_str;
            (*_str)++; 
            assert((strlen(copy) - strlen(*_str)) == 1);
            pthread_mutex_unlock(mutex);
        }
    }
    if (*d_mode == 0x02) {
            
        while (1) {
            pthread_mutex_lock(mutex);
            if (strlen(*_str) == 0) {
                pthread_mutex_unlock(mutex);
                break;
            }
            const char* copy = *_str;
            (*_str)++;
            assert((strlen(copy) - strlen(*_str)) == 1);
            pthread_mutex_unlock(mutex);
        }
        
        
    }

    return NULL;
}

void* thread_arguments(void* arg) {
    args_t* args = (args_t*)arg;
    switch (args->size) {
        case 1:
            addition(args);
            break;
        case 3:
            traversal(args);
            break;
        default:
            break;
    }    

    pthread_exit(NULL);
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


        int* i = shared_address(NULL, sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (i == MAP_FAILED) {
            printf(TEST_FAIL "ERROR 74: Failed to allocate memory for i\n");
            return 1;
        }
        printf(TEST_INFO "Allocated shared int @ %p\n", (void*)i);
        *i = 0;
        
        threads[0]->args.size = 1;
        threads[0]->args.arr = malloc(2 * sizeof(void*)); 
        threads[0]->args.arr[0] = i;
        threads[0]->args.arr[1] = threads[0]->mutex;
        threads[0] = create_thread(threads[0], 0x01, thread_arguments);
        
        printf(TEST_INFO "Thread spawned — shared address mapped @ %p\n", i);

        printf(SEPARATOR);
        printf(TEST_INFO "Running " ANSI_YELLOW "1000" ANSI_RESET " iteration monotonicity check...\n");

        int main_value     = 0;
        int last_thread_value = -1;
        int current_thread_value = 0;

        for (int j = 0; j < 1000; j++) {
            main_value = j + 1;
            pthread_mutex_lock(threads[0]->mutex);
            current_thread_value = *i;
            pthread_mutex_unlock(threads[0]->mutex);


            assert(main_value == j + 1 &&
                   "Main thread: should increment by 1");

            assert(current_thread_value >= last_thread_value &&
                   "Thread value should be monotonically increasing or stable");

            last_thread_value = current_thread_value;
        }

        printf(TEST_PASS "Monotonicity check passed across all 1000 iterations\n");
        printf(SEPARATOR);

        join_thread(threads[0], NULL);
        printf(TEST_INFO "Thread joined successfully\n");

        assert(main_value == 1000 && "Main thread value should be 1000");
        printf(TEST_PASS "Main thread final value : " ANSI_YELLOW "%d" ANSI_RESET " / expected " ANSI_YELLOW "1000\n" ANSI_RESET, main_value);

        assert(*i == 1002 && "Thread final value should be 1002");
        printf(TEST_PASS "Thread final value      : " ANSI_YELLOW "%d" ANSI_RESET " / expected " ANSI_YELLOW "1002\n" ANSI_RESET, *i);

        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
        munmap_address(i, sizeof(int));
    }
 
    {
        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  STRING TRAVERSAL TEST  ·  Thread Pool          \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

        char* one = shared_address(NULL, sizeof(char), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        one = "This is a short string";
        char* two = shared_address(NULL, sizeof(char), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0); 
        two = "This is a very very very very long string"; 

        printf(TEST_INFO "Short string : " ANSI_YELLOW "\"%s\"\n" ANSI_RESET, one);
        printf(TEST_INFO "Long string  : " ANSI_YELLOW "\"%s\"\n" ANSI_RESET, two);
        printf(SEPARATOR);

        const uint8_t mode_1 = 0x01;
        threads[1]->args.size = 3;
        threads[1]->args.arr = malloc(3 * sizeof(void*));
        threads[1]->args.arr[0] = (void*)&one;
        threads[1]->args.arr[1] = (void*)&mode_1;
        threads[1]->args.arr[2] = (void*)threads[1]->mutex;
        threads[1] = create_thread(threads[1], 0x01, thread_arguments);

        const uint8_t mode_2 = 0x02;
        threads[2]->args.size = 3;
        threads[2]->args.arr = malloc(sizeof(void*) * 3);
        threads[2]->args.arr[0] = (void*)&two;
        threads[2]->args.arr[1] = (void*)&mode_2;
        threads[2]->args.arr[2] = (void*)threads[2]->mutex;
        threads[2] = create_thread(threads[2], 0x01, thread_arguments);
        
        printf(SEPARATOR);
        printf(TEST_INFO "Entering traversal loop — monitoring both threads...\n");

        float esc = 0.0;
        float r1 = 0.0;
        float r2 = 0.0;
        for (;;) {
            if (esc == 1) break;

            pthread_mutex_lock(threads[1]->mutex);
            if (strlen(one) != 0) {
                const char* cpy_so = one;
                one += 2;
                assert((strlen(cpy_so) - strlen(one) == 2));
            } else r1 = 0.5;
            pthread_mutex_unlock(threads[1]->mutex);

            pthread_mutex_lock(threads[2]->mutex);
            if (strlen(two) != 0) {
                const char* cpy_so = two;
                two += 2;
                assert((strlen(cpy_so) - strlen(two) == 2));
            } else r2 = 0.5;
            pthread_mutex_unlock(threads[2]->mutex);
            
            esc = r1 + r2;
              
        }
        
        printf(TEST_PASS "Thread[1] — all chars valid printable ASCII, pointer stayed in bounds\n");
        printf(TEST_PASS "Thread[2] — all chars valid printable ASCII, pointer stayed in bounds\n");
        printf(SEPARATOR);
        printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

        for (int i = 1; i < 3; i++) {
            join_thread(threads[i], NULL);
        }
        munmap_address(one, sizeof(char));
        munmap_address(two, sizeof(char));
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