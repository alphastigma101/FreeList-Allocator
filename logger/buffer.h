#ifndef _BUFFER_H_
#define _BUFFER_H_
#include <stddef.h>
#include <stdint.h>
#include <string.h> 
#include <stdarg.h> 
#include <stdio.h> 
#include <sys/types.h>
#define __USE_GNU 1
#include <sys/mman.h> 

/* Change the desired size, if string literal which gets compared is < ALLOC_THRESHOLD a variant of malloc is used */
/* Otherwise, mmap is used */
#ifndef ALLOC_THRESHOLD
    #define ALLOC_THRESHOLD 50
#endif


typedef struct buffer_t {
    char* (*get_msg_t_cstr)();
    char* (*get_dir_t_cstr)();
} buffer_t;

extern buffer_t buffer;
extern void init_buffer_t();
extern void reset_buffer(const uint8_t mode);
extern char* append_to_cstr(char* cstrt, char* cstrs, uint8_t mode);
extern char* create_cstr(size_t size);
extern size_t cstr_size(const int length, ...); 
extern int_fast8_t check_or_write_cstr(const uint8_t mode, const size_t c1, const size_t c2, const int length, ...);
extern char* write_long_cstr(const uint8_t mode, const int length, ...);
extern void reset_and_free_cstr(const int length, ...);
extern void unmap_cstr(const int length, ...);
extern char* format_target_cstr(const char* fmt, va_list args);  
#endif