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
option(BENCHMARK_ENV 0)
#option(ASAN_STACK_MULTIPLIER 16)
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
            bitmap/bitmap.c
            threads/threads.c
            logger/buffer.c
            tests/arena_test.c
        PUBLIC
            FILE_SET arenaHeaders
            TYPE HEADERS
            BASE_DIRS
                arena
                bitmap
                threads
                logger
                tests
            FILES
                logger/logger.h
                arena/arena.h
                threads/threads.h
                bitmap/bitmap.h
    )

    add_executable(test_allocator)
    if (DEFINED __SANITIZE_THREAD__ AND  __SANITIZE_THREAD__ STREQUAL "1")
        target_link_options(test_allocator PRIVATE -fsanitize=thread,undefined -pthread)
        target_compile_options(test_allocator PRIVATE -fsanitize=thread,undefined)
    else()
        target_link_options(test_allocator PRIVATE -fsanitize=address,undefined,leak -pthread)
    endif()
    target_compile_definitions(test_allocator 
        PRIVATE 
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
            bitmap/bitmap.c
            threads/threads.c
            logger/logger.c
            logger/buffer.c
            tests/allocator_test.c
        PUBLIC
            FILE_SET allocatorHeaders
            TYPE HEADERS
            BASE_DIRS
                allocator
                bitmap
                logger
                threads 
                arena
                tests 
            FILES
                arena/arena.h
                bitmap/bitmap.h
                logger/buffer.h
                logger/logger.h
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
    if (DEFINED __SANITIZE_THREAD__ AND  __SANITIZE_THREAD__ STREQUAL "1")
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
            threads/threads.c
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

if (CMAKE_BUILD_TYPE STREQUAL "BenchMarking")

    # ============================================================
    # Shared production-grade flags for every BenchMarking executable
    # ============================================================


    # Optimization. -O2 is the conventional "production" level: -O3 turns on
    # more aggressive optimizations that are occasionally slower in practice
    # and more likely to expose latent UB-dependent bugs, so -O2 is the safer,
    # industry-standard default. -DNDEBUG compiles out assert(). -flto enables
    # link-time (cross-translation-unit) optimization -- the most direct lever
    # against "static inline still duplicates": it lets the compiler/linker
    # see across .c file boundaries instead of each TU being an isolated
    # island. -fipa-icf (on by default at -O2, listed explicitly for clarity)
    # merges byte-identical function bodies where doing so is provably safe.
    option(ENABLE_DEBUGGING "Build with -Og -g3 instead of -O2/LTO for a usable debugging experience (trades some runtime speed for accurate stepping/inspection)" OFF)

    if (ENABLE_DEBUGGING)
        set(BENCH_OPT_FLAGS
            -Og
            -g3
        )
    else()
        # -O2 is the conventional "production" level: -O3 turns on more
        # aggressive optimizations that are occasionally slower in practice
        # and more likely to expose latent UB-dependent bugs, so -O2 is the
        # safer, industry-standard default. -DNDEBUG compiles out assert().
        # -flto=auto enables link-time (cross-translation-unit) optimization,
        # parallelized across available cores -- the most direct lever
        # against "static inline still duplicates": it lets the
        # compiler/linker see across .c file boundaries instead of each TU
        # being an isolated island. -fipa-icf (on by default at -O2, listed
        # explicitly for clarity) merges byte-identical function bodies
        # where doing so is provably safe.
        set(BENCH_OPT_FLAGS
            -O2
            -DNDEBUG
            -flto=auto
            -fipa-icf
        )
    endif()

    # Dead-code / duplicate-code elimination -- the most direct, reliable fix
    # for static-inline duplication specifically. -ffunction-sections and
    # -fdata-sections put each function/global in its own linker section;
    # --gc-sections then lets the linker discard any section nothing
    # references -- including an out-of-line static-inline copy a given TU
    # emitted but never actually needed once its own call sites got inlined.
    # Confirmed directly (not assumed): a static inline function whose
    # address is taken -- this codebase's bitmap_test_impl / dispatch-table
    # pattern (allocator.bitmap.bitmap_test = bitmap_test_impl) -- gets one
    # full, separate copy per translation unit that references it.
    set(BENCH_SECTION_FLAGS -ffunction-sections -fdata-sections)
    set(BENCH_SECTION_LINK_FLAGS -Wl,--gc-sections)

    # Hardening -- standard for code handling raw memory directly, which an
    # allocator does by definition. _FORTIFY_SOURCE needs -O1+ to do
    # anything, which -O2 satisfies. -fPIE/-pie enable ASLR for the
    # executable; -z relro -z now make the GOT read-only after relocation;
    # -z noexecstack blocks an executable stack. Worth knowing: stack
    # protector and PIE both carry a small, usually-negligible per-call/
    # per-load overhead -- real for a latency-sensitive allocator, just
    # rarely the dominant cost. Drop -fstack-protector-strong first if this
    # ever shows up in a profile.
    set(BENCH_HARDENING_FLAGS -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE)
    set(BENCH_HARDENING_LINK_FLAGS -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)

    # Warnings. -Wall -Wextra is the baseline. -Wshadow, -Wconversion, and
    # -Wsign-conversion catch real bug classes (this codebase mixes size_t /
    # unsigned int / int extensively). -Wformat=2 and -Wnull-dereference
    # catch common, serious mistakes. -Wpedantic is deliberately OMITTED:
    # this codebase relies on GNU extensions throughout (statement
    # expressions, __auto_type, __builtin_expect/__builtin_prefetch,
    # [[gnu::destructor]]), and -Wpedantic would flag all of that as noise,
    # not signal. -Werror is included on the premise that production code
    # should be warning-clean by policy -- but be aware -Wconversion /
    # -Wsign-conversion are the two most likely to surface a real backlog of
    # existing warnings on a codebase not written with them on from the
    # start; if that turns this into an immediate wall of build failures,
    # that's accurate signal, not a bug in this flag set -- consider fixing
    # incrementally with -Werror removed temporarily rather than dropping
    # the warnings themselves.
    set(BENCH_WARNING_FLAGS
        -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
        -Wformat=2 -Wnull-dereference -Wdouble-promotion -Werror
    )

    # Sanitizers -- OPT-IN, not default. Sanitizers and accurate
    # benchmarking are in direct tension: ASan alone typically costs 2-3x
    # wall-clock overhead, which would badly distort every timing number
    # this target exists to produce. Defaulting them off keeps
    # "BenchMarking" builds actually measuring performance by default;
    # flip ENABLE_BENCH_SANITIZERS=ON for a dedicated correctness pass when
    # that's specifically the goal, with timing distortion understood as
    # the accepted cost of that mode. -fno-omit-frame-pointer is standard
    # alongside ASan -- it makes the resulting stack traces meaningfully
    # more accurate.
    option(ENABLE_BENCH_SANITIZERS "Build BenchMarking executables with ASan/UBSan/LSan (distorts timing -- for correctness passes, not performance measurement)" OFF)
    set(BENCH_SANITIZE_FLAGS "")
    if (ENABLE_BENCH_SANITIZERS)
        set(BENCH_SANITIZE_FLAGS -fsanitize=address,undefined,leak -fno-omit-frame-pointer)
    endif()

    # ============================================================
    # bench_allocator
    # ============================================================
    add_executable(bench_allocator)
    target_compile_options(bench_allocator PRIVATE
        ${BENCH_OPT_FLAGS} ${BENCH_SECTION_FLAGS} ${BENCH_HARDENING_FLAGS}
        ${BENCH_WARNING_FLAGS} ${BENCH_SANITIZE_FLAGS}
    )
    # -fsanitize must be passed to BOTH compile and link steps, not link
    # alone -- verified directly: a stack-buffer-overflow built with
    # -fsanitize on link only produced a garbled "SEGV on unknown address /
    # wild jump" crash instead of ASan's normal, precise
    # "stack-buffer-overflow" report pointing at the exact line. Link-only
    # instrumentation still catches some bugs (malloc/free/memcpy misuse,
    # via libc interceptors that don't need the calling code instrumented),
    # but silently fails to properly catch or diagnose others (stack and
    # global overflows, use-after-return) -- exactly the classes of bug
    # most likely to show up in hand-written, pointer-heavy allocator code.
    target_link_options(bench_allocator PRIVATE
        ${BENCH_OPT_FLAGS} ${BENCH_SECTION_LINK_FLAGS} ${BENCH_HARDENING_LINK_FLAGS}
        ${BENCH_SANITIZE_FLAGS} -g -pthread
    )
    target_compile_definitions(bench_allocator
        PRIVATE
            BENCHMARK_ENV=${BENCHMARK_ENV}
            MUTEX_ATTR=${MUTEX_ATTR}
            THREAD_STATE=${THREAD_STATE}
            USTP=${USTP}
            INHERITSCHED=${INHERITSCHED}
            LOGGING=${LOGGING}
            TESTING=${TESTING}
            ITEM_SIZE=${ITEM_SIZE}
            CLEANER_TIME=${CLEANER_TIME}
    )
    target_sources(bench_allocator
        PRIVATE
            allocator/allocator.c
            arena/arena.c
            bitmap/bitmap.c
            threads/threads.c
            logger/logger.c
            logger/buffer.c
            benchmark/allocator_benchmark.c
        PUBLIC
            FILE_SET allocatorHeaders
            TYPE HEADERS
            BASE_DIRS
                allocator
                bitmap
                logger
                threads
                arena
                benchmark
            FILES
                allocator/allocator.h 
                arena/arena.h
                bitmap/bitmap.h
                logger/buffer.h
                logger/logger.h
                threads/threads.h
    )

    # ============================================================
    # bench_logger
    # ============================================================
    add_executable(bench_logger)
    target_compile_options(bench_logger PRIVATE
        ${BENCH_OPT_FLAGS} ${BENCH_SECTION_FLAGS} ${BENCH_HARDENING_FLAGS}
        ${BENCH_WARNING_FLAGS} ${BENCH_SANITIZE_FLAGS}
    )
    target_link_options(bench_logger PRIVATE
        ${BENCH_OPT_FLAGS} ${BENCH_SECTION_LINK_FLAGS} ${BENCH_HARDENING_LINK_FLAGS}
        ${BENCH_SANITIZE_FLAGS}
    )
    target_compile_definitions(bench_logger
        PRIVATE
            BENCHMARK_ENV=${BENCHMARK_ENV}
            LOGGING=${LOGGING}
            TESTING=${TESTING}
            ITEM_SIZE=${ITEM_SIZE}
            CLEANER_TIME=${CLEANER_TIME}
    )
    target_sources(bench_logger
        PRIVATE
            logger/logger.c
            logger/buffer.c
            benchmark/logger_benchmark.c
        PUBLIC
            FILE_SET loggerHeaders
            TYPE HEADERS
            BASE_DIRS
                logger
                benchmark
            FILES
                logger/logger.h
                logger/buffer.h
    )

    # ============================================================
    # bench_threads
    # ============================================================
    add_executable(bench_threads)
    target_compile_options(bench_threads PRIVATE
        ${BENCH_OPT_FLAGS} ${BENCH_SECTION_FLAGS} ${BENCH_HARDENING_FLAGS}
        ${BENCH_WARNING_FLAGS} ${BENCH_SANITIZE_FLAGS}
    )
    target_link_options(bench_threads PRIVATE
        ${BENCH_OPT_FLAGS} ${BENCH_SECTION_LINK_FLAGS} ${BENCH_HARDENING_LINK_FLAGS}
        ${BENCH_SANITIZE_FLAGS} -pthread
    )
    target_compile_definitions(bench_threads
        PRIVATE
            BENCHMARK_ENV=${BENCHMARK_ENV}
            MUTEX_ATTR=${MUTEX_ATTR}
            THREAD_STATE=${THREAD_STATE}
            USTP=${USTP}
            INHERITSCHED=${INHERITSCHED}
    )
    target_sources(bench_threads
        PRIVATE
            threads/threads.c
            benchmark/threading_benchmark.c
        PUBLIC
            FILE_SET poolHeaders
            TYPE HEADERS
            BASE_DIRS
                threads
                tests
                benchmark
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
