cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(MUTEX_ATTR 2)
option(THREAD_STATE 0)
option(USTP 2)
option(INHERITSCHED 1)
option(__SANITIZE_THREAD__ 0)
option(ITEM_SIZE 500)
option(CLEANER_TIME 1800000)
option(TESTING 0)
option(LOGGING 0)
option(ASAN_STACK_MULTIPLIER 16)
#add_compile_options(clang++)

# Advance options go before the project
if(MSVC)

    # MSVC specific options
    add_compile_options(/W4 /WX)  # High warning level, treat warnings as errors
    add_compile_options(/sdl)     # Security development lifecycle checks
    add_compile_options(/Oy-)     # Disable frame pointer omission for better debugging
    #add_compile_definitions(/O2) # Optimization Flag. Should be set when this project is released
    add_compile_options(/fsanitize=address,undefined) # More info Asan/UBsan: https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170 
    # TODO: We need to create code that if we release the build, the following flags should be enabled/disabled
        # We want absolute max performance, and will sacrifice hardening
        # Disable:
            # /W4 /WX
            # /Oy-
            # Asan/UbSan
        # Enable:
            #/O2
            

endif()


project(../
        VERSION 1.0
        LANGUAGES C
        DESCRIPTION "Free List Allocator written in C. Used in Busybox and varies other applications."
        HOMEPAGE_URL "https://github.com/alphastigma101/FreeList-Allocator/tree/C"
)

if (CMAKE_BUILD_TYPE STREQUAL "Testing")
    if (DEFINED ENABLE_DEBUGGING AND ENABLE_DEBUGGING STREQUAL "1")
        add_compile_options(-Wall -Wextra -g -O0 -fno-omit-frame-pointer)
    else()
        add_compile_options(-Wall -Wextra -O2 -fno-omit-frame-pointer)
    endif()
    
    add_executable(test_arena)
    target_link_options(test_arena PRIVATE -fsanitize=address,undefined,leak)
    target_sources(test_arena
        PRIVATE
            arena/arena.c
            threads/pool.c
            tests/arena_test.c
        PUBLIC
            FILE_SET arenaHeaders
            TYPE HEADERS
            BASE_DIRS
                arena
                threads
                tests
            FILES
                arena/arena.h
                threads/threads.h
    )

    add_executable(test_allocator)
    if (DEFINED __SANITIZE_THREAD__ STREQUAL "1")
        target_link_options(test_allocator PRIVATE -fsanitize=thread,undefined -pthread)
    else()
        target_link_options(test_allocator PRIVATE -fsanitize=address,undefined,leak -pthread)
    endif()
    target_compile_definitions(test_allocator 
        PRIVATE 
            ASAN_STACK_MULTIPLIER=${ASAN_STACK_MULTIPLIER}
            MUTEX_ATTR=${MUTEX_ATTR} 
            THREAD_STATE=${THREAD_STATE} 
            USTP=${USTP} 
            INHERITSCHED=${INHERITSCHED}
            LOGGING=${LOGGING} 
            TESTING=${TESTING} 
            ITEM_SIZE=${ITEM_SIZE} 
            CLEANER_TIME=${CLEANER_TIME} 
    )
    target_sources(test_allocator
        PRIVATE
            allocator/allocator.c
            arena/arena.c
            threads/pool.c
            logger/logger.c
            logger/buffer.c
            tests/allocator_test.c
        PUBLIC
            FILE_SET allocatorHeaders
            TYPE HEADERS
            BASE_DIRS
                allocator
                logger
                threads 
                arena
                tests 
            FILES
                logger/logger.h
                logger/buffer.h
                arena/arena.h
                threads/threads.h
    )

    add_executable(test_logger)
    target_compile_definitions(test_logger 
        PRIVATE 
            LOGGING=${LOGGING} 
            TESTING=${TESTING} 
            ITEM_SIZE=${ITEM_SIZE} 
            CLEANER_TIME=${CLEANER_TIME}
    )
    target_sources(test_logger
        PRIVATE
            logger/logger.c
            logger/buffer.c
            tests/logger_test.c
        PUBLIC
            FILE_SET loggerHeaders
            TYPE HEADERS
            BASE_DIRS
                logger
                tests
            FILES
                logger/logger.h
                logger/buffer.h
    )

    add_executable(test_threads)
    if (DEFINED __SANITIZE_THREAD__ STREQUAL "1")
        target_link_options(test_threads PRIVATE -fsanitize=thread,undefined -pthread)
    else()
        target_link_options(test_threads PRIVATE -fsanitize=address,undefined,leak -pthread)
    endif()
    target_compile_definitions(test_threads 
        PRIVATE 
            ASAN_STACK_MULTIPLIER=${ASAN_STACK_MULTIPLIER}
            MUTEX_ATTR=${MUTEX_ATTR} 
            THREAD_STATE=${THREAD_STATE} 
            USTP=${USTP} 
            INHERITSCHED=${INHERITSCHED}
    )

    target_sources(test_threads
        PRIVATE
            threads/pool.c
            tests/multithreading_test.c
        PUBLIC
            FILE_SET poolHeaders
            TYPE HEADERS
            BASE_DIRS
                threads
                tests
            FILES
                threads/threads.h
    )

endif()


if (CMAKE_BUILD_TYPE STREQUAL "Profile")


endif()

if (CMAKE_BUILD_TYPE STREQUAL "Production")
    
    add_executable(test_arena)
    target_sources(test_arena
        PRIVATE
            arena/arena.c
            threads/pool.c
            tests/test_arena.c

        PUBLIC
            FILE_SET arenaHeaders
            TYPE HEADERS
            BASE_DIRS
                arena
                threads
                tests
                
            FILES
                arena/arena.h
                threads/threads.h
    )

    if (DEFINED LOGGING AND LOGGING STREQUAL "1")
        
        add_executable(test_allocator)
        target_sources(test_allocator
            PRIVATE
                allocator/allocator.c
                arena/arena.c
                thread/pool.c
                logger/logger.c
                logger/buffer.c
                tests/test_allocator.c

            PUBLIC
                FILE_SET allocatorHeaders
                TYPE HEADERS
                BASE_DIRS
                    allocator
                    arena
                    threads
                    logger
                    tests

                FILES

                    logger/logger.h
                    logger/buffer.h
                    arena/arena.h
                    threads/pool.h
        )

        add_executable(test_logger)
        target_compile_definitions(test_logger PRIVATE LOGGING=1)
        target_sources(test_logger
            PRIVATE
                logger/logger.c
                logger/buffer.c

            PUBLIC
                FILE_SET loggerHeaders
                TYPE HEADERS
                BASE_DIRS
                    logger
                FILES
                    logger/logger.h
                    logger/buffer.h
        )

    else()

        target_sources(test_allocator
            PRIVATE
                allocator/allocator.c
                arena/arena.c
                thread/pool.c
                tests/test_allocator.c

            PUBLIC
                FILE_SET allocatorHeaders
                TYPE HEADERS
                BASE_DIRS
                    allocator
                    tests
                FILES
                    arena/arena.h
                    threads/pool.h
        )

    endif()
    
    add_executable(test_threads)
    target_sources(test_threads
        PRIVATE
            threads/pool.c
        PUBLIC
            FILE_SET poolHeaders
            TYPE HEADERS
            BASE_DIRS
                threads
            FILES
                threads/pool.h
    )

endif()
