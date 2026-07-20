
#include "../logger/logger.h"
#include <assert.h> 
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_PASS    ANSI_BOLD ANSI_GREEN  "  [✔] " ANSI_RESET
#define TEST_FAIL    ANSI_BOLD ANSI_RED    "  [✘] " ANSI_RESET
#define TEST_INFO    ANSI_BOLD ANSI_CYAN   "  [~] " ANSI_RESET
#define TEST_HEADER  ANSI_BOLD ANSI_MAGENTA
#define SEPARATOR    ANSI_CYAN "  ────────────────────────────────────────────\n" ANSI_RESET

const char test_one[] =
    "{\n"
    "\t\"add.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Added values of: [2] + [3] Result of a + b is 5\",\n"
    "\t\t\"Added values of: [6] + [7] Result of a + b is 13\",\n"
    "\t\t\"Added values of: [9] + [10] Result of a + b is 19\",\n"
    "\t\t\"Added values of: [20] + [2] Result of a + b is 22\",\n"
    "\t\t16\n"
    "\t]\n"
    "}";

const char test_two[] =
    "{\n"
    "\t\"add.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Added values of: [2] + [3] Result of a + b is 5\",\n"
    "\t\t\"Added values of: [6] + [7] Result of a + b is 13\",\n"
    "\t\t\"Added values of: [9] + [10] Result of a + b is 19\",\n"
    "\t\t\"Added values of: [20] + [2] Result of a + b is 22\",\n"
    "\t\t16\n"
    "\t],\n"
    "\t\"subtract.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t0,\n"
    "\t\t\"Minus values of: [2] - [3] Result of a - b is -1\",\n"
    "\t\t33\n"
    "\t]\n"
    "}";

const char test_three[] =
    "{\n"
    "\t\"add.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Added values of: [2] + [3] Result of a + b is 5\",\n"
    "\t\t\"Added values of: [6] + [7] Result of a + b is 13\",\n"
    "\t\t\"Added values of: [9] + [10] Result of a + b is 19\",\n"
    "\t\t\"Added values of: [20] + [2] Result of a + b is 22\",\n"
    "\t\t16\n"
    "\t],\n"
    "\t\"subtract.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t0,\n"
    "\t\t\"Minus values of: [2] - [3] Result of a - b is -1\",\n"
    "\t\t33\n"
    "\t],\n"
    "\t\"multiplication.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Multiple values of: [2] - [3] Result of a * b is 6\",\n"
    "\t\t\"Multiple values of: [6] - [7] Result of a * b is 42\",\n"
    "\t\t\"Multiple values of: [9] - [10] Result of a * b is 90\",\n"
    "\t\t\"Multiple values of: [20] - [2] Result of a * b is 40\",\n"
    "\t\t50\n"
    "\t]\n"
    "}";

const char test_four[] =
    "{\n"
    "\t\"add.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Added values of: [2] + [3] Result of a + b is 5\",\n"
    "\t\t\"Added values of: [6] + [7] Result of a + b is 13\",\n"
    "\t\t\"Added values of: [9] + [10] Result of a + b is 19\",\n"
    "\t\t\"Added values of: [20] + [2] Result of a + b is 22\",\n"
    "\t\t16\n"
    "\t],\n"
    "\t\"subtract.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t0,\n"
    "\t\t\"Minus values of: [2] - [3] Result of a - b is -1\",\n"
    "\t\t33\n"
    "\t],\n"
    "\t\"multiplication.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Multiple values of: [2] - [3] Result of a * b is 6\",\n"
    "\t\t\"Multiple values of: [6] - [7] Result of a * b is 42\",\n"
    "\t\t\"Multiple values of: [9] - [10] Result of a * b is 90\",\n"
    "\t\t\"Multiple values of: [20] - [2] Result of a * b is 40\",\n"
    "\t\t50\n"
    "\t],\n"
    "\t\"division.c\":\n"
    "\t[\n"
    "\t\t0,\n"
    "\t\t3,\n"
    "\t\t\"Division values of: [2] - [3] Result of a / b is 0\",\n"
    "\t\t\"Division values of: [6] - [7] Result of a / b is 0\",\n"
    "\t\t\"Division values of: [9] - [10] Result of a / b is 0\",\n"
    "\t\t\"Division values of: [20] - [2] Result of a / b is 10\",\n"
    "\t\t68\n"
    "\t]\n"
    "}";
	



static int add_line = 0;
static void add(int a, int b) {

    if (add_line == 0) add_line = __LINE__;

    #if LOGGING == 0

        printer.add(0, "add.c", add_line, "Minus values of: [%d] - [%d] Result of a + b is %d", a, b, a + b);
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

static int multiple_line = 0;
static void multiplication(int a, int b) {

    if (multiple_line == 0) multiple_line = __LINE__;
    #if LOGGING == 0
        printer.add(0, "multiplication.c", multiple_line, "Multiple values of: [%d] - [%d] Result of a * b is %d", a, b, a * b);
        //printer.print("subtract.c", sub_line);

    #else 

        logger.add(0, "multiplication.c", multiple_line, "Multiple values of: [%d] - [%d] Result of a * b is %d", a, b, a * b);
        
    #endif

        
}


static int division_line = 0;
static void division(int a, int b) {

    if (division_line == 0) division_line = __LINE__;
    #if LOGGING == 0
        printer.add(0, "division.c", division_line, "Division values of: [%d] - [%d] Result of a / b is %d", a, b, a / b);
        //printer.print("subtract.c", sub_line);

    #else 

        logger.add(0, "division.c", division_line, "Division values of: [%d] - [%d] Result of a / b is %d", a, b, a / b);
        
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
        {
            printf(SEPARATOR);
            printf(TEST_INFO "4. Update the same memory addresses of code fragments mulitplication/division\n");

            multiplication(2, 3);
            multiplication(2, 3);
            multiplication(2, 3);

            frag = printer.find("mulitplication.c", multiple_line);
            assert(frag->occurances == 3);  
            
            division(2, 3);
            division(2, 3);
            division(2, 3);
            division(2, 3);

            frag = printer.find("division.c", division_line);
            assert(frag->occurances == 4); 
            
            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET); 
        }
    #else

        init_logger_t();
        int line = 0;
        printf("ITEM_SIZE Value is: %d\n", ITEM_SIZE);
        printf("CLEANER_TIME Value is: %d\n", CLEANER_TIME);
        
        {
            printf(SEPARATOR);
            printf(TEST_INFO "1. Init start for addition and write to file\n");
            
            add(2, 3);
            add(6, 7);
            add(9, 10);
            add(20, 2);
            logger.initiate_write();
            
            FILE* fp = NULL;
            FILE* cmp = NULL;
            
            const char* file = buffer.get_dir_t_cstr();
            //char* test_one_file = write_long_cstr(0x0, 2, "../snapshots", "test_one.json");
            const size_t size = cstr_size(1, test_one);
            
            fp = fopen(file, "r");
            cmp = fopen("./snapshots/test_one.json", "r");
            //fwrite(test_one, 1, size,cmp);
            //fflush(cmp);
            //rewind(cmp); 
            
            for (size_t i = 0; i < size; i++) {
                const int a = fgetc(fp);
                const int b = fgetc(cmp);
                
                if (a != b) {
                    printf("\n"
                        ANSI_RED "  ╔══════════════════════════════════════════════════╗\n"
                        "  ║" ANSI_RESET ANSI_BOLD "          ✗  ASSERTION FAILURE — TEST ONE          " ANSI_RESET ANSI_RED "║\n"
                        "  ╚══════════════════════════════════════════════════╝\n" ANSI_RESET
                        "\n"
                        "   " ANSI_CYAN "📍 Location" ANSI_RESET "  →  line " ANSI_BOLD "%d" ANSI_RESET "\n"
                        "\n"
                        "   " ANSI_GREEN "✓ Expected" ANSI_RESET "   →  " ANSI_GREEN "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "   " ANSI_RED   "✗ Got     " ANSI_RESET "   →  " ANSI_RED   "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "\n"
                        ANSI_YELLOW "  ──────────────────────────────────────────────────\n" ANSI_RESET
                        "   " ANSI_YELLOW "⚠  Mismatch detected — halting execution" ANSI_RESET "\n\n",
                        line,
                        test_one[i], (unsigned char)test_one[i],
                        a, (unsigned char)a
                    );
                    exit(-1);
                }
                line = line + 1;
            }

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
            
            //memset(test_one_file, 0, cstr_size(1, file));
            //free(test_one_file);
            fclose(fp);
            fclose(cmp);

        }

        {
            printf(SEPARATOR);
            printf(TEST_INFO "2. Init start for subtraction and write to file\n");
            
            substract(2, 3);
            logger.initiate_write();

            FILE* fp = NULL;
            FILE* cmp = NULL;

            const char* file = buffer.get_dir_t_cstr();
            //char* test_one_file = write_long_cstr(0x0, 2, "../snapshots", "test_one.json");
            const size_t size = cstr_size(1, test_two);
            
            fp = fopen(file, "r");
            cmp = fopen("./snapshots/test_two.json", "r");
    
            //fwrite(test_one, 1, size,cmp);
            //fflush(cmp);
            //rewind(cmp); 
            
            for (size_t i = 0; i < size; i++) {
                const int a = fgetc(fp);
                const int b = fgetc(cmp);
                
                if (a != b) {
                    printf("\n"
                        ANSI_RED "  ╔══════════════════════════════════════════════════╗\n"
                        "  ║" ANSI_RESET ANSI_BOLD "          ✗  ASSERTION FAILURE — TEST TWO          " ANSI_RESET ANSI_RED "║\n"
                        "  ╚══════════════════════════════════════════════════╝\n" ANSI_RESET
                        "\n"
                        "   " ANSI_CYAN "📍 Location" ANSI_RESET "  →  line " ANSI_BOLD "%d" ANSI_RESET "\n"
                        "\n"
                        "   " ANSI_GREEN "✓ Expected" ANSI_RESET "   →  " ANSI_GREEN "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "   " ANSI_RED   "✗ Got     " ANSI_RESET "   →  " ANSI_RED   "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "\n"
                        ANSI_YELLOW "  ──────────────────────────────────────────────────\n" ANSI_RESET
                        "   " ANSI_YELLOW "⚠  Mismatch detected — halting execution" ANSI_RESET "\n\n",
                        line,
                        test_two[i], (unsigned char)test_two[i],
                        a, (unsigned char)a
                    );
                    exit(-1);
                }
                line = line + 1;
            }

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);

            //memset(test_one_file, 0, cstr_size(1, file));
            //free(test_one_file);
            fclose(fp);
            fclose(cmp);

        }

        {
            printf(SEPARATOR);
            printf(TEST_INFO "2. Init start for multiplication and write to file\n");
            
            multiplication(2, 3);
            multiplication(6, 7);
            multiplication(9, 10);
            multiplication(20, 2);
            logger.initiate_write();
            FILE* fp = NULL;
            FILE* cmp = NULL;
            
            const char* file = buffer.get_dir_t_cstr();
            //char* test_one_file = write_long_cstr(0x0, 2, "../snapshots", "test_one.json");
            const size_t size = cstr_size(1, test_three);
            
            fp = fopen(file, "r");
            cmp = fopen("./snapshots/test_three.json", "r");
    
            //fwrite(test_one, 1, size,cmp);
            //fflush(cmp);
            //rewind(cmp); 
            
            for (size_t i = 0; i < size; i++) {
                const int a = fgetc(fp);
                const int b = fgetc(cmp);
                
                if (a != b) {
                    printf("\n"
                        ANSI_RED "  ╔══════════════════════════════════════════════════╗\n"
                        "  ║" ANSI_RESET ANSI_BOLD "          ✗  ASSERTION FAILURE — TEST ONE          " ANSI_RESET ANSI_RED "║\n"
                        "  ╚══════════════════════════════════════════════════╝\n" ANSI_RESET
                        "\n"
                        "   " ANSI_CYAN "📍 Location" ANSI_RESET "  →  line " ANSI_BOLD "%d" ANSI_RESET "\n"
                        "\n"
                        "   " ANSI_GREEN "✓ Expected" ANSI_RESET "   →  " ANSI_GREEN "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "   " ANSI_RED   "✗ Got     " ANSI_RESET "   →  " ANSI_RED   "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "\n"
                        ANSI_YELLOW "  ──────────────────────────────────────────────────\n" ANSI_RESET
                        "   " ANSI_YELLOW "⚠  Mismatch detected — halting execution" ANSI_RESET "\n\n",
                        line,
                        test_one[i], (unsigned char)test_one[i],
                        a, (unsigned char)a
                    );
                    exit(-1);
                }
                line = line + 1;
            }

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
            
            //memset(test_one_file, 0, cstr_size(1, file));
            //free(test_one_file);
            fclose(fp);
            fclose(cmp);

        }

        {
            printf(SEPARATOR);
            printf(TEST_INFO "3. Init start for division and write to file\n");
            
            division(2, 3);
            division(6, 7);
            division(9, 10);
            division(20, 2);
            logger.initiate_write();

            FILE* fp = NULL;
            FILE* cmp = NULL;
            
            const char* file = buffer.get_dir_t_cstr();
            //char* test_one_file = write_long_cstr(0x0, 2, "../snapshots", "test_one.json");
            const size_t size = cstr_size(1, test_four);
            
            fp = fopen(file, "r");
            cmp = fopen("./snapshots/test_four.json", "r");
    
            //fwrite(test_one, 1, size,cmp);
            //fflush(cmp);
            //rewind(cmp); 
            
            for (size_t i = 0; i < size; i++) {
                const int a = fgetc(fp);
                const int b = fgetc(cmp);
                
                if (a != b) {
                    printf("\n"
                        ANSI_RED "  ╔══════════════════════════════════════════════════╗\n"
                        "  ║" ANSI_RESET ANSI_BOLD "          ✗  ASSERTION FAILURE — TEST ONE          " ANSI_RESET ANSI_RED "║\n"
                        "  ╚══════════════════════════════════════════════════╝\n" ANSI_RESET
                        "\n"
                        "   " ANSI_CYAN "📍 Location" ANSI_RESET "  →  line " ANSI_BOLD "%d" ANSI_RESET "\n"
                        "\n"
                        "   " ANSI_GREEN "✓ Expected" ANSI_RESET "   →  " ANSI_GREEN "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "   " ANSI_RED   "✗ Got     " ANSI_RESET "   →  " ANSI_RED   "'%c'" ANSI_RESET "  (0x%02x)\n"
                        "\n"
                        ANSI_YELLOW "  ──────────────────────────────────────────────────\n" ANSI_RESET
                        "   " ANSI_YELLOW "⚠  Mismatch detected — halting execution" ANSI_RESET "\n\n",
                        line,
                        test_one[i], (unsigned char)test_one[i],
                        a, (unsigned char)a
                    );
                    exit(-1);
                }
                line = line + 1;
            }

            printf(SEPARATOR);
            printf(TEST_HEADER "  RESULT: " ANSI_GREEN "PASSED ✔\n" ANSI_RESET);
            printf(TEST_HEADER "  ══════════════════════════════════════════════\n\n" ANSI_RESET);
            
            //memset(test_one_file, 0, cstr_size(1, file));
            //free(test_one_file);
            fclose(fp);
            fclose(cmp);
        }

        printf("\n");
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);
        printf(TEST_HEADER "  LOGGER VARIABLE TEST SUITE                   \n" ANSI_RESET);
        printf(TEST_HEADER "  ══════════════════════════════════════════════\n" ANSI_RESET);

    
    #endif

    return 0;
}