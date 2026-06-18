#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

//#include "buffer.h"
//#include <stdlib.h>
//#include <sys/types.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline
#define OPTIMIZE_SIZE __attribute__((optimize("O0")))

// TODO: Move msg_t and dir_t structs here and make setters and getters in buffer field to get them 
buffer_t buffer = {0};

FORCE_INLINE void buffer_t_resize(const size_t size, uint8_t mode); 
FORCE_INLINE void create_buffer_t_msg(const size_t size);
FORCE_INLINE void create_buffer_t_dir(const size_t size);
FORCE_INLINE char* resize_cstr(char* cstr, const size_t target_size, const size_t src_size);


[[gnu::hot]]
inline extern char* append_to_cstr(char* cstrt, char* cstrs, const uint8_t mode) {
    char* res = NULL;
    if (mode == 0x0) {
        const size_t size = cstr_size(1, cstrt) +  cstr_size(1, cstrs);
        cstrt[cstr_size(1, cstrt) - 1] = ' ';
        res = strcat(cstrt, cstrs);
        res[size - 1] = '\0'; 
        return res;
    }
    else if (mode == 0x01 || mode == 0x02) {
        const size_t new_size = cstr_size(2, "\n\t\t", cstrt);
        const size_t src_size = cstr_size(1, cstrs);
        if (src_size < new_size) res = resize_cstr(cstrs, src_size, new_size);
        res = strcat(mode == 0x01 ? cstrs : cstrt, "\n\t\t");
        res = strcat(res, mode == 0x01 ? cstrt : cstrs);
        return res;
    }
    return NULL;
}

[[gnu::hot]]
char* resize_cstr(char* cstr, const size_t target_size, const size_t src_size) {
    const size_t total = src_size + target_size;
    char* res = NULL;
    if (src_size < ALLOC_THRESHOLD) {
        cstr = realloc(cstr, total);
        memset(cstr, 0, total);
        cstr[total - 1] = '\0';
        return cstr;
    }
    char* tmp = mremap(cstr, cstr_size(1, cstr), total, MREMAP_MAYMOVE);
    if (tmp == MAP_FAILED) { return NULL; }
    cstr[total - 1] = '\0';
    cstr = tmp;
    return  cstr;
}

[[gnu::hot]]
FORCE_INLINE void create_buffer_t_dir(const size_t size) {
    const size_t new_size = buffer.dir.size + size;
    if (buffer.dir.str == NULL) {
        if (size < ALLOC_THRESHOLD) {
            buffer.dir.flag = 0x0;
            buffer.dir.str = calloc(size, 1);
            if (!buffer.dir.str) {

                //DBG(ANSI_RED "init_buffer_t_dir: Failed to allocate memory for buffer.dir.str!\n" ANSI_RESET, NULL);
                return;
            }
        }
        else {

            int res;
            buffer.dir.flag = 0x01;
            buffer.dir.str = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (buffer.dir.str == MAP_FAILED) {

                //DBG(ANSI_RED "init_buffer_t_dir: Failed to mmap memory for buffer.dir.str!\n" ANSI_RESET, NULL);
                return;
            }
            res = madvise(buffer.msg.str, new_size, MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                res = munmap(buffer.msg.str, size);
                return;
            }
        }
        memcpy(&buffer.dir.size, &new_size, sizeof(size_t));
        return;
    }
    if (size > buffer.dir.size) buffer_t_resize(new_size, 0x0);
}

[[gnu::hot]]
FORCE_INLINE void create_buffer_t_msg(const size_t size) {
    const size_t new_size = buffer.msg.size + size;
    if (buffer.msg.str == NULL) {
        if (size < ALLOC_THRESHOLD) {
            buffer.msg.flag = 0x0;
            buffer.msg.str = calloc(size, 1);
            if (!buffer.msg.str) {

                //DBG(ANSI_RED "init_buffer_t_msg: Failed to allocate memory for buffer.msg.str!\n" ANSI_RESET, NULL);
                return;
            }
        }
        else {
            int res;
            buffer.msg.flag = 0x01;
            buffer.msg.str = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (buffer.msg.str == MAP_FAILED) {

                //DBG(ANSI_RED "init_buffer_t_msg: Failed to mmap memory for buffer.msg.str!\n" ANSI_RESET, NULL);
                return;
            }

            res = madvise(buffer.msg.str, new_size, MADV_SEQUENTIAL | MADV_MERGEABLE);
            if (res == -1) {
                res = munmap(buffer.msg.str, size);
                return;
            }
        }

        memcpy(&buffer.msg.size, &new_size, sizeof(size_t));
    }
    if (size > buffer.msg.size) buffer_t_resize(new_size, 0x01);

    return;
}

/***
    * @description: Free function that resets the bytes 
    * @param mode: 0x01 will reset buffer.msg.str and 0x0 will reset buffer.dir.str
*/
[[gnu::hot]]
void reset_buffer(const uint8_t mode) {
    switch (mode) {
        case 0x0:
            memset(buffer.dir.str, 0, buffer.dir.size - 1);
            break;

        case 0x01:
            memset(buffer.msg.str, 0, buffer.msg.size - 1);
            break;

        case 0x02:

            memset(buffer.dir.str, 0, buffer.dir.size - 1);
            memset(buffer.msg.str, 0, buffer.msg.size - 1);
            break;

        default: return;
    }
}

[[gnu::hot]]
FORCE_INLINE void buffer_t_resize(const size_t size, uint8_t mode) {
    if (mode == 0x01 || mode == 0x0) {
        int res = -1;
        if (size < ALLOC_THRESHOLD) {
            if (mode == 0x01) {
                buffer.msg.flag = 0x0;
                res = munmap(buffer.msg.str, buffer.msg.size);
                if (res == -1) return;
            }
            else {
                buffer.dir.flag = 0x0;
                res = munmap(buffer.dir.str, buffer.dir.size);
                if (res == -1) return;
            }
            char* tmp = realloc(mode == 0x01 ? buffer.msg.str : buffer.dir.str, size);
            if (tmp) {
                memset(tmp, 0, size);
                tmp[size - 1] = '\0';
                if (mode == 0x01) buffer.msg.str = tmp;
                else buffer.dir.str = tmp;
            }
            else { return; }
            memcpy(mode == 0x01 ? &buffer.msg.size : &buffer.dir.size, &size, sizeof(size_t));
            return;
        }
        else {
            if (buffer.msg.flag == 0x0 || buffer.dir.flag == 0x0) {
                memset(mode == 0x01 ? buffer.msg.str : buffer.dir.str, 0, mode == 0x01 ? buffer.msg.size : buffer.dir.size);
                free(mode == 0x01 ? buffer.msg.str : buffer.dir.str);
                if (mode == 0x01) { 
                    buffer.msg.flag = 0x01;
                    buffer.msg.str = mmap(buffer.msg.str, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                    if (buffer.msg.str == MAP_FAILED) return;
                }
                else {
                    buffer.dir.flag = 0x01;
                    buffer.dir.str = mmap(buffer.dir.str, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                    if (buffer.dir.str == MAP_FAILED) {
                        res = munmap(buffer.dir.str, buffer.dir.size);
                        return;
                    }
                }
                res = madvise(mode == 0x01 ? buffer.msg.str : buffer.dir.str, size, MADV_SEQUENTIAL | MADV_MERGEABLE);
                if (res == -1) {
                    res = munmap(mode == 0x01 ? buffer.msg.str : buffer.dir.str, size);
                    return;
                }
                memcpy(mode == 0x01 ? &buffer.msg.size : &buffer.dir.size, &size, sizeof(size_t));
                return;
            }
            char* tmp = mremap(mode == 0x01 ? buffer.msg.str : buffer.dir.str, mode == 0x01 ? buffer.msg.size : buffer.dir.size, size, MREMAP_MAYMOVE);
            if (tmp == MAP_FAILED) { return; }
            memcpy(mode == 0x01 ? &buffer.msg.size : &buffer.dir.size, &size, sizeof(size_t));
            if (mode == 0x01) {
                buffer.msg.str[size - 1] = '\0';
                buffer.msg.str = tmp;
            }
            else {
                buffer.dir.str[size - 1] = '\0';
                buffer.dir.str = tmp;
            }
        }
    }
    return;
}

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
            size_t res;
            if (buffer.msg.str || buffer.dir.str) {
                va_list args_copy;
                va_copy(args_copy, args);
                const size_t size = vsnprintf(NULL, 0, fmt, args_copy) + 1;
                va_end(args_copy);
                mode == 0x01 ?  size > buffer.msg.size ? buffer_t_resize(size, 0x01) : mode : size > buffer.dir.size ? buffer_t_resize(size, 0x0) : mode;
                res = vsnprintf(mode == 0x01 ? buffer.msg.str : buffer.dir.str, mode == 0x01 ? buffer.msg.size : buffer.dir.size, fmt, args);
                va_end(args);
                return (res + 1) == size ? 1 : 0;
            }
            else {
                va_list args_copy;
                va_copy(args_copy, args);
                const size_t size = vsnprintf(NULL, 0, fmt, args_copy) + 1;
                va_end(args_copy);
                mode == 0x01 ? create_buffer_t_msg(size) : create_buffer_t_dir(size);
                res = vsnprintf(mode == 0x01 ? buffer.msg.str : buffer.dir.str,
                                mode == 0x01 ? buffer.msg.size : buffer.dir.size,
                                fmt, args);
                va_end(args);
                return (res + 1) == size ? 1 : 0;
            }
            return 1;
        }
    }
    
    return c1 > c2 ? 1 : c1 == c2 ? 2 : c1 < c2 ? 3 : 0;
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
        char* res = format_target_cstr(fmt, args);
        va_end(args);
        return res;
    }
    else if (mode == 0x0) {

        for (int i = 0; i < length; i++) {
            const char* val = va_arg(args, const char*);
            if (val) size += strlen(val) + 1;
        }

        va_end(args);

        char* res = calloc(size, 1);
        if (!res) return NULL;

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

/***
    * @desription: Function that takes a c string and render in whatever objects were passed to args
    * @param fmt: A c string that has specifiers needed to be render in. Expecting this kind of format from caller: "Added values of: [%d] + [%d] Result of a + b is %d\n", 
    * @param va_list: a series of objects that will be rendered into fmt.
    * @return: Returns the string but with the rendered objects 
*/
inline char* format_target_cstr(const char* fmt, va_list args) {

    va_list copy;
    va_copy(copy, args);
    size_t size = vsnprintf(NULL, 0, fmt, copy) + 1;
    va_end(copy);

    char* res = calloc(size, 1);
    if (!res) return NULL;
    vsnprintf(res, size, fmt, args);
    return res;
}

//[[gnu::destructor(0)]]
FORCE_INLINE void dctor_buffer() {
    if (buffer.dir.size < ALLOC_THRESHOLD) {
        if (buffer.dir.str != NULL) {

            memset(buffer.dir.str, 0, buffer.dir.size - 1);
            free(buffer.dir.str);
        }
    }

    if (buffer.msg.size < ALLOC_THRESHOLD) {
        if (buffer.msg.str != NULL) {

            memset(buffer.msg.str, 0, buffer.msg.size - 1);
            free(buffer.msg.str);
        }
    }

    if (buffer.msg.size > ALLOC_THRESHOLD) {

        int res;
        res = munmap(buffer.dir.str, buffer.dir.size - 1);
        if (res == -1) {

            //DBG(ANSI_RED "dctor_buffer: Failed to unmap buffer.dir.str!\n" ANSI_RESET, NULL);

        }
        res = munmap(buffer.msg.str, buffer.msg.size - 1);
        if (res == -1) {

            //DBG(ANSI_RED "dctor_buffer: Failed to unmap buffer.msg.str!\n" ANSI_RESET, NULL);

        }
    }

    memset(&buffer, 0, sizeof(buffer_t));

}