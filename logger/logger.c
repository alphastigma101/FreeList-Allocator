#include "logger.h"
#include "buffer.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

#define FORCE_INLINE __attribute__((always_inline)) static inline
#define OPTIMIZE_SIZE __attribute__((optimize("O0")))
 
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

            write();

        #endif
        
    }

    else if (RUNTIME_TABLE_SIZE == ITEM_SIZE) {
        clean_logger();
        init_logger_t();
        RUNTIME_TABLE_SIZE = 0;
        ARR_RUNTIME_SIZE = 0;
    }

    int hash = (int)(((uintptr_t)(strlen(file) * 2654435761UL) ^ (uintptr_t)line) % (unsigned int)ITEM_SIZE);

    if (arr[hash] == NULL) {
        code_fragment_t* frag = aligned_alloc(alignof(code_fragment_t), sizeof(code_fragment_t));
        if (!frag) {

            DBG(ANSI_RED "Error in add: failed to allocate memory for frag variable!\n" ANSI_RESET, NULL);
            return;
        }
        memset(frag, 0, sizeof(code_fragment_t));

        frag->line = line; 
        frag->desc = format_target_cstr(desc, args);

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

    //frag->desc =  append_to_cstr(frag->desc, format_target_cstr(desc, args));
    //va_end(args);
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

[[gnu::cold]]
[[gnu::optimize("O0")]]
void write() {
    //char* dir = NULL;
    struct stat sb;
    if (stat(DIRECTORY, &sb) == 0) {
        time_t t = time(NULL);
        const char* s_time = asctime(gmtime(&t));
        size_t target_size = cstr_size(3, DIRECTORY, s_time, LOGGER_FILE_TYPE); 

        if (!buffer.dir.str) {
            create_buffer_t_dir(target_size);
            char* tmp = write_long_cstr(0x0, 3, DIRECTORY, s_time, LOGGER_FILE_TYPE);
            //dir = append_to_cstr(tmp, NULL, 0x01); // TODO: Swapping to this function after everything works
            memcpy(buffer.dir.str, tmp, target_size);
            memset(tmp, 0, target_size);
            free(tmp);
        }

        FILE *fp = fopen(buffer.dir.str, "w");
        if (fp == NULL) {
            char* err = write_long_cstr(0x01, 1, ANSI_RED "Error opening the file %s" ANSI_RESET, buffer.dir.str);
            if (err) {
                DBG(err, NULL);
                memset(err, 0, 1);
                free(err);
            }
            return;
        }
        memset(&t, 0, sizeof(time_t));

        int res = fprintf(fp, "%s", "{\n\t");
        char** arr_file = mmap(NULL, ITEM_SIZE, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (arr_file == MAP_FAILED) {
            DBG(ANSI_RED "write: failed to allocate memory for arr_file!\n" ANSI_RESET, NULL);
            return;
        } 

        char* modified_cstring = "";
        for (size_t i = 0; i < ITEM_SIZE; i++) {
            code_fragment_t* iter = (code_fragment_t*)arr[i];
            if (iter != NULL) {
                const int file_length = cstr_size(1, iter->file);
                size_t hash = (size_t)(((uintptr_t)(file_length * 2654435761UL) ^ (uintptr_t)iter->line) % (size_t)ITEM_SIZE);

                target_size = cstr_size(1, "\n\t\"%s\":\n\t[\n\t\t%d,\n\t\t%d,\n\t\t\"%s\",\n\t\t%d\n\t]");
                create_buffer_t_msg(target_size); 
                const int_fast8_t size_check = check_cstr_len(0x01, buffer.msg.size, target_size, 1, ANSI_YELLOW "[%d] is less than or equal to: [%d]" ANSI_RESET, buffer.msg.size, target_size);
                if (size_check == 0) { // empty the buffer regardless
                    DBG(buffer.msg.str, NULL);
                    reset_buffer(0x01);
                } else reset_buffer(0x01);
                
                if (arr_file[hash] != NULL) {
                    
                    res = snprintf(buffer.msg.str, buffer.msg.size,
                                                    ",\n\t[\n\t\t%d,\n\t\t%d,\n\t\t\"%s\",\n\t\t%d\n\t]",
                                                    iter->priority, iter->occurances, iter->desc, iter->line);
                    if (res > 0) {
                        modified_cstring = write_long_cstr(0x0, 1, buffer.msg.str);
                        arr_file[hash] = modified_cstring;
                        reset_buffer(0x01);
                    } else {
                        DBG(ANSI_RED "Failed to copy data to arr_file!\n" ANSI_RESET, NULL);
                    }
                }
                else {
            
                    res = snprintf(buffer.msg.str, buffer.msg.size,
                                                    "\n\t\"%s\":\n\t[\n\t\t%d,\n\t\t%d,\n\t\t\"%s\",\n\t\t%d\n\t]",
                                                    iter->file, iter->priority, iter->occurances, iter->desc, iter->line);
                    if (res > 0) {
                        modified_cstring = write_long_cstr(0x0, 1, buffer.msg.str);
                        printf("String value is: %s\n", modified_cstring);
                        arr_file[hash] = modified_cstring;
                        reset_buffer(0x01);

                    } else {
                        DBG(ANSI_RED "Failed to copy data to arr_file!\n" ANSI_RESET, NULL);
                    }
                }
            }
        }
        for (size_t i = 0; i < ITEM_SIZE; i++)
            if (arr_file[i] != NULL) 
                res = fprintf(fp, "%s", arr_file[i]);
        res = fprintf(fp, "%s", "\n}\n\t");
        fclose(fp);

        memset(arr_file, 0, 1);
        //if (ALLOC_THRESHOLD < ITEM_SIZE) 
            //free(arr_file);
        //else 
            res = munmap(arr_file, ITEM_SIZE);
        if (res == -1) {

            DBG(ANSI_RED "Error: Failed to unmap arr_file!\n" ANSI_RESET, NULL); 
        }

        memset(&sb, 0, sizeof(struct stat));
        reset_buffer(0x01);
        return;

    }
    else {
        int check;
        check = mkdir(DIRECTORY,0777);
        if (!check) {
            memset(&sb, 0, sizeof(struct stat));
            return write();
        }
        else {
            DBG(ANSI_RED "Unable to create directory\n" ANSI_RESET, NULL);
            return;
        }
    }
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

    if (ITEM_SIZE < 50 && arr == NULL) {

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
            logger.write = &write;
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