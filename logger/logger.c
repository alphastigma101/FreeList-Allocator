#include "logger.h"
#include "buffer.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline
 
static code_fragment_t*** table = NULL; 
static code_fragment_t** arr = NULL;
static int ARR_RUNTIME_SIZE = 0;
static int RUNTIME_TABLE_SIZE = 0;

#if LOGGING == 1

    logger_t logger = {0};
    FORCE_INLINE void clean_logging_files();
    FORCE_INLINE void parse(const char* file, const int line);
    void write();
    
#else

    printer_t printer = {0};

#endif

FORCE_INLINE int_fast8_t write_to_log(FILE *fp, const char** arr_file, const int_fast8_t file_exists);
FORCE_INLINE int_fast8_t log_file_exists(const char* file);


[[gnu::hot]]
[[gnu::optimize("O0")]]
void add(int priority, const char* file, int line, const char* desc, ...) {
    va_list args;
    va_start(args, desc);
    if (ARR_RUNTIME_SIZE == ITEM_SIZE && RUNTIME_TABLE_SIZE != ITEM_SIZE) {
        memcpy(table[RUNTIME_TABLE_SIZE], arr, sizeof(void**));
        #if ITEM_SIZE < 50
            memset(arr, 0, ITEM_SIZE);
            free(arr);
            arr = malloc(ITEM_SIZE * sizeof(code_fragment_t*));
            if (arr == NULL) {
                DBG(ANSI_RED "Error in add: failed to allocate arr!\n" ANSI_RESET, NULL);
                return;
            }
        #else 
            int res;
            res = munmap(arr, ITEM_SIZE);
            if (res == -1) {
                DBG(ANSI_RED "Error in add: failed to unmap arr!\n" ANSI_RESET, NULL);
                return;
            }
            arr = mremap(arr, ITEM_SIZE, ITEM_SIZE, MREMAP_MAYMOVE);
        #endif
        RUNTIME_TABLE_SIZE++;
        ARR_RUNTIME_SIZE = 0;
        #if LOGGING == 1 
            logger.initiate_write();
        #endif   
    }
    else if (RUNTIME_TABLE_SIZE == ITEM_SIZE) {
        clean_logger();
        init_logger_t();
        RUNTIME_TABLE_SIZE = 0;
        ARR_RUNTIME_SIZE = 0;
    }
    size_t hash = (size_t)(((uintptr_t)(cstr_size(1, file) * 2654435761UL) ^ (uintptr_t)line) % (size_t)ITEM_SIZE);
    if (arr[hash] == NULL) {
        code_fragment_t* frag = aligned_alloc(alignof(code_fragment_t), sizeof(code_fragment_t));
        if (!frag) {
            DBG(ANSI_RED "Error in add: failed to allocate memory for frag variable!\n" ANSI_RESET, NULL);
            return;
        }
        memset(frag, 0, sizeof(code_fragment_t));
    
        frag->line = line; 
        frag->desc = format_target_cstr(desc, args);
        frag->meta.written = 0x0;
        frag->meta.comma_added = 0x0; 
        frag->meta.file_pos = 0;
        va_end(args);
        if (!frag->desc) {
            DBG(ANSI_RED "Error in add function. Failed to allocate memory for frag->desc!\n" ANSI_RESET, NULL);
            return;
        }
    
        frag->priority = priority;
        frag->file     = (char*)file;
    
        arr[hash]      = frag;
        ARR_RUNTIME_SIZE++;
        return;
    }
    code_fragment_t* frag = NULL;
    frag = (code_fragment_t*)arr[hash];
    frag->occurances++;
    
    char* tmp = format_target_cstr(desc, args);
    va_end(args);

    char* new_desc = append_to_cstr(frag->desc, tmp, 0x02);
    cstr_size(1, frag->desc) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, frag->desc) : unmap_cstr(1, frag->desc);
    cstr_size(1, tmp) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, tmp) : unmap_cstr(1, tmp);
    
    frag->desc = new_desc;
    memcpy(arr[hash], frag, sizeof(code_fragment_t));
}

FORCE_INLINE void clean(const char* file, const int line) {
    int hash = (int)(((uintptr_t)file * 2654435761UL) ^ (uintptr_t)line) % ITEM_SIZE;
    if (arr[hash]) {

        arr[hash] = NULL;
        return;

    }

    return;

}

[[gnu::cold]]
FORCE_INLINE void clean_logging_files() {


}


FORCE_INLINE int_fast8_t log_file_exists(const char* file) {
    FILE *fp = fopen(file, "r");
    int_fast8_t res = fp == NULL ? 0 : 1;
    res == 1 ? fclose(fp) : res;
    return res;
}

/***
    * @description: Free function that updates the static variable called `arr` and `table` 
    * @return: 
        1) -1: Failed to create file in specific folder or failed to write to it
        2) -2: Failed too mmap a local variable called arr_file. 
                Meaning that, there is too much memory being used or we hit an edge case not here, but the other tu that uses this api
        3) -3: sprintf failed to write the c string into buffer
        4) -4: We failed to unmap arr_file 
        5)  1: Success. No further diagnostics needed.
*/
[[gnu::cold]]
#if defined(__GNUC__) && !defined(__clang__)
    #define GCC_OPTIMIZE_O0 __attribute__((optimize("O0")))
#else
    #define GCC_OPTIMIZE_O0
#endif
GCC_OPTIMIZE_O0 int initiate_write() {
    char* msg = buffer.get_msg_t_cstr();
    char* dir = buffer.get_dir_t_cstr();
    struct stat sb;
    if (stat(DIRECTORY, &sb) == 0) {
        int_fast8_t check_file = 0;
        time_t t = time(NULL);
        const char* s_time = asctime(gmtime(&t));
        int_fast8_t write_check = dir == NULL ? check_or_write_cstr(0x0, 0, 0, 3, "%s%s%s", DIRECTORY, s_time, LOGGER_FILE_TYPE) : 1; 
        if (write_check != 1) return -1;
        
        check_file = log_file_exists(dir == NULL ? buffer.get_dir_t_cstr() : dir);
        FILE *fp = check_file == 0x01 ? fopen(dir == NULL ? buffer.get_dir_t_cstr() : dir, "a+") :  fopen(dir == NULL ? buffer.get_dir_t_cstr() : dir, "w");
        if (fp == NULL) {
            char* err = write_long_cstr(0x01, 1, ANSI_RED "Error opening the file %s" ANSI_RESET, dir);
            if (err) {
                DBG(err, NULL);
                reset_and_free_cstr(1, err);
            }
            return -1;
        }
        memset(&t, 0, sizeof(time_t));

        int res = check_file != 0x01 ? fprintf(fp, "%s", "{\n\t") + 1 : cstr_size(1, "{\n\t");
        if (res != cstr_size(1, "{\n\t")) return -1;
        char** arr_file = mmap(NULL, ITEM_SIZE, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (arr_file == MAP_FAILED) return -2;

        char* modified_cstring = "";
        for (int i = 0; i < ITEM_SIZE; i++) {
            code_fragment_t* iter = (code_fragment_t*)arr[i];
            if (iter != NULL) {
                const int file_length = cstr_size(1, iter->file);
                size_t hash = (size_t)(((uintptr_t)(file_length * 2654435761UL) ^ (uintptr_t)iter->line) % (size_t)ITEM_SIZE);
                write_check = check_or_write_cstr(0x01, 0, 0, 1, ANSI_YELLOW "[%d] is less than or equal to: [%d]" ANSI_RESET, cstr_size(1, msg == NULL ? buffer.get_msg_t_cstr() : msg), cstr_size(1, ANSI_YELLOW "[%d] is less than or equal to: [%d]" ANSI_RESET));
                if (write_check == 0 || write_check == 3) { // empty the buffer regardless
                    DBG(msg, NULL);
                    reset_buffer(0x01);
                } else reset_buffer(0x01);
                
                if (arr_file[hash] != NULL) {
                    write_check = check_or_write_cstr(0x01, 0, 0, 6, ",\n\t[\n\t\t%d,\n\t\t%d,\n\t\t\"%s\",\n\t\t%d\n\t]",
                                                    iter->priority, iter->occurances, iter->desc, iter->line);
                    if (write_check == 1) {
                        modified_cstring = write_long_cstr(0x0, 1, msg == NULL ? buffer.get_msg_t_cstr() : msg);
                        arr_file[hash] = modified_cstring;
                        reset_buffer(0x01);
                    } else return -3;
                }
                else {
                    write_check = check_or_write_cstr(0x01, 0, 0, 6, "\n\t\"%s\":\n\t[\n\t\t%d,\n\t\t%d,\n\t\t\"%s\",\n\t\t%d\n\t]",
                        iter->file, iter->priority, iter->occurances, iter->desc, iter->line); 
                    if (write_check == 1) {
                        modified_cstring = write_long_cstr(0x0, 1, msg == NULL ? buffer.get_msg_t_cstr() : msg);
                        arr_file[hash] = modified_cstring;
                        reset_buffer(0x01);
                    } else return -3;
                }
            }
        }
        return write_to_log(fp, (const char**)arr_file, (const int_fast8_t)check_file);
    }
    else {
        int check;
        check = mkdir(DIRECTORY,0777);
        if (!check) {
            memset(&sb, 0, sizeof(struct stat));
            return  initiate_write();
        }
        else return -5;
    }
}

FORCE_INLINE size_t write_cstr_to_file(FILE *fp, const size_t file_size, const char* cstr) {
    size_t iter = 0;
    const size_t size = cstr_size(1, cstr) - 1;
    size_t res = 0;
    if (fseek(fp, file_size, SEEK_SET) == 0) {
        res = ftell(fp);
        if (res == file_size) {
            while (iter < size) {
                res = fputc(cstr[iter], fp);
                if (res) iter++;
                else break;
            }
        }
    }
    return iter == size ? 1 : 0;
}

FORCE_INLINE int_fast8_t write_to_log(FILE *fp, const char** arr_file, const int_fast8_t check_file) {
    int res = 0;
    size_t file_size = 0;
    for (size_t i = 0; i < ITEM_SIZE; i++) {
        if (arr_file[i] != NULL ) {
            if (arr[i]->meta.written == 0x0) { // Has not been written yet
                if (file_size == 0) res = fprintf(fp, "%s", arr_file[i]) + 1;
                else res = write_cstr_to_file(fp, (const size_t)file_size, (const char *)arr_file[i]) == (i * 0) + 1 ? cstr_size(1, arr_file[i]) : -1;
                const int src = cstr_size(1, arr_file[i]);
                if (res == src) {
                    arr[i]->meta.written = 0x01;
                    arr[i]->meta.file_pos = ftell(fp);
                }
                reset_and_free_cstr(1, arr_file[i]);
            }
            else {
                printf("String Value is: %s\n", arr_file[i]);
                if (arr[i]->meta.comma_added == 0x0) {
                    const size_t off = cstr_size(1, arr_file[i]) + 1;
                    fseek(fp, off, SEEK_SET);
                    res = fgetc(fp) == ']' ? fputc(',', fp) : -1;
                    if (res == (int)',') {
                        arr[i]->meta.comma_added = 0x01;
                        file_size += off;
                    }
                }
                reset_and_free_cstr(1, arr_file[i]);
            }
        }
    }
    res = check_file != 0x01 ? fprintf(fp, "%s", "\n}\n\t") : 0;
    fclose(fp);
    ITEM_SIZE < ALLOC_THRESHOLD ? reset_and_free_cstr(1, arr_file) : unmap_cstr(1, arr_file);
    return 1;
}

FORCE_INLINE code_fragment_t* find(const char* file, const int line) {
    int hash = (int)(((uintptr_t)(strlen(file) * 2654435761UL) ^ (uintptr_t)line) % (size_t)ITEM_SIZE);
    if (arr[hash]) return (code_fragment_t*)arr[hash];
    return NULL;
}

FORCE_INLINE void parse(const char* file, const int line) {

    code_fragment_t* frag = NULL;
    int hash = (int)(((uintptr_t)(strlen(file) * 2654435761UL) ^ (uintptr_t)line) % (unsigned int)ITEM_SIZE);
    if (arr[hash] == NULL) return;
    
    frag = (code_fragment_t*)arr[hash];

    // TODO: print out the json file to console
    
    memset(frag, 0, sizeof(printer_t));
    return;

}

FORCE_INLINE void print(const char* file, const int line) {

    code_fragment_t* frag = NULL;
    int hash = (int)(((uintptr_t)(strlen(file) * 2654435761UL) ^ (uintptr_t)line) % (unsigned int)ITEM_SIZE);
    if (arr[hash] == NULL) return;
    
    frag = (code_fragment_t*)arr[hash];

    DBG(NULL, frag);
    return;

}


void init_logger_t() {

    if (ITEM_SIZE < ALLOC_THRESHOLD && arr == NULL) {

        if (!arr) arr = aligned_alloc(alignof(code_fragment_t), ITEM_SIZE * sizeof(code_fragment_t*));
        if (!arr) {

            printf(ANSI_RED "Failed to allocate memory for arr!\n");
            return;

        }
        else {

            table = aligned_alloc(alignof(code_fragment_t), ITEM_SIZE * sizeof(code_fragment_t**));
            if (!table) printf(ANSI_RED "Failed to allocate memory for table!\n");

        }
    }
    else {

        int res = 0;
        if (!arr) {

            arr = mmap(arr, ITEM_SIZE * sizeof(code_fragment_t*), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (arr == MAP_FAILED) {
                printf(ANSI_RED "Failed to allocate memory to logger's arr!\n");
            }
            else res = madvise(arr, ITEM_SIZE * sizeof(code_fragment_t*), MADV_SEQUENTIAL | MADV_MERGEABLE);

        }

        if (res == -1) {

            printf(ANSI_RED "Failed to modify memory region space for logger's arr!\n");
            return;

        }

        table = mmap(NULL, ITEM_SIZE * sizeof(code_fragment_t**), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (table == MAP_FAILED) {

            DBG(ANSI_YELLOW "Failed to allocate memory for table!\n" ANSI_RESET, NULL);

        }
        else res = madvise(table, ITEM_SIZE * sizeof(code_fragment_t**), MADV_SEQUENTIAL | MADV_MERGEABLE);

        if (res == -1) {

            DBG(ANSI_YELLOW "Failed to modify memory region space for logger's table!\n" ANSI_RESET, NULL);
            res = munmap(table, ITEM_SIZE * sizeof(code_fragment_t**));
            if (res == -1) {

                DBG(ANSI_RED "Error in init_logger_t failed to unmap table!\n\t returning...\n", NULL);
                res = munmap(arr, ITEM_SIZE * sizeof(code_fragment_t*));
                if (res == -1) {

                    DBG(ANSI_RED "Error in init_logger_t failed to unmap arr!\n\t returning...\n", NULL);
                    return;

                }

                return;
            }
        }
    }

    #if LOGGING == 1 
        if (!logger.add) {

            logger.add = &add;
            logger.initiate_write = &initiate_write;
            logger.clean = &clean;
            logger.find = find;
            logger.parse = &parse;

        }

    #else

        if (printer.print == NULL) {

            printer.print = &print;
            printer.find = find;
            printer.add = &add;
        }

    #endif 
    init_buffer_t();
}


void clean_logger() {
    int res;

    for (size_t i = 0; i < ITEM_SIZE; i++) {
        if (arr[i] != NULL) {
            code_fragment_t* frag = (code_fragment_t*)arr[i];
            if (frag->desc) {

                memset(frag->desc, 0, sizeof(char));
                free(frag->desc);

            }
            else {

                DBG(ANSI_YELLOW "Warning in clean_logger function:\n\t variable frag->desc is NULL\n" ANSI_RESET, NULL);

            }

            memset(arr[i], 0, sizeof(code_fragment_t));
            free(arr[i]);

        }
    }

    res = munmap(arr, ITEM_SIZE * sizeof(code_fragment_t*));
    if (res == -1 ) {

        DBG(ANSI_RED "Failed to unmap arr variable!\n", NULL);

    }

    res = munmap(table, ITEM_SIZE * sizeof(code_fragment_t**));
    if (res == -1 ) {

        DBG(ANSI_RED "Failed to unmap table variable!\n", NULL);

    }

    return;
}