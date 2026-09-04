#include "logger.h"
#include <stdalign.h>
#include <stddef.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>

//#include "logger.h"
//#include <stdalign.h>
//#include <sys/stat.h>
//#include <stdlib.h>
//#include <dirent.h>
#define FORCE_INLINE __attribute__((always_inline)) static inline
 
static code_fragment_t*** table = NULL; 
static code_fragment_t** arr = NULL;
static int ARR_RUNTIME_SIZE = 0;
static int RUNTIME_TABLE_SIZE = 0;
static size_t DATA_SIZE = 0;

#if LOGGING == 1
    logger_t logger = {0};
#else
    printer_t printer = {0};
#endif

// TODO: Wrap with the #if LOGGING == 1
char timestamp[26];
FORCE_INLINE int_fast8_t clean_logging_files(const char* cstr);
FORCE_INLINE int parse_timestamp(char* cstr);
FORCE_INLINE int initiate_write();
FORCE_INLINE int_fast8_t write_to_log(FILE *fp, const char** arr_file);
FORCE_INLINE void dctor_logger();

FORCE_INLINE int_fast8_t check_or_create_folder(const char* csrt) {
    struct stat sb;
    if (stat(csrt, &sb) == 0) return 0x01;
    int check;
    check = mkdir(csrt,0777);
    if (!check) memset(&sb, 0, sizeof(struct stat));
    return 0x0;
}

FORCE_INLINE int_fast8_t log_file_exists(const char* file) {
    FILE *fp = fopen(file, "r");
    int_fast8_t res = fp == NULL ? 0x0 : 0x01;
    res == 1 ? fclose(fp) : res;
    return res;
}

static void write_to_stream(FILE* fp, const int length, ...) {
    va_list args;
    va_start(args, length);
    for (int i = 0; i < length && length > 0; i++) { 
        const char* val = va_arg(args, const char *);
        if (val) fputs(val, fp);
    }
    va_end(args);
} 

[[gnu::hot]]
FORCE_INLINE const char* create_timestamp() {
    memset(timestamp, 0, (size_t)cstr_size(1, timestamp));
    struct timespec ts; 
    timespec_get(&ts, TIME_UTC); 
    strftime(timestamp, sizeof(timestamp), "%a %b %e %T %Y", localtime(&ts.tv_sec));
    memset(&ts, 0, sizeof(struct timespec));
    return timestamp;
}

FORCE_INLINE int parse_timestamp(char* cstr) {
    int hours = 0, minutes = 0, seconds = 0;
    const size_t size = (size_t)cstr_size(1, cstr);
    for (size_t i = 0; i < size; i++) {
        if (cstr[i] == ':') {
            hours = cstr[i - 1] + cstr[i - 2] * 3600;
            minutes = cstr[i + 1] + cstr[i + 2] * 60;
            seconds = cstr[i + 4] + cstr[i + 5];
            break;
        }
    }
    const int total_time = (hours + minutes + seconds) * 1000;
    return total_time >= CLEANER_TIME ? 1 : 0;   
}

FORCE_INLINE void append_to_table() {
    if (ARR_RUNTIME_SIZE == ITEM_SIZE && RUNTIME_TABLE_SIZE != ITEM_SIZE) {
        if (!table[RUNTIME_TABLE_SIZE]) 
            table[RUNTIME_TABLE_SIZE] = ITEM_SIZE < ALLOC_THRESHOLD ? aligned_alloc(alignof(code_fragment_t**), sizeof(code_fragment_t**)) : 
                mmap(NULL, ITEM_SIZE * sizeof(code_fragment_t**), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        memcpy(table[RUNTIME_TABLE_SIZE], arr, ITEM_SIZE * sizeof(code_fragment_t*));
        if (arr && ITEM_SIZE < ALLOC_THRESHOLD) reset_and_free_cstr(1, arr);
        else unmap_cstr(1, arr);
        if (!arr && ITEM_SIZE < ALLOC_THRESHOLD) arr = aligned_alloc(alignof(code_fragment_t*), ITEM_SIZE * sizeof(code_fragment_t*));
        else arr = mmap(NULL, ITEM_SIZE * sizeof(code_fragment_t*), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        RUNTIME_TABLE_SIZE++;
        ARR_RUNTIME_SIZE = 0;
        memset(arr, 0, ITEM_SIZE * sizeof(code_fragment_t*));
        #if LOGGING == 1
            if(buffer.get_dir_t_cstr()) initiate_write();
        #endif  
    }
    else if (RUNTIME_TABLE_SIZE == ITEM_SIZE) {
        for (size_t i = 0; i < ITEM_SIZE; i++) {
            if (arr[i]) {
                cstr_size(1, arr[i]->desc) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, arr[i]->desc) : unmap_cstr(1, arr[i]->desc);
                memset(arr[i], 0, sizeof(code_fragment_t));
                free(arr[i]);
            }
            if (table[i]) {
                code_fragment_t* frag = *table[i];
                cstr_size(1, frag->desc) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, frag->desc) : unmap_cstr(1, frag->desc);
                memset(frag, 0, sizeof(code_fragment_t));
                memset(table[i], 0, sizeof(code_fragment_t*));
                free(frag);
                free(table[i]);
            }

        }
        RUNTIME_TABLE_SIZE = 0;
        ARR_RUNTIME_SIZE = 0;
    }
}

[[gnu::hot]]
static void update_entry(size_t hash, const char* desc, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    
    arr[hash]->occurances++;
    char* tmp = format_target_cstr(desc, args);
    va_end(args);
    
    char* new_desc = append_to_cstr(arr[hash]->desc, tmp, 0x02);
    cstr_size(1, arr[hash]->desc) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, arr[hash]->desc) : unmap_cstr(1, arr[hash]->desc);
    cstr_size(1, tmp) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, tmp) : unmap_cstr(1, tmp);
    arr[hash]->desc = new_desc;
    return;
}

[[gnu::hot]]
[[gnu::optimize("O0")]]
static void add(int priority, const char* file, int line, const char* desc, ...) {
    va_list args;
    va_start(args, desc);

    if (ARR_RUNTIME_SIZE == ITEM_SIZE) append_to_table();
    
    size_t hash = (size_t)(((uintptr_t)((size_t)cstr_size(1, file) * 2654435761UL) ^ (uintptr_t)line) % (size_t)ITEM_SIZE);
    if (arr[hash] == NULL) {
        if (table[hash]) arr = table[hash];
        if (!arr[hash]) arr[hash] = aligned_alloc(alignof(code_fragment_t),   sizeof(code_fragment_t));
        else return update_entry(hash, desc, args);
        if (!arr[hash]) {
            DBG(ANSI_RED "Error in add: failed to allocate memory for frag variable!\n" ANSI_RESET, NULL);
            return;
        }
        memset(arr[hash], 0, sizeof(code_fragment_t));
        arr[hash]->line = line; 
        arr[hash]->desc = format_target_cstr(desc, args);
        va_end(args);
        if (!arr[hash]->desc) {
            DBG(ANSI_RED "Error in add function. Failed to allocate memory for frag->desc!\n" ANSI_RESET, NULL);
            return;
        }
        arr[hash]->priority = priority;
        arr[hash]->file     = (char*)file;
        ARR_RUNTIME_SIZE++;
        return;
    }
    return update_entry(hash, desc, args);
}

FORCE_INLINE void remove_entry(const char* file, const int line) {
    size_t hash = (size_t)(((uintptr_t)cstr_size(1, file) * 2654435761UL) ^ (uintptr_t)line) % (size_t)ITEM_SIZE;
    if (arr[hash]) {
        code_fragment_t* frag = arr[hash];
        reset_and_free_cstr(2, frag, frag->desc);
        return;
    }
    return;
}

[[gnu::cold]]
FORCE_INLINE int_fast8_t clean_logging_files(const char* cstr) {
    struct dirent *entry;
    DIR *dp = opendir(DIRECTORY);

    if (dp == NULL) {
        perror("opendir");
        return 0x0;
    }

    while ((entry = readdir(dp)) != NULL) {
        const char* entry_i = entry->d_name;
        if (cstr_size(1, entry_i) > 6) {
            char* res = write_long_cstr(0x0, 2, DIRECTORY, entry_i);
            if (strcmp(res, cstr) != 0) {
                const char* ext = &entry->d_name[cstr_size(1, entry->d_name) - 6];
                if (strcmp(ext, LOGGER_FILE_TYPE) == 0) {
                    char* full_path = write_long_cstr(0x0, 2, DIRECTORY, entry->d_name);
                    if (remove(full_path) == 0) {
                        cstr_size(1, full_path) < ALLOC_THRESHOLD
                            ? reset_and_free_cstr(1, full_path)
                            : unmap_cstr(1, full_path);
                        full_path = NULL;
                        cstr_size(1, res) < ALLOC_THRESHOLD
                            ? reset_and_free_cstr(1, res)
                            : unmap_cstr(1, res);
                        res = NULL;
                        continue;
                    }
                    else {
                        char* warning = write_long_cstr(0x0, 4, ANSI_YELLOW, "Failed to remove file: ", entry->d_name, ANSI_RESET);
                        DBG(warning, NULL);
                        cstr_size(1, warning) < ALLOC_THRESHOLD
                            ? reset_and_free_cstr(1, warning)
                            : unmap_cstr(1, warning);
                    }
                    if (full_path) {
                        cstr_size(1, full_path) < ALLOC_THRESHOLD
                            ? reset_and_free_cstr(1, full_path)
                            : unmap_cstr(1, full_path);
                        full_path = NULL;
                    }
                }
            }
            if (res) {
                cstr_size(1, res) < ALLOC_THRESHOLD
                    ? reset_and_free_cstr(1, res)
                    : unmap_cstr(1, res);
                res = NULL;
            }
        }
    }
    closedir(dp);
    return 0x01;
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
GCC_OPTIMIZE_O0 FORCE_INLINE int initiate_write() {
    if (check_or_create_folder(DIRECTORY) == 0x0) return -5;
    int_fast8_t check_file = 0;
    int_fast8_t write_check = buffer.get_dir_t_cstr() == NULL ? check_or_write_cstr(0x0, 0, 0, 3, "%s%s%s", DIRECTORY, create_timestamp(), LOGGER_FILE_TYPE) : 1; 
    if (write_check != 1) return -1;
    else if (parse_timestamp(timestamp) == 1) {
        if (clean_logging_files(buffer.get_dir_t_cstr()) != 0x01) DBG(ANSI_RED "Failed to clean directory!\n" ANSI_RESET, NULL);
    }
    check_file = log_file_exists(buffer.get_dir_t_cstr());
    FILE *fp = check_file == 0x01 ? fopen(buffer.get_dir_t_cstr(), "r+") : fopen(buffer.get_dir_t_cstr(), "w");
    if (fp == NULL) {
        char* err = write_long_cstr(0x01, 1, ANSI_RED "Error opening the file %s" ANSI_RESET, buffer.get_dir_t_cstr());
        if (err) {
            DBG(err, NULL);
            reset_and_free_cstr(1, err);
        }
        return -1;
    }

    char** arr_file = ITEM_SIZE < ALLOC_THRESHOLD ? calloc(ITEM_SIZE, 1) : mmap(NULL, ITEM_SIZE * sizeof(char*), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (!arr_file) return -2;

    char* modified_cstring = "";
    for (int i = 0; i < ITEM_SIZE; i++) {
        code_fragment_t* frag = arr[i] != NULL ? arr[i] : table[i] ? *table[i] : NULL;
        if (frag != NULL) {
            size_t hash = (size_t)(((uintptr_t)((size_t)cstr_size(1, frag->file) * 2654435761UL) ^ (uintptr_t)frag->line) % (size_t)ITEM_SIZE);
            write_check = check_or_write_cstr(0x01, 0, 0, 1, ANSI_YELLOW "[%d] is less than or equal to: [%d]" ANSI_RESET, cstr_size(1, buffer.get_msg_t_cstr() ), cstr_size(1, ANSI_YELLOW "[%d] is less than or equal to: [%d]" ANSI_RESET));
            if (write_check == 0x0 || write_check == 0x03) { // empty the buffer regardless
                DBG(buffer.get_msg_t_cstr(), NULL);
                reset_buffer(0x01);
            } else reset_buffer(0x01);
            
            write_check = check_or_write_cstr(0x01, 0, 0, 6, "\n\t\"%s\":\n\t[\n\t\t%d,\n\t\t%d,\n\t\t\"%s\",\n\t\t%d\n\t]",
                frag->file, frag->priority, frag->occurances, frag->desc, frag->line); 
            if (write_check == 1) {
                modified_cstring = write_long_cstr(0x0, 1, buffer.get_msg_t_cstr());
                arr_file[hash] = modified_cstring;
                DATA_SIZE += cstr_size(1, modified_cstring);
                reset_buffer(0x01);
            } else return -3;
    
        }
    }
    const int_fast8_t cllr = write_to_log(fp, (const char**)arr_file);
    ITEM_SIZE < ALLOC_THRESHOLD ? reset_and_free_cstr(1, arr_file) : unmap_cstr(1, arr_file);
    return cllr == 0x01 ? 1 : -1; 
}

FORCE_INLINE int_fast8_t write_to_log(FILE *fp, const char** arr_file) {
    int first_entry = 0;
    char* out = create_cstr((size_t)DATA_SIZE * 2);
    for (size_t i = 0; i < ITEM_SIZE; i++) {
        if (arr_file[i] != NULL) {
            if (first_entry == 0) {
                out = append_to_cstr(out, (char*)arr_file[i], 0x0);
                first_entry = 1;
            }
            else {
                out = append_to_cstr(out, ",", 0x0);
                out = append_to_cstr(out, (char*)arr_file[i], 0x0);
            }
            cstr_size(1, arr_file[i]) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, arr_file[i]) : unmap_cstr(1, arr_file[i]);
        }
    }
    write_to_stream(fp, 3, "{\n\t", out, "\n}\n\t");
    fclose(fp);
    cstr_size(1, out) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, out) : unmap_cstr(1, out);
    DATA_SIZE = 0;
    return 1;
}

FORCE_INLINE code_fragment_t* find(const char* file, const int line) {
    size_t hash = (size_t)(((uintptr_t)((size_t)cstr_size(1, file) * 2654435761UL) ^ (uintptr_t)line) % (size_t)ITEM_SIZE);
    if (arr[hash]) return (code_fragment_t*)arr[hash];
    return NULL;
}

FORCE_INLINE void print(const char* file, const int line) {
    size_t hash = (size_t)(((uintptr_t)((size_t)cstr_size(1,  file) * 2654435761UL) ^ (uintptr_t)line) % (size_t)ITEM_SIZE);
    if (arr[hash] == NULL) return;
    DBG(NULL, arr[hash]);
    return;
}

void init_logger_t() {
    init_buffer_t();
    if (ITEM_SIZE < ALLOC_THRESHOLD && arr == NULL) {
        if (!arr) arr = aligned_alloc(alignof(code_fragment_t*), ITEM_SIZE * sizeof(code_fragment_t*));
        if (!arr) {
            DBG(ANSI_RED "Failed to allocate memory for arr!\n" ANSI_RESET, NULL);
            return;
        }
        else {
            table = aligned_alloc(alignof(code_fragment_t**), ITEM_SIZE * sizeof(code_fragment_t**));
            if (!table) {
                DBG(ANSI_RED "Failed to allocate memory for table!\n" ANSI_RESET, NULL);
                return;
            }
        }
    }
    else {
        int res = 0;
        if (!arr) {
            arr = mmap(arr, ITEM_SIZE * sizeof(code_fragment_t*), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (arr == MAP_FAILED) {
                DBG(ANSI_RED "Failed to allocate memory to logger's arr!\n" ANSI_RESET, NULL);
                return;
            }
            res = madvise(arr, ITEM_SIZE * sizeof(code_fragment_t**), MADV_SEQUENTIAL);
            if (res == -1) return;
            res = madvise(arr, ITEM_SIZE * sizeof(code_fragment_t**), MADV_MERGEABLE);
            if (res == -1) return;

        }
        table = mmap(NULL, ITEM_SIZE * sizeof(code_fragment_t**), PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (table == MAP_FAILED) {
            DBG(ANSI_YELLOW "Failed to allocate memory for table!\n" ANSI_RESET, NULL);
            return;
        }
        else {
            res = madvise(table, ITEM_SIZE * sizeof(code_fragment_t**), MADV_SEQUENTIAL);
            if (res == -1) return;
            res = madvise(arr, ITEM_SIZE * sizeof(code_fragment_t**), MADV_MERGEABLE);
            if (res == -1) return;
        }

        if (res == -1) {
            DBG(ANSI_YELLOW "Failed to modify memory region space for logger's table!\n" ANSI_RESET, NULL);
            res = munmap(table, ITEM_SIZE * sizeof(code_fragment_t**));
            if (res == -1) {
                DBG(ANSI_RED "Error in init_logger_t failed to unmap table!\n\t returning...\n" ANSI_RESET, NULL);
                res = munmap(arr, ITEM_SIZE * sizeof(code_fragment_t*));
                if (res == -1) {
                    DBG(ANSI_RED "Error in init_logger_t failed to unmap arr!\n\t returning...\n"ANSI_RESET , NULL);
                    return;
                }
                return;
            }
        }
    }
    #if LOGGING == 1 
        if (!logger.add) {
            logger.add = &add;
            #if TESTING == 1
                logger.initiate_write = &initiate_write;
            #endif
            logger.remove_entry = &remove_entry;
        }
    #else
        if (!printer.print) {
            printer.print = &print;
            printer.add = &add;
            #if TESTING == 1
                printer.find = find;
            #endif
        }
    #endif 
    memset(arr, 0, ITEM_SIZE * sizeof(code_fragment_t*));
     memset(table, 0, ITEM_SIZE * sizeof(code_fragment_t**));
}

[[gnu::destructor]]
void dctor_logger() {
    for (size_t i = 0; i < ITEM_SIZE; i++) {
        if (arr[i]) {
            cstr_size(1, arr[i]->desc) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, arr[i]->desc) : unmap_cstr(1, arr[i]->desc);
            memset(arr[i], 0, sizeof(code_fragment_t));
            free(arr[i]);
            arr[i] = NULL;
        }
        if (table[i]) {
            code_fragment_t* frag = *table[i];
            if (frag) {
                cstr_size(1, frag->desc) < ALLOC_THRESHOLD ? reset_and_free_cstr(1, frag->desc) : unmap_cstr(1, frag->desc);
                memset(frag, 0, sizeof(code_fragment_t));
                free(frag);
            }
        }
    }
    memset(timestamp, 0, (size_t)cstr_size(1, timestamp));
    return;
}