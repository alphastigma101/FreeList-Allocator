#ifndef __BITMAP_H__
#define __BITMAP_H__
#include <stddef.h>

typedef struct bitmap_t {
    struct bitmap_t (*bitmap_clear)(struct bitmap_t bitmap, size_t idx, size_t start, size_t end);
    struct bitmap_t (*bitmap_set)(struct bitmap_t bitmap, size_t idx, size_t start, size_t end);
    size_t (*bitmap_test)(struct bitmap_t bitmap, size_t start, size_t end);
    unsigned char* bits;
    size_t   n_bytes; /* the size of the bit table */
} bitmap_t;


extern void init_bitmap_t(bitmap_t* bitmap, const size_t size);
extern void clean_bitmap(bitmap_t* bitmap);

#endif 