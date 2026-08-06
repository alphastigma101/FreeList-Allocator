#include "../tests/tests.h"
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h> // Development
#include <string.h>
#include <sys/types.h>
#include "../threads/threads.h" // Production
//#include "../DataStructures/C/structures.h" // Development

/*
#include "../tests/tests.h"
#include <stdlib.h>
#include <stdalign.h> // Development
#include <string.h>
#include <sys/types.h>
#include "../threads/threads.h" // Production
//#include "../DataStructures/C/structures.h" // Development
*/

static threads_t* threads = NULL;

// Numerical Helper
static void* addition(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    int* i = args[2];
    pthread_mutex_t* mutex = (pthread_mutex_t*)args[0];
    for (int j = 0; *i < 1000; j+=3) { 
        pthread_mutex_lock(mutex);
        *i = j;
        pthread_mutex_unlock(mutex);
    }
    return NULL;
}

// String Traversal Test mode can be either forward or backwards
static void* traversal(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);

    char* _str = (char*)args[0];
    pthread_mutex_t* mutex = (pthread_mutex_t*)args[1];
    size_t* pos = (size_t*)args[2];
    size_t total_len = strlen(_str);

    while (1) {
        pthread_mutex_lock(mutex);
        if (*pos >= total_len) {
            pthread_mutex_unlock(mutex);
            break;
        }
        size_t before = *pos;
        (*pos)++;
        EXPECT_EQ(*pos - before, 1);
        pthread_mutex_unlock(mutex);
    }
    return NULL;
}

static void* lf_traversal(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    atomic_uintptr_t* _ptr = (atomic_uintptr_t*)args[0];

    for (;;) {
        uintptr_t current = atomic_load(_ptr);
        const char* str = (const char*)current;
        if (*str == '\0') break;
        uintptr_t next = current + 1;
        atomic_compare_exchange_strong(_ptr, &current, next);
    }
    return NULL;
}

void* thread_arguments(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    atomic_int* ptr = (atomic_int*)args[1]; 
    const int size = atomic_load(ptr);
    switch (size) {
        case 1:
            addition(meta);
            break;
        case 2:
            lf_traversal(meta);
            break;
        case 3:
            traversal(meta);
            break;
        default:
            break;
    }    
    pthread_exit(NULL);
}

TEST(LockThreadPool, NumericValue) {
    int* i = shared_address(NULL, sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_int size;
    atomic_store(&size, 1);
    EXPECT_NE(i, MAP_FAILED);
    *i = 0;
    routine_metadata(&threads[0], 3, &threads[0].lock->mutex, &size, i);
    create_thread(&threads[0], thread_arguments);

    int main_value     = 0;
    int last_thread_value = -1;
    int current_thread_value = 0;

    for (int j = 0; j < 1000; j++) {
        main_value = j + 1;
        pthread_mutex_lock(&threads[0].lock->mutex);
        current_thread_value = *i;
        pthread_mutex_unlock(&threads[0].lock->mutex);

        EXPECT_EQ(main_value, j + 1);
        EXPECT_GE(current_thread_value, last_thread_value);
        last_thread_value = current_thread_value;
    }

    EXPECT_EQ(main_value, 1000);
    EXPECT_EQ(*i, 1002);
    munmap_address(i, sizeof(int));
    join_thread(&threads[0], NULL);
}

TEST(LockThreadPool, StringTraversal) {
    const char* src1 = "This is a short string";
    const char* src2 = "This is a very very very very long string";
    size_t len1 = strlen(src1), len2 = strlen(src2);

    char* one = shared_address(NULL, len1 + 1, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memcpy(one, src1, len1 + 1);
    char* two = shared_address(NULL, len2 + 1, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memcpy(two, src2, len2 + 1);

    size_t one_pos = 0, two_pos = 0;

    routine_metadata(&threads[1], 3, one, &threads[1].lock->mutex, &one_pos);
    create_thread(&threads[1], thread_arguments);
    routine_metadata( &threads[2], 3, two, &threads[2].lock->mutex, &two_pos);
    create_thread(&threads[2], thread_arguments);

    float esc = 0.0;
    float r1 = 0.0;
    float r2 = 0.0;
    for (;;) {
        if (esc == 1) break;
        pthread_mutex_lock(&threads[1].lock->mutex);
        if (one_pos < len1) {
            size_t before = one_pos;
            size_t step = (len1 - one_pos < 2) ? (len1 - one_pos) : 2;
            one_pos += step;
            EXPECT_EQ(one_pos - before, step);
        } else r1 = 0.5;
        pthread_mutex_unlock(&threads[1].lock->mutex);

        pthread_mutex_lock(&threads[2].lock->mutex);
        if (two_pos < len2) {
            size_t before = two_pos;
            size_t step = (len2 - two_pos < 2) ? (len2 - two_pos) : 2;
            two_pos += step;
            EXPECT_EQ(two_pos - before, step);
        } else r2 = 0.5;
        pthread_mutex_unlock(&threads[2].lock->mutex);
        esc = r1 + r2;
    }

    for (int i = 1; i < 3; i++) {
        join_thread(&threads[i], NULL);
    }

    munmap_address(one, len1 + 1);
    munmap_address(two, len2 + 1);
}

TEST(LockFreeThreadPool, StringTraversal) {
    for (int i = 0; i < 4; i++ ) clean_threads(&threads[i]);
    memset(threads, 0, sizeof(threads_t) * 4);
    create_thread_pool(threads, 4, 0x0, 0x0, 0x0);
    
    atomic_uintptr_t one, two;
    
    const char* src1 = "This is a short string, but every thread that access me will not be locked\n";
    atomic_store(&one, (uintptr_t)src1);
    const char* src2 = "This is a very very very very very very long string, and I mean very long. This will also have no lock on it so ervery thread should be able to access it\n";
    atomic_store(&two, (uintptr_t)src2);
    
    atomic_int size;
    atomic_store(&size, 2);
    routine_metadata(&threads[0], 2, &one, &size);
    create_thread(&threads[0], thread_arguments);
    routine_metadata(&threads[1], 2, &two, &size);
    create_thread(&threads[1], thread_arguments);
    
    float t1_res = 0.0, t2_res = 0.0;
    float res = 0.0;

    for (;;) {
        if (res == 1) break;
        if (t1_res != 0.5) {
            uintptr_t current = atomic_load(&one);
            const char* str = (const char*)current;
            if (*str == '\0') t1_res = 0.5;
            else {
                size_t remaining = strlen(str);
                uintptr_t step = (remaining < 2) ? remaining : 2;
                uintptr_t next = current + step;
                if (atomic_compare_exchange_strong(&one, &current, next)) {
                    const char* new_str = (const char*)next;
                    EXPECT_EQ(strlen(str) - strlen(new_str), step);
                }
            }
        }

        if (t2_res != 0.5) {
            uintptr_t current = atomic_load(&two);
            const char* str = (const char*)current;
            if (*str == '\0') t2_res = 0.5;
            else {
                size_t remaining = strlen(str);
                uintptr_t step = (remaining < 2) ? remaining : 2;
                uintptr_t next = current + step;
                if (atomic_compare_exchange_strong(&two, &current, next)) {
                    const char* new_str = (const char*)next;
                    EXPECT_EQ(strlen(str) - strlen(new_str), step);
                }
            }
        }

        res = t1_res + t2_res;
    }

    for (int i = 0; i < 2; i++ ) join_thread(&threads[i], NULL);
}


TEST(Thread, Clean) {
    for (int i = 0; i < 4; i++) clean_threads(&threads[i]);
    free(threads);
    threads = NULL;
}



// TODO: ZSTD Also has its own threading library, so that needs to be integrated into busybox's configuration

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
    threads = aligned_alloc(alignof(threads_t), 4 * sizeof(threads_t));
    memset(threads, 0, 4 * sizeof(threads_t));
    create_thread_pool(threads, 4, 0x0, 0x01, 0x0);
    

    // User defined data structures / Objects 
    // Create a queue, linked lists, binary search tree, and a couple other data structures 
    // that will really test to see if multi-threading is working or not 
    /*{ 
        
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

    }

    {
        // An array of threads that is not threads_t 
    }

    {
        // Singular thread testing 

    }*/


    return __run_all_tests();
}