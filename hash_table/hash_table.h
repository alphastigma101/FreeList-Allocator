#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

typedef struct byte_entries_t {
    unsigned char inuse;
    unsigned int bytes;
    void* ptr;
    struct offset_entries_t* offset;
    struct byte_entries_t* next;
} byte_entries_t;

typedef struct offset_entries_t {
    unsigned char inuse;
    unsigned int offset;
    void* ptr;
    struct byte_entries_t* bytes;
    struct offset_entries_t* next;
} offset_entries_t;

typedef struct hash_table_t {
    byte_entries_t**   byte_map;
    offset_entries_t** offset_map;
    unsigned int bucket_count;  /* always a power of two */
    unsigned int entry_count;   /* live entries, drives resize decisions */
} memory_address_hash_table_t;

extern memory_address_hash_table_t* set(memory_address_hash_table_t* table, const unsigned int offset, const unsigned int bytes, const unsigned char inuse, void* ptr);
extern memory_address_hash_table_t* update(memory_address_hash_table_t* table, const unsigned int offset, const unsigned int bytes, const unsigned char inuse);
extern memory_address_hash_table_t* destroy(memory_address_hash_table_t* table, const unsigned int offset, const unsigned int bytes);
extern void clean(memory_address_hash_table_t* table);
extern byte_entries_t* get_entry_t_by_bytes(memory_address_hash_table_t* table, const unsigned int bytes, const unsigned char inuse);
extern offset_entries_t* get_entry_t_by_offset(memory_address_hash_table_t* table, const unsigned int offset, const unsigned char inuse);

#endif