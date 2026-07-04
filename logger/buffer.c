#include "buffer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

//#include "buffer.h"
//#include <stdlib.h>
//#include <sys/types.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline

struct __msg {
    uint8_t flag : 1;
    char* str;
    size_t size;
};

struct __dir {
    uint8_t flag : 1;
    char* str;
    size_t size;
};

struct __msg msg = {0};
struct __dir dir = {0};

[[gnu::hot]]
FORCE_INLINE char* get_msg_t_cstr() { return msg.str; }
[[gnu::hot]]
FORCE_INLINE char* get_dir_t_cstr() { return dir.str; }

buffer_t buffer = {0};
FORCE_INLINE int_fast8_t buffer_t_resize(const size_t size, uint8_t mode); 
FORCE_INLINE int_fast8_t create_buffer_t_msg(const size_t size);
FORCE_INLINE int_fast8_t create_buffer_t_dir(const size_t size);
FORCE_INLINE char* resize_cstr(char* cstr, const size_t target_size, const size_t src_size);

inline void init_buffer_t() {
    buffer.get_msg_t_cstr = &get_msg_t_cstr;
    buffer.get_dir_t_cstr = &get_dir_t_cstr;
    return;
}

[[gnu::hot]]
inline char* append_to_cstr(char* cstrt, char* cstrs, const uint8_t mode) {
    char* res = NULL;
    if (mode == 0x0) {
        const size_t size = cstr_size(1, cstrt) +  cstr_size(1, cstrs);
        res = strcat(cstrt, cstrs);
        res[size - 1] = '\0'; 
        return res;
    }
    else if (mode == 0x01 || mode == 0x02) {
        const size_t a = cstr_size(2, "\",\n\t\t\"", mode == 0x01 ? cstrt : cstrs);
        const size_t b = cstr_size(1, mode == 0x01 ? cstrs : cstrt);
        if (b < a + b) res = resize_cstr(mode == 0x01 ? cstrs : cstrt, b, a);
        res = strcat(res, "\",\n\t\t\"");
        res = strcat(res, mode == 0x01 ? cstrt : cstrs);
        return res;
    }
    return NULL;
}

[[gnu::hot]]
FORCE_INLINE char* resize_cstr(char* cstr, const size_t target_size, const size_t src_size) {
    const size_t total = src_size + target_size;
    const size_t old_len = cstr_size(1, cstr);
    char* res = NULL;
    res = total < ALLOC_THRESHOLD ? calloc(total, 1) : mmap(NULL, total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (!res) return NULL;

    memcpy(res, cstr, old_len);
    res[total - 1] = '\0';
    return res;
}

[[gnu::hot]]
FORCE_INLINE int_fast8_t create_buffer_t_dir(const size_t size) {
    const size_t new_size = dir.size + size;
    if (dir.str == NULL) {
        if (size < ALLOC_THRESHOLD) {
            dir.flag = 0x0;
            dir.str = calloc(size, 1);
            if (!dir.str) { return -1; }
        }
        else {
            int res;
            dir.flag = 0x01;
            dir.str = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (dir.str == MAP_FAILED) { return -2; }
            res = madvise(msg.str, new_size, MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                res = munmap(msg.str, size);
                return -3;
            }
        }
        memcpy(&dir.size, &new_size, sizeof(size_t));
    }
    if (size > dir.size) return buffer_t_resize(new_size, 0x0);
    return 1;
}

[[gnu::hot]]
FORCE_INLINE int_fast8_t create_buffer_t_msg(const size_t size) {
    const size_t new_size = msg.size + size;
    if (msg.str == NULL) {
        if (size < ALLOC_THRESHOLD) {
            msg.flag = 0x0;
            msg.str = calloc(size, 1);
            if (!msg.str) return -1;
        }
        else {
            int res;
            msg.flag = 0x01;
            msg.str = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (msg.str == MAP_FAILED) return -2;

            res = madvise(msg.str, new_size, MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                res = munmap(msg.str, size);
                return -2;
            }
        }
        memcpy(&msg.size, &new_size, sizeof(size_t));
    }
    if (size > msg.size) return buffer_t_resize(new_size, 0x01);
    return 1;
}

/***
    * @description: Free function that resets the bytes 
    * @param mode: 0x01 will reset buffer.msg.str and 0x0 will reset buffer.dir.str
*/
[[gnu::hot]]
inline void reset_buffer(const uint8_t mode) {
    switch (mode) {
        case 0x0:
            memset(dir.str, 0, dir.size - 1);
            break;

        case 0x01:
            memset(msg.str, 0, msg.size - 1);
            break;

        case 0x02:

            memset(dir.str, 0, dir.size - 1);
            memset(msg.str, 0, msg.size - 1);
            break;

        default: return;
    }
}


// 0x0 and 0x04 is for dir.str
// 0x02 and 0x03 is for msg.str
// 0x05 for failure
[[gnu::hot]]
FORCE_INLINE int_fast8_t buffer_t_resize(const size_t size, uint8_t mode) {
    if (mode == 0x01 || mode == 0x0) {
        int res = 0;
        if (size < ALLOC_THRESHOLD) {
            if (mode == 0x01 && msg.flag == 0x01) msg.flag = 0x0;
            else if (mode == 0x0 && dir.flag == 0x01) dir.flag = 0x0;
            char* tmp = realloc(mode == 0x01 ? msg.str : dir.str, size);
            if (tmp) {
                memset(tmp, 0, size);
                tmp[size - 1] = '\0';
                if (mode == 0x01) msg.str = tmp;
                else if (mode == 0x0) dir.str = tmp;
            }
            else {
                // Attempt to allocate directly instead of resizing
                reset_and_free_cstr(1, mode == 0x01 ? msg.str : dir.str);
                if (mode == 0x01 && (!tmp)) {
                    tmp = calloc(msg.size, 1);
                    msg.str = tmp;
                }
                else if (mode == 0x0 && (!tmp)) {
                    tmp = calloc(dir.size, 1);
                    dir.str = tmp;
                }
                if (mode == 0x01 && (!tmp)) return 0x03;
                else if (mode == 0x0 && (!tmp)) return 0x0; 
            }
            memcpy(mode == 0x01 ? &msg.size : &dir.size, &size, sizeof(size_t));
            return 0x01;
        }
        else {
            if (msg.flag == 0x0 || dir.flag == 0x0) {
                if (mode == 0x01) { 
                    msg.flag = 0x01;
                    if (msg.str && (msg.size < ALLOC_THRESHOLD)) reset_and_free_cstr(1, msg.str);
                    msg.str = msg.size < ALLOC_THRESHOLD ? mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0) : 
                        mremap(msg.str, msg.size, size, MREMAP_MAYMOVE);
                    if (msg.str == MAP_FAILED) return 0x02;
                }
                else {
                    dir.flag = 0x01;
                    if (!dir.str && dir.size < ALLOC_THRESHOLD) reset_and_free_cstr(1, dir.str);
                    dir.str = dir.size < ALLOC_THRESHOLD ? mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0) : 
                        mremap(dir.str, dir.size, size, MREMAP_MAYMOVE);
                    if (dir.str == MAP_FAILED) return 0x04;
                }
                const size_t target_size = mode == 0x01 ? msg.size : dir.size;
                if (target_size < ALLOC_THRESHOLD) {
                    res = madvise(mode == 0x01 ? msg.str : dir.str, size, MADV_SEQUENTIAL | MADV_MERGEABLE);
                    if (res == -1) {
                        res = munmap(mode == 0x01 ? msg.str : dir.str, size);
                        return mode == 0x01 ? 0x02 : 0x04;
                    }
                }
                memcpy(mode == 0x01 ? &msg.size : &dir.size, &size, sizeof(size_t));
                return 0x01;
            }
        }
    }
    return 0x05;
}

[[gnu::hot]]
inline char* create_cstr(size_t size) { return size < ALLOC_THRESHOLD ? calloc(size, 1) : mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0); }

[[gnu::hot]]
inline int cstr_size(const int length, ...) { 
    va_list args;
    va_start(args, length);
    int size = 0;
    for (int i = 0; i < length && length > 0; i++) { 
        const char* val = va_arg(args, const char *);
        if (val) size += strlen(val) + 1; // + 1 for the null terminator 
    }
    va_end(args);
    return size;
}

/***
    * @description: Free function that performs a comparison check of size, and can write to msg_t or dir_t 
    * @param c1: c1 is abbreviated as cstring one, so pass in the representation of size of it 
    * @param c2: c2 is abbreviated as cstring two, we compare it heavily against c1
    * @param length: if length is greater than 0, then caller is using the logger api 
    * @note: If you are using the logger api, then the first arg will be the cstring with the desired speicifers,
                followed by whatever the user wants to render into the cstring. 
    * @return:
        Will return the following integers:

        1) 0 castrophic failure. An underflow or an overflow has occured and this is will be on the caller side.
            No diagnostic needed from here.
        
        2) 1 c1 > c2. This is usually what you want to be safe. This means c1 capacity can hold c2 and more 
        
        3) 2 c1 == c2. It is an exact fit. No extra bytes/units. It is an exact fit.

        4) 3 c1 < c2. This could be potentionally bad. If c1 is less than c2, c1 cannot hold c2.    
*/
[[gnu::hot]]           
inline int_fast8_t check_or_write_cstr(const uint8_t mode, const size_t c1, const size_t c2, const int length, ...) {
    if (length > 0) {
        if (mode == 0x01 || mode == 0x0) {
            va_list args;
            va_start(args, length);
            const char* fmt = va_arg(args, const char*);
            va_list args_copy;
            va_copy(args_copy, args);
            size_t res;
            if (msg.str || dir.str) {
                int_fast8_t check = 0;
                const size_t size = vsnprintf(NULL, 0, fmt, args_copy) + 1;
                va_end(args_copy);
                if (mode == 0x01 && size > msg.size) check = buffer_t_resize(size, 0x01);
                else if (mode == 0x0 && size > dir.size) check = buffer_t_resize(size, 0x0);
                check = mode == 0x01 && (msg.size >= size) ? 0x01 : mode == 0x0 && (dir.size >= size) ? 0x01 : 0x0; 
                res = check == 0x01 ? vsnprintf(mode == 0x01 ? msg.str : dir.str, mode == 0x01 ? msg.size : dir.size, fmt, args) : -1;
                va_end(args);
                return (res + 1) == size ? 0x01 : 0x0;
            }
            else {
                const size_t size = vsnprintf(NULL, 0, fmt, args_copy) + 1;
                va_end(args_copy);
                mode == 0x01 ? create_buffer_t_msg(size) : create_buffer_t_dir(size);
                res = vsnprintf(mode == 0x01 ? msg.str : dir.str,
                                mode == 0x01 ? msg.size : dir.size,
                                fmt, args);
                va_end(args);
                return (res + 1) == size ? 0x01 : 0x0;
            }
            return 1;
        }
    }
    return c1 > c2 ? 0x01 : c1 == c2 ? 0x02 : c1 < c2 ? 0x03 : 0x0;
}

/***
    * @description: Free function that concats a series of small c strings into a large c string
    * @param mode: `0x0` will allocate memory on the heap while `0x01` will call in format_target_cstring function
    * @param length: The amount of strings that are being passed in 
    * @return: return an allocated string from the heap to the caller  
*/
[[gnu::hot]]
inline char* write_long_cstr(const uint8_t mode, const int length, ...) {
    va_list args;
    va_start(args, length);
    size_t size = 1;

    if (mode == 0x01) {
        const char* fmt = va_arg(args, const char*);
        va_list args_copy;
        va_copy(args_copy, args);
        char* res = format_target_cstr(fmt, args_copy);
        va_end(args);
        return res;
    }
    else if (mode == 0x0) {
        for (int i = 0; i < length; i++) {
            const char* val = va_arg(args, const char*);
            if (val) size += strlen(val) + 1;
        }
        va_end(args);
        char* res = size < ALLOC_THRESHOLD ? calloc(size, 1) : mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (!res) return NULL;
        res[size - 1] = '\0';
        va_start(args, length);
        char* cursor = res;
        for (int i = 0; i < length && length > 0; i++) {
            const char* val = va_arg(args, const char*);
            if (val) cursor = stpcpy(cursor, val);
        }
        va_end(args);
        return res; 
    }
    return NULL;
}

[[gnu::hot]]
inline void reset_and_free_cstr(const int length, ...) {
    va_list args;
    va_start(args, length);
    for (int i = 0; i < length && length > 0; i++) {
        uint8_t* val = va_arg(args, uint8_t*);
        if (val) {
            memset(val, 0, cstr_size(1, val));
            free(val);
        }
    }
    va_end(args);
    return;
}

[[gnu::hot]] 
inline void unmap_cstr(const int length, ...) {
    va_list args;
    va_start(args, length);
    for (int i = 0; i < length && length > 0; i++) {
        char* val = va_arg(args, char*);
        if (val) {
            if (munmap(val, cstr_size(1, val) != -1)) continue;
            else break;
        }
    }
    va_end(args);
    return;
}

/***
    * @desription: Function that takes a c string and render in whatever objects were passed to args
    * @param fmt: A c string that has specifiers needed to be render in. Expecting this kind of format from caller: "Added values of: [%d] + [%d] Result of a + b is %d\n", 
    * @param va_list: a series of objects that will be rendered into fmt.
    * @return: Returns the string but with the rendered objects 
*/
[[gnu::hot]]
inline char* format_target_cstr(const char* fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    size_t size = vsnprintf(NULL, 0, fmt, copy) + 1;
    va_end(copy);

    char* res = size < ALLOC_THRESHOLD ? calloc(size, 1) : mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (!res) return NULL;
    vsnprintf(res, size, fmt, args);
    return res;
}

[[gnu::destructor]]
FORCE_INLINE void dctor_buffer() {
    if (msg.str && msg.size < ALLOC_THRESHOLD) reset_and_free_cstr(1, msg.str);
    else unmap_cstr(1, msg.str);
    if (dir.str && dir.size < ALLOC_THRESHOLD) reset_and_free_cstr(1, dir.str);
    else unmap_cstr(1, dir.str);
    memset(&msg, 0, sizeof(buffer_t));
    memset(&dir, 0, sizeof(buffer_t));
}