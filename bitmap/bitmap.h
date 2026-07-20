#ifndef __BITMAP_H__
#define __BITMAP_H__

typedef struct bitmap_t {
    unsigned char* bits;
    unsigned int   n_bytes; /* the size of the bit table */
    struct bitmap_t (*bitmap_clear)(struct bitmap_t bitmap, int idx, int start, int end);
    struct bitmap_t (*bitmap_set)(struct bitmap_t bitmap, int idx, int start, int end);
    int (*bitmap_test)(struct bitmap_t bitmap, int start, int end);
} bitmap_t;


extern bitmap_t init_bitmap_t(const int size);
extern void clean_bitmap(bitmap_t bitmap);

#endif 