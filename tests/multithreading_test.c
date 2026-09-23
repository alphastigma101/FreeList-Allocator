#include "../tests/tests.h"
#include <limits.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h> // Development
#include <string.h>
#include <sys/types.h>
#include "../threads/threads.h" // Production
#include "../DataStructures/C/structures.h" // Development

typedef struct __attribute__((aligned(DEFAULT_ALIGNMENT))) task_metadata_t {
    int task_status[4];
    int total_tasks;
    int total_task_succession;
    int total_task_failed;
} task_metadata_t;
task_metadata_t task_metadata = {0};
queue_t queue = {0};
/*
#include "../tests/tests.h"
#include <stdlib.h>
#include <stdalign.h> // Development
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "../threads/threads.h" // Production
//#include "../DataStructures/C/structures.h" // Development
*/

static threads_t* threads = NULL;

// Numerical Addition Helper
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

static void* subtract(struct function_t* meta) {
    return nullptr;
}

static void* multiply(struct function_t* meta) {
    return nullptr;
}

static void* divide(struct function_t* meta) {

    return nullptr;
}

// Numerical Random Data Helper
static inline void* queue_t_random_numerical_values(struct function_t* meta) {
    void** args = routine_metadata_arguments(meta);
    if (!args) pthread_exit(NULL);
    
    size_t length = *(size_t*)args[0];
    size_t range = *(size_t*)args[1];
    
    for (size_t i = 0; i < length; i++) {
        size_t value = (size_t)(rand());
        value = value % range;
        QUEUE_ENQUEUE(&queue, &value, "external");
    }

    pthread_exit(NULL);
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
        EXPECT_EQ(*pos - before, (size_t)1);
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


TEST(Modes, Thread) {
    threads_t t0;
    #if THREADS_T_ENABLE_ATTRIBUTE == 1
        t0 = init_threads_t();
        EXPECT_EQ(t0.attr.status, 0);
        #if THREADS_T_ENABLE_STACK == 1
            EXPECT_NE(&t0.attr.stack, nullptr);
        #endif
        clean_threads(&t0);
    #endif
    #if THREADS_T_ENABLE_LOCK == 1
        t0 = init_threads_t();
        #if THREADS_T_ENABLE_MUTEX_ATTR == 1
            EXPECT_NE(&t0.lock.mutex_attr, nullptr);
        #endif
        EXPECT_EQ(t0.lock.status, 0);
        clean_threads(&t0);
    #endif
    #if THREADS_T_ENABLE_CONDITION == 1
        t0 = init_threads_t();
        #if THREADS_T_ENABLE_CONDITION_ATTR == 1
            EXPECT_NE(&t0.cond.attr, nullptr);
        #endif
        EXPECT_EQ(t0.cond.status, 0);
        clean_threads(&t0);
    #endif
}

TEST(ThreadPool, NumericValue) {
    int* i = shared_address(nullptr, sizeof(int), PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
    atomic_int size;
    atomic_store(&size, 1);
    ASSERT_NE(i, MAP_FAILED);
    *i = 0;
    #if THREADS_T_ENABLE_LOCK == 1
        routine_metadata(&threads[1], 3, &threads[1].lock.mutex, &size, i);
    #else 
        routine_metadata(&threads[1], 2U, &size, i);
    #endif
    threads_t_query(threads, 4U, thread_arguments, 1);

    int main_value     = 0;
    int last_thread_value = -1;
    int current_thread_value = 0;

    for (int j = 0; j < 1000; j++) {
        main_value = j + 1;
        #if THREADS_T_ENABLE_LOCK == 1
            pthread_mutex_lock(&threads[1].lock.mutex);
            current_thread_value = *i;
            pthread_mutex_unlock(&threads[1].lock.mutex);
        #else 
            current_thread_value = *i;
        #endif
        EXPECT_EQ(main_value, j + 1);
        EXPECT_GE(current_thread_value, last_thread_value);
        last_thread_value = current_thread_value;
    }

    EXPECT_EQ(main_value, 1000);
    EXPECT_EQ(*i, 1002);
    munmap_address(i, sizeof(int), __FILE__,  __LINE__);
}

TEST(ThreadPool, StringTraversal) {
    const char* src1 = "This is a short string";
    const char* src2 = "This is a very very very very long string";
    size_t len1 = strlen(src1), len2 = strlen(src2);

    char* one = shared_address(NULL, len1 + 1, PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
    memcpy(one, src1, len1 + 1);
    char* two = shared_address(NULL, len2 + 1, PROT_WRITE | PROT_READ, INT_MAX, -1, 0);
    memcpy(two, src2, len2 + 1);

    size_t one_pos = 0, two_pos = 0;
    #if THREADS_T_ENABLE_LOCK == 1
        routine_metadata(&threads[1], 3, one, &threads[1].lock.mutex, &one_pos);
        routine_metadata(&threads[1], 3, two, &threads[2].lock.mutex, &two_pos);
    #else 
        routine_metadata(&threads[1], 2, one, &one_pos);
        routine_metadata(&threads[2], 2, two, &two_pos);
    #endif
    threads_t_query(threads, 4U, thread_arguments, 1);
    threads_t_query(threads, 4U, thread_arguments, 1);

    float esc = 0.0;
    float r1 = 0.0;
    float r2 = 0.0;
    for (;;) {
        if (esc == 1) break;
        #if THREADS_T_ENABLE_LOCK == 1
            pthread_mutex_lock(&threads[1].lock.mutex);
            if (one_pos < len1) {
                size_t before = one_pos;
                size_t step = (len1 - one_pos < 2) ? (len1 - one_pos) : 2;
                one_pos += step;
                EXPECT_EQ(one_pos - before, step);
            } else r1 = 0.5;
            pthread_mutex_unlock(&threads[1].lock.mutex);
        #else 
            if (one_pos < len1) {
                size_t before = one_pos;
                size_t step = (len1 - one_pos < 2) ? (len1 - one_pos) : 2;
                one_pos += step;
                EXPECT_EQ(one_pos - before, step);
            } else r1 = 0.5;
        #endif
        #if THREADS_T_ENABLE_LOCK == 1
            pthread_mutex_lock(&threads[2].lock.mutex);
            if (two_pos < len2) {
                size_t before = two_pos;
                size_t step = (len2 - two_pos < 2) ? (len2 - two_pos) : 2;
                two_pos += step;
                EXPECT_EQ(two_pos - before, step);
            } else r2 = 0.5;
            pthread_mutex_unlock(&threads[2].lock.mutex);
        #else 
            if (two_pos < len2) {
                size_t before = two_pos;
                size_t step = (len2 - two_pos < 2) ? (len2 - two_pos) : 2;
                two_pos += step;
                EXPECT_EQ(two_pos - before, step);
            } else r2 = 0.5;
        #endif
        esc = r1 + r2;
    }

    for (int i = 1; i < 3; i++) {
        join_thread(&threads[i], NULL);
    }

    munmap_address(one, len1 + 1, __FILE__,  __LINE__);
    munmap_address(two, len2 + 1, __FILE__,  __LINE__);
}

TEST(LockFreeThreadPool, StringTraversal) {
    for (int i = 0; i < 4; i++ ) clean_threads(&threads[i]);
    memset(threads, 0, sizeof(threads_t) * 4);
    create_thread_pool(threads, 4U);
    
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
        if (t1_res != (float)0.5) {
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

        if (t2_res != (float)0.5) {
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

TEST(Manager, Query) {
    for (int i = 0; i < 4; i++ ) clean_threads(&threads[i]);
    memset(threads, 0, sizeof(threads_t) * 4U);
    create_thread_pool(threads, 4U);
    memset(task_metadata.task_status, 0, sizeof(int) * 4);
    int a = INT_MAX, b = 0, c = INT_MAX, d = 0;
    do {
        threads_t* t1 = find_thread_t(threads, 4);
        if (!t1) task_metadata.total_task_failed++;
        if ((!(a <= 0)) && task_metadata.task_status[0] == 0) {
            routine_metadata(t1, 1, &a);
            const int res = threads_t_query(threads, 4U, subtract, 1);
            if (res == -1) task_metadata.total_task_failed++;
        } else if (a <= 0 && task_metadata.task_status[0] == 0) { task_metadata.total_tasks++; task_metadata.task_status[0] = 1; }
        if ((!(b != INT_MAX)) && task_metadata.task_status[1] == 0) {
            routine_metadata(t1, 1, &b);
            const int res = threads_t_query(threads, 4U, addition, 1);
            if (res == -1) task_metadata.total_task_failed++;
        } else if (b == INT_MAX && task_metadata.task_status[1] == 0) { task_metadata.total_tasks++; task_metadata.task_status[1] = 1; }
        if ((!(d != INT_MAX)) && task_metadata.task_status[2] == 0) {
            routine_metadata(t1, 1, &d);
            const int res = threads_t_query(threads, 4U, multiply, 1);
            if (res == -1) task_metadata.total_task_failed++;
        } else if (d == INT_MAX && task_metadata.task_status[2] == 0) { task_metadata.total_tasks++; task_metadata.task_status[2] = 1; }
        if ((!(c < 50)) && (task_metadata.task_status[3] == 0)) {
            routine_metadata(t1, 1, &c);
            const int res = threads_t_query(threads, 4U, divide, 1);
            if (res == -1) task_metadata.total_task_failed++;
        } else if (c < 50 && task_metadata.task_status[3] == 0) { task_metadata.total_tasks++; task_metadata.task_status[3] = 1; }
    } while(task_metadata.total_tasks != 3);
    EXPECT_LE(task_metadata.total_task_failed, 0);
    EXPECT_GE(task_metadata.total_task_succession, 1);  
}

// LF == Lock Free
// TP = Thread Pool
/*TEST(LFTPExternal, Queue) {
    for (int i = 0; i < 4; i++ ) clean_threads(&threads[i]);
    memset(threads, 0, sizeof(threads_t) * 4);
    create_thread_pool(threads, 4, 0x0, 0x0, 0x0, 0x0);

    QUEUE_INIT(&queue, 0x01, 0x01, 0x0, 0x0, 10UL);
    
    size_t length = 50, range = 100;
    // Thread A 
    routine_metadata(&threads[0], 2, &length, &range);
    create_thread(&threads[0], queue_t_random_numerical_values);
    
    length = 150, range = 200;
    // Thread B
    routine_metadata(&threads[1], 2, &length, &range);
    create_thread(&threads[1], queue_t_random_numerical_values);
    
    size_t count = 0;
    void* item = NULL;
    while (count != 200 / 2) {
        QUEUE_DEQUEUE(&queue, item);
        count++;
    }
    (void)item;

    join_thread(&threads[0], NULL);
    join_thread(&threads[1], NULL);
    EXPECT_EQ(QUEUE_SIZE(&queue), (size_t)100);
    QUEUE_DESTROY(&queue);
    
}*/


TEST(Thread, Clean) {
    for (int i = 0; i < 4; i++) clean_threads(&threads[i]);
    free(threads);
    threads = NULL;
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
    threads = aligned_alloc(alignof(threads_t), 4U * sizeof(threads_t));
    memset(threads, 0, 4U * sizeof(threads_t));
    create_thread_pool(threads, 4U);
    return __run_all_tests();
}