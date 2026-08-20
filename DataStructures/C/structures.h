#ifndef STRUCTURES_H
#define STRUCTURES_H


#include "../../threads/threads.h"
#include <string.h>

/* ============================================================================
 * QUEUE (Thread-Safe FIFO)
 * ============================================================================ */

typedef struct queue_node_t {
    _Atomic(void*) data;
    _Atomic(struct queue_node_t*) next;
} queue_node_t;

typedef struct queue_t {
    _Atomic(queue_node_t*) head;
    _Atomic(queue_node_t*) tail;
    threads_t*    pool;
    _Atomic size_t size;
} queue_t;

// Queue operations
#define QUEUE_INIT(q, _mode, _attr, _locked, _stack, _size) do { \
    __typeof__(queue_t *) _qi = (q); \
    if (!_qi) _qi = shared_address(_qi, sizeof(queue_t), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0); \
    if (_qi) { \
        memset(_qi, 0, sizeof(queue_t)); \
        _qi->head = NULL; \
        _qi->tail = NULL; \
        create_thread_pool(_qi->pool, _size, _mode, _attr, _locked, _stack); \
        atomic_init(&_qi->size, 0); \
    } \
} while(0)

#define QUEUE_ENQUEUE(q, item, type) do { \
    __typeof__(queue_t *) _qe = (q); \
    if (_qe) { \
        queue_node_t* node = shared_address(NULL, sizeof(queue_node_t), PROT_READ | PROT_WRITE, MAP_NORESERVE, -1, 0); \
        if (node) { \
            memset(node, 0, sizeof(queue_node_t)); \
            atomic_store_explicit(&node->data, (item), memory_order_relaxed); \
            node->next = NULL; \
            if (atomic_load_explicit(&_qe->tail, memory_order_relaxed) != NULL) atomic_store_explicit(&_qe->tail->next, node, memory_order_relaxed); \
            else atomic_store_explicit(&_qe->head, node, memory_order_relaxed); \
            atomic_store_explicit(&_qe->tail, node, memory_order_relaxed); \
            atomic_fetch_add_explicit(&_qe->size, 1, memory_order_relaxed); \
        } \
    } \
} while(0)

#define QUEUE_DEQUEUE(q, result) do { \
    __typeof__(queue_t *) _qd = (q); \
    if (_qd) { \
        if (atomic_load_explicit(&_qd->head, memory_order_relaxed) != NULL) { \
            queue_node_t* node = atomic_load_explicit(&_qd->head, memory_order_relaxed); \
            (result) = atomic_load_explicit(&node->data, memory_order_relaxed);  \
            atomic_store_explicit(&_qd->head, node->next, memory_order_relaxed); \
            if (!atomic_load_explicit(&_qd->head, memory_order_relaxed)) _qd->tail = NULL; \
            if (node) munmap_address(node, sizeof(queue_node_t), __FILE__,  __LINE__); \
            node = NULL; \
            atomic_fetch_sub_explicit(&_qd->size, 1, memory_order_relaxed); \
        } else { \
            (result) = NULL; \
        } \
    } \
} while(0)

#define QUEUE_SIZE(q) \
    ({(q) != NULL ? atomic_load_explicit(&(q)->size, memory_order_relaxed) : 0; })

#define QUEUE_DESTROY(q) do { \
    __typeof__(queue_t *) _qx = (q); \
    if (_qx) { \
        void* _qx_item = NULL; \
        while (atomic_load_explicit(&_qx->head, memory_order_relaxed)) { \
            QUEUE_DEQUEUE(_qx, _qx_item); \
        } \
        (void)_qx_item; \
        const size_t size = QUEUE_SIZE(_qx); \
        if (size == 0) { \
            if (_qx) { \
                memset(_qx, 0, sizeof(queue_t)); \
                _qx = NULL; \
            } \
        } \
    } \
} while(0)

extern queue_t queue;

/* ============================================================================
 * LINKED LIST (Thread-Safe Doubly-Linked)
 * ============================================================================ */

/*typedef struct list_node_t {
    void* data;
    struct list_node_t* next;
    struct list_node_t* prev;
} list_node_t;

typedef struct list_t {
    list_node_t* head;
    list_node_t* tail;
    threads_t*   pool;
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
} while(0)*/

/* ============================================================================
 * BINARY SEARCH TREE (Thread-Safe)
 * ============================================================================ */

/*typedef struct bst_node_t {
    int key;
    void* data;
    struct bst_node_t* left;
    struct bst_node_t* right;
} bst_node_t;

typedef struct bst_t {
    bst_node_t* root;
    threads_t*  pool;
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
} while(0)*/

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

/*typedef struct hash_entry_t {
    uint32_t key;
    void* value;
    struct hash_entry_t* next;
} hash_entry_t;

typedef struct hash_table_t {
    hash_entry_t* buckets[HASH_BUCKETS];
    threads_t*    pool;
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
} while(0)*/

/* ============================================================================
 * RING BUFFER (Lock-Free SPSC - Single Producer Single Consumer)
 * ============================================================================ */

#define RING_SIZE 1024

/*typedef struct ring_buffer_t {
    void* data[RING_SIZE];
    threads_t* pool;
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
} while(0)*/

#endif /* STRUCTURES_H */