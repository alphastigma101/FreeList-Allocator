// Header and translation unit are not tightly integrated. Basically what a logger should do 
// Have the ability to include it or exclude it to redude the amount of bytes needed 
#ifndef _LOGGING_H_
#define _LOGGING_H_
#include "buffer.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Control the time in months, days, or years to clean up the directory default is 30 minutes */
#ifndef CLEANER_TIME 
    #define CLEANER_TIME 1800000
#endif

/* Place where the logs will be stored at */
/* Files are formatted as .json files */
#ifndef DIRECTORY
    #define DIRECTORY "../logs/"
#endif 

/* Change the type of file extension. Default is .json */
#ifndef LOGGER_FILE_TYPE 
    #define LOGGER_FILE_TYPE ".json"
#endif

// TODO: NOT NEEDED ANYMORE! REMOVE THIS FROM ALL TRANSLATION UNIT FILES
#ifndef PRINT_DEBUGGING 
    #define PRINT_DEBUGGING 0
#endif

/* The max length of the message to store in logger */
#ifndef MESSAGE_LEN // TODO: Rename this to buffer instead
    #define MESSAGE_LEN 512 // NOT NEEDED ANYMORE
#endif

#define GET_LOCAL_TIME(buf) \
    do { \
        struct timespec ts; \
        timespec_get(&ts, TIME_UTC); \
        strftime(buf, sizeof(buf), "%a %b %e %T %Y", localtime(&ts.tv_sec)); \
    } while(0)

/* Include the logger or printer variable into the translation unit files - 0 is include printer 1 is include logger */
#ifndef LOGGING 
    #define LOGGING -1
#endif

// TODO: NOT NEEDED ANYMORE! REMOVE THIS FROM ALL TRANSLATION UNIT FILES
#ifndef LOGLEVEL
    #define LOGLEVEL 5
#endif

/* The color print debugging */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_MAGENTA "\033[35m"

typedef struct code_fragment_metadata_t {
    uint8_t written : 1;
    uint8_t comma_added : 1;
    size_t file_pos;
} code_fragment_metadata_t;

typedef struct code_fragment_t {
    
    int priority : 3;
    int line;
    int occurances;
    char* desc;
    char* file;
    code_fragment_metadata_t meta;

} code_fragment_t;


typedef struct logger_t {

    void               (*add)(int priority, const char* file, int line, const char* desc, ...);
    void               (*clean)(const char* file, const int line); // key is date and time
    //#if TESTING == 1
        int               (*initiate_write)(); // This will iterate through keys and entries ad add the {} at the correct spots
    //#endif
    void               (*parse)(const char* file, const int line);
    code_fragment_t*   (*find)(const char* file, const int line);

} logger_t;

typedef struct printer_t {

    void                    (*add)(int priority, const char* file, int line, const char* desc, ...);
    code_fragment_t*        (*find)(const char* file, const int line);
    void                    (*print)(const char* file, const int line);

} printer_t;


#if LOGGING == 1
    extern logger_t logger;
#else
    extern printer_t printer;
#endif

extern void init_logger_t();
extern void clean_logger();

// ─────────────────────────────────────────────────────────────────────────────
// Print Debugging Macros
// ─────────────────────────────────────────────────────────────────────────────

// Print a value with its variable name, file, and line number
#define DBG(_msg, _printer) do { \
    if ((_msg) != NULL) { \
        printf("\n=============================================================\n"); \
        printf("[DBG] " ANSI_CYAN "%s" ANSI_RESET " called from [ " ANSI_MAGENTA "%s" ANSI_RESET " ] on line [ %d ]\n", \
               (const char*)(_msg), __FILE__, __LINE__); \
    } \
    else { \
        printf("\n=============================================================\n"); \
        printf("[DBG] " ANSI_CYAN "%s" ANSI_RESET " called from [ " ANSI_MAGENTA "%s" ANSI_RESET " ] on line [ %d ]\n", \
               _Generic((_printer), \
                   void*: "", \
                   default: ((code_fragment_t*)(_printer)) ? ((code_fragment_t*)(_printer))->desc : ""), \
               _Generic((_printer), \
                   void*: "", \
                   default: ((code_fragment_t*)(_printer)) ? ((code_fragment_t*)(_printer))->file : ""), \
               _Generic((_printer), \
                   void*: 0, \
                   default: ((code_fragment_t*)(_printer)) ? ((code_fragment_t*)(_printer))->line : 0)); \
        printf("%sPriority Level is : [ %d ]\n" ANSI_RESET, \
               _Generic((_printer), \
                   void*: ANSI_GREEN, \
                   default: ((code_fragment_t*)(_printer)) ? \
                       (((code_fragment_t*)(_printer))->priority == 0 ? ANSI_GREEN  : \
                        ((code_fragment_t*)(_printer))->priority == 1 ? "\033[38;5;208m" : \
                                                                         ANSI_RED) \
                       : ANSI_GREEN), \
               _Generic((_printer), \
                   void*: 0, \
                   default: ((code_fragment_t*)(_printer)) ? ((code_fragment_t*)(_printer))->priority : 0)); \
        printf(ANSI_YELLOW "Common occurrences is: [ %d ]\n" ANSI_RESET, \
               _Generic((_printer), \
                   void*: 0, \
                   default: ((code_fragment_t*)(_printer)) ? ((code_fragment_t*)(_printer))->occurances : 0)); \
    } \
    printf("=============================================================\n"); \
} while(0)

#endif 