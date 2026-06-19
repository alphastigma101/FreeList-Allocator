
#include "../logger/logger.h"
#include <assert.h> 
#include <stddef.h>
#include <stdio.h>

#define TEST_PASS    ANSI_BOLD ANSI_GREEN  "  [✔] " ANSI_RESET
#define TEST_FAIL    ANSI_BOLD ANSI_RED    "  [✘] " ANSI_RESET
#define TEST_INFO    ANSI_BOLD ANSI_CYAN   "  [~] " ANSI_RESET
#define TEST_HEADER  ANSI_BOLD ANSI_MAGENTA
#define SEPARATOR    ANSI_CYAN "  ────────────────────────────────────────────\n" ANSI_RESET

static int add_line = 0;
static void add(int a, int b) {

    if (add_line == 0) add_line = __LINE__;

    #if LOGGING == 0

        printer.add(0, "add.c", add_line, 4, "Minus values of: [%d] - [%d] Result of a + b is %d", a, b, a + b);
        //printer.print("subtract.c", sub_line);

    #else 

        logger.add(0, "add.c", add_line, "Added values of: [%d] + [%d] Result of a + b is %d", a, b, a + b);
        
    #endif
}

static int sub_line = 0;
static void substract(int a, int b) {

    if (sub_line == 0) sub_line = __LINE__;
    #if LOGGING == 0
        printer.add(0, "subtract.c", sub_line, "Minus values of: [%d] - [%d] Result of a - b is %d", a, b, a - b);
        //printer.print("subtract.c", sub_line);

    #else 

        logger.add(0, "subtract.c", sub_line, "Minus values of: [%d] - [%d] Result of a - b is %d", a, b, a - b);
        
    #endif

        
}

int main(void) {

    #if LOGGING == 0
        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  PRINTER VARIABLE TEST SUITE                   \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        init_logger_t();
        code_fragment_t* frag;
        {
            printf(SEPARATOR);
            printf(TEST_INFO "1. Init start for addition\n");
            
            add(2, 3);
            frag = printer.find("add.c", add_line);
            assert(frag->occurances == 0);

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
        }

        {
            printf(SEPARATOR);
            printf(TEST_INFO "2. Init start for subtract\n");
            
            substract(2, 3);
            frag = printer.find("subtract.c", sub_line);
            assert(frag->occurances == 0);
            
            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

        }

        {
            printf(SEPARATOR);
            printf(TEST_INFO "3. Update the same memory addresses of code fragments\n");

            add(2, 3);
            add(2, 3);
            add(2, 3);

            frag = printer.find("add.c", add_line);
            assert(frag->occurances == 3);  
            
            substract(2, 3);
            substract(2, 3);
            substract(2, 3);
            substract(2, 3);

            frag = printer.find("subtract.c", sub_line);
            assert(frag->occurances == 4); 
            
            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);  

        }
    #else

        init_logger_t();
        //code_fragment_t* frag;

        {
            printf(SEPARATOR);
            printf(TEST_INFO "1. Init start for addition and write to file\n");
            
            add(2, 3);
            add(6, 7);
            add(9, 10);
            add(20, 2);
            logger.initiate_write();

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

        }

        {
            printf(SEPARATOR);
            printf(TEST_INFO "2. Init start for subtraction and write to file\n");
            
            substract(2, 3);
            logger.initiate_write();

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

        }

        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  LOGGER VARIABLE TEST SUITE                   \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

    
    #endif

    clean_logger();

    return 0;
}