#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>

/* ============================================================================
 * QUEUE (Thread-Safe FIFO)
 * ============================================================================ */

typedef struct queue_node_t {
    void* data;
    struct queue_node_t* next;
} queue_node_t;

typedef struct queue_t {
    queue_node_t* head;
    queue_node_t* tail;
    pthread_mutex_t lock;
    _Atomic size_t size;
} queue_t;

// Queue operations
#define QUEUE_INIT(q) do { \
    (q)->head = NULL; \
    (q)->tail = NULL; \
    pthread_mutex_init(&(q)->lock, NULL); \
    atomic_init(&(q)->size, 0); \
} while(0)

#define QUEUE_ENQUEUE(q, item) do { \
    queue_node_t* node = malloc(sizeof(queue_node_t)); \
    node->data = (item); \
    node->next = NULL; \
    pthread_mutex_lock(&(q)->lock); \
    if ((q)->tail) (q)->tail->next = node; \
    else (q)->head = node; \
    (q)->tail = node; \
    atomic_fetch_add(&(q)->size, 1); \
    pthread_mutex_unlock(&(q)->lock); \
} while(0)

#define QUEUE_DEQUEUE(q, result) do { \
    pthread_mutex_lock(&(q)->lock); \
    if ((q)->head) { \
        queue_node_t* node = (q)->head; \
        (result) = node->data; \
        (q)->head = node->next; \
        if (!(q)->head) (q)->tail = NULL; \
        free(node); \
        atomic_fetch_sub(&(q)->size, 1); \
    } else { \
        (result) = NULL; \
    } \
    pthread_mutex_unlock(&(q)->lock); \
} while(0)

#define QUEUE_SIZE(q) atomic_load(&(q)->size)

#define QUEUE_DESTROY(q) do { \
    void* item; \
    while ((q)->head) { \
        QUEUE_DEQUEUE(q, item); \
    } \
    pthread_mutex_destroy(&(q)->lock); \
} while(0)

/* ============================================================================
 * LINKED LIST (Thread-Safe Doubly-Linked)
 * ============================================================================ */

typedef struct list_node_t {
    void* data;
    struct list_node_t* next;
    struct list_node_t* prev;
} list_node_t;

typedef struct list_t {
    list_node_t* head;
    list_node_t* tail;
    pthread_rwlock_t rwlock;
    _Atomic size_t size;
} list_t;

#define LIST_INIT(l) do { \
    (l)->head = NULL; \
    (l)->tail = NULL; \
    pthread_rwlock_init(&(l)->rwlock, NULL); \
    atomic_init(&(l)->size, 0); \
} while(0)

#define LIST_APPEND(l, item) do { \
    list_node_t* node = malloc(sizeof(list_node_t)); \
    node->data = (item); \
    node->next = NULL; \
    pthread_rwlock_wrlock(&(l)->rwlock); \
    node->prev = (l)->tail; \
    if ((l)->tail) (l)->tail->next = node; \
    else (l)->head = node; \
    (l)->tail = node; \
    atomic_fetch_add(&(l)->size, 1); \
    pthread_rwlock_unlock(&(l)->rwlock); \
} while(0)

#define LIST_PREPEND(l, item) do { \
    list_node_t* node = malloc(sizeof(list_node_t)); \
    node->data = (item); \
    node->prev = NULL; \
    pthread_rwlock_wrlock(&(l)->rwlock); \
    node->next = (l)->head; \
    if ((l)->head) (l)->head->prev = node; \
    else (l)->tail = node; \
    (l)->head = node; \
    atomic_fetch_add(&(l)->size, 1); \
    pthread_rwlock_unlock(&(l)->rwlock); \
} while(0)

#define LIST_SIZE(l) atomic_load(&(l)->size)

#define LIST_DESTROY(l) do { \
    pthread_rwlock_wrlock(&(l)->rwlock); \
    list_node_t* current = (l)->head; \
    while (current) { \
        list_node_t* next = current->next; \
        free(current); \
        current = next; \
    } \
    pthread_rwlock_unlock(&(l)->rwlock); \
    pthread_rwlock_destroy(&(l)->rwlock); \
} while(0)

/* ============================================================================
 * BINARY SEARCH TREE (Thread-Safe)
 * ============================================================================ */

typedef struct bst_node_t {
    int key;
    void* data;
    struct bst_node_t* left;
    struct bst_node_t* right;
} bst_node_t;

typedef struct bst_t {
    bst_node_t* root;
    pthread_rwlock_t rwlock;
    _Atomic size_t size;
} bst_t;

#define BST_INIT(tree) do { \
    (tree)->root = NULL; \
    pthread_rwlock_init(&(tree)->rwlock, NULL); \
    atomic_init(&(tree)->size, 0); \
} while(0)

static inline bst_node_t* bst_insert_node(bst_node_t* node, int key, void* data, _Atomic size_t* size) {
    if (!node) {
        node = (bst_node_t*)malloc(sizeof(bst_node_t));
        node->key = key;
        node->data = data;
        node->left = NULL;
        node->right = NULL;
        atomic_fetch_add(size, 1);
        return node;
    }
    if (key < node->key) {
        node->left = bst_insert_node(node->left, key, data, size);
    } else if (key > node->key) {
        node->right = bst_insert_node(node->right, key, data, size);
    }
    return node;
}

#define BST_INSERT(tree, key, data) do { \
    pthread_rwlock_wrlock(&(tree)->rwlock); \
    (tree)->root = bst_insert_node((tree)->root, (key), (data), &(tree)->size); \
    pthread_rwlock_unlock(&(tree)->rwlock); \
} while(0)

#define BST_SIZE(tree) atomic_load(&(tree)->size)

static inline void bst_destroy_nodes(bst_node_t* node) {
    if (!node) return;
    bst_destroy_nodes(node->left);
    bst_destroy_nodes(node->right);
    free(node);
}

#define BST_DESTROY(tree) do { \
    pthread_rwlock_wrlock(&(tree)->rwlock); \
    bst_destroy_nodes((tree)->root); \
    pthread_rwlock_unlock(&(tree)->rwlock); \
    pthread_rwlock_destroy(&(tree)->rwlock); \
} while(0)

/* ============================================================================
 * ATOMIC COUNTER (Lock-Free - Stress Test)
 * ============================================================================ */

typedef struct atomic_counter_t {
    _Atomic uint64_t value;
} atomic_counter_t;

#define COUNTER_INIT(c) atomic_init(&(c)->value, 0)
#define COUNTER_INC(c) atomic_fetch_add(&(c)->value, 1)
#define COUNTER_DEC(c) atomic_fetch_sub(&(c)->value, 1)
#define COUNTER_GET(c) atomic_load(&(c)->value)
#define COUNTER_SET(c, val) atomic_store(&(c)->value, (val))

/* ============================================================================
 * CONCURRENT HASH TABLE (Lock-Striped)
 * ============================================================================ */

#define HASH_BUCKETS 256

typedef struct hash_entry_t {
    uint32_t key;
    void* value;
    struct hash_entry_t* next;
} hash_entry_t;

typedef struct hash_table_t {
    hash_entry_t* buckets[HASH_BUCKETS];
    pthread_mutex_t locks[HASH_BUCKETS];
    _Atomic size_t size;
} hash_table_t;

#define HASH_INIT(ht) do { \
    for (int i = 0; i < HASH_BUCKETS; i++) { \
        (ht)->buckets[i] = NULL; \
        pthread_mutex_init(&(ht)->locks[i], NULL); \
    } \
    atomic_init(&(ht)->size, 0); \
} while(0)

#define HASH_PUT(ht, k, v) do { \
    uint32_t bucket = (k) % HASH_BUCKETS; \
    hash_entry_t* entry = malloc(sizeof(hash_entry_t)); \
    entry->key = (k); \
    entry->value = (v); \
    pthread_mutex_lock(&(ht)->locks[bucket]); \
    entry->next = (ht)->buckets[bucket]; \
    (ht)->buckets[bucket] = entry; \
    atomic_fetch_add(&(ht)->size, 1); \
    pthread_mutex_unlock(&(ht)->locks[bucket]); \
} while(0)

#define HASH_SIZE(ht) atomic_load(&(ht)->size)

#define HASH_DESTROY(ht) do { \
    for (int i = 0; i < HASH_BUCKETS; i++) { \
        pthread_mutex_lock(&(ht)->locks[i]); \
        hash_entry_t* entry = (ht)->buckets[i]; \
        while (entry) { \
            hash_entry_t* next = entry->next; \
            free(entry); \
            entry = next; \
        } \
        pthread_mutex_unlock(&(ht)->locks[i]); \
        pthread_mutex_destroy(&(ht)->locks[i]); \
    } \
} while(0)

/* ============================================================================
 * RING BUFFER (Lock-Free SPSC - Single Producer Single Consumer)
 * ============================================================================ */

#define RING_SIZE 1024

typedef struct ring_buffer_t {
    void* data[RING_SIZE];
    _Atomic size_t head;
    _Atomic size_t tail;
} ring_buffer_t;

#define RING_INIT(rb) do { \
    atomic_init(&(rb)->head, 0); \
    atomic_init(&(rb)->tail, 0); \
} while(0)

#define RING_PUSH(rb, item, success) do { \
    size_t head = atomic_load(&(rb)->head); \
    size_t next = (head + 1) % RING_SIZE; \
    if (next != atomic_load(&(rb)->tail)) { \
        (rb)->data[head] = (item); \
        atomic_store(&(rb)->head, next); \
        (success) = 1; \
    } else { \
        (success) = 0; \
    } \
} while(0)

#define RING_POP(rb, item, success) do { \
    size_t tail = atomic_load(&(rb)->tail); \
    if (tail != atomic_load(&(rb)->head)) { \
        (item) = (rb)->data[tail]; \
        atomic_store(&(rb)->tail, (tail + 1) % RING_SIZE); \
        (success) = 1; \
    } else { \
        (item) = NULL; \
        (success) = 0; \
    } \
} while(0)

#endif /* STRUCTURES_H */