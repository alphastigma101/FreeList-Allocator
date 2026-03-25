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

.PHONY: all clean 
