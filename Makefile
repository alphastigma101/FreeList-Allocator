CC = clang-19
CFLAGS = -Wall -Wextra -g -O0 -fno-omit-frame-pointer
SANITIZERS = -fsanitize=address,undefined,leak
LDFLAGS = $(SANITIZERS) -pthread

THREAD_SRCS  = tests/multithreading_test.c threads/pool.c
THREAD_OBJS  = $(THREAD_SRCS:.c=.o)

ARENA_SRCS = tests/arena_test.c arena/arena.c threads/pool.c
ARENA_OBJS = $(ARENA_SRCS:.c=.o)

ALLOCATOR_SRCS = tests/allocator_test.c allocator/allocator.c arena/arena.c threads/pool.c
ALLOCATOR_OBJS = $(ALLOCATOR_SRCS:.c=.o)

TARGETS = test_multithreading test_arena test_allocator

all: $(TARGETS)

test_multithreading: $(THREAD_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	
test_arena: $(ARENA_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	
test_allocator: $(ALLOCATOR_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(SANITIZERS) -c $< -o $@

clean:
	rm -f $(TARGETS) $(PARSER_OBJS) $(THREAD_OBJS) $(ARENA_OBJS) $(ALLOCATOR_OBJS)


profile:
	# Create assembly output directory
	mkdir -p asm_output
	
	# Generate assembly files for macro version (with sanitizers)
	$(CC) $(CFLAGS) $(SANITIZERS) -S tests/multithreading_test.c -o asm_output/multithreading_test_macro.s
	$(CC) $(CFLAGS) $(SANITIZERS) -S threads/pool.c -o asm_output/pool_macro.s
	$(CC) $(CFLAGS) $(SANITIZERS) -S arena/arena.c -o asm_output/arena_macro.s
	$(CC) $(CFLAGS) $(SANITIZERS) -S allocator/allocator.c -o asm_output/allocator_macro.s
	
	# Generate assembly files without sanitizers for comparison
	$(CC) $(CFLAGS) -S tests/multithreading_test.c -o asm_output/multithreading_test_no_san.s
	$(CC) $(CFLAGS) -S threads/pool.c -o asm_output/pool_no_san.s
	$(CC) $(CFLAGS) -S arena/arena.c -o asm_output/arena_no_san.s
	$(CC) $(CFLAGS) -S allocator/allocator.c -o asm_output/allocator_no_san.s
	
	# Generate optimized assembly (-O2) to see inlining decisions
	$(CC) -O2 -S tests/multithreading_test.c -o asm_output/multithreading_test_O2.s
	$(CC) -O2 -S threads/pool.c -o asm_output/pool_O2.s
	
	# Count macro expansions in each assembly file
	@echo "\n=== Macro Expansion Counts (with sanitizers) ==="
	@grep -c "movq.*\$$0x0,-0x[0-9a-f]*.*(%rbp)" asm_output/pool_macro.s || echo "0"
	
	@echo "\n=== Macro Expansion Counts (without sanitizers) ==="
	@grep -c "movq.*\$$0x0,-0x[0-9a-f]*.*(%rbp)" asm_output/pool_no_san.s || echo "0"
	
	# Show where FIND_AVAILABLE_THREAD expands
	@echo "\n=== FIND_AVAILABLE_THREAD expansion locations ==="
	@grep -n "FIND_AVAILABLE_THREAD" asm_output/*.s || echo "Not found in assembly (macro expanded)"
	
	# Generate AST dump for macro analysis
	$(CC) -Xclang -ast-dump -fsyntax-only threads/pool.c 2> asm_output/pool_ast.txt
	@echo "\n=== Macro expansions in AST ==="
	@grep -c "MacroExpansion.*FIND_AVAILABLE" asm_output/pool_ast.txt || echo "0"
	
	# Create comparison summary
	@echo "\n=== File Size Comparison ==="
	@ls -lh asm_output/*.s | awk '{print $$9, $$5}'
	
	# Optional: Generate object file size comparison
	@echo "\n=== Object File Size Comparison (with vs without sanitizers) ==="
	$(CC) $(CFLAGS) $(SANITIZERS) -c threads/pool.c -o asm_output/pool_with_san.o
	$(CC) $(CFLAGS) -c threads/pool.c -o asm_output/pool_no_san.o
	@ls -lh asm_output/pool_*.o | awk '{print $$9, $$5}'

# Alternative: Profile specific macro function
profile-macro-only:
	# Generate assembly only for the function containing FIND_AVAILABLE_THREAD
	$(CC) $(CFLAGS) $(SANITIZERS) -S threads/pool.c -o asm_output/pool_full.s
	@echo "\n=== Extracting deallocate function assembly ==="
	@sed -n '/^deallocate:/,/^[a-zA-Z_]*:/p' asm_output/pool_full.s > asm_output/deallocate_macro.s
	@echo "Saved to asm_output/deallocate_macro.s"
	@wc -l asm_output/deallocate_macro.s

# Clean assembly output
clean-profile:
	rm -rf asm_output/

# Compare macro vs inline function (requires both versions)
compare: profile
	@echo "\n=== COMPARISON: Macro vs Inline Function ==="
	@echo "Macro version - loop initializations:"
	@grep -c "movq.*\$$0x0,-0x" asm_output/pool_macro.s || echo "0"
	@echo "Macro version - UBSan/ASan calls:"
	@grep -c "__ubsan\|__asan" asm_output/pool_macro.s || echo "0"


.PHONY: all clean profile profile-macro-only clean-profile compare
