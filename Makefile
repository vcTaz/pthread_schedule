CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -I./include
LDFLAGS ?= -pthread

SRC = src/pthread_schedule.c
OBJ = $(SRC:.c=.o)
LIB = libpthread_schedule.a

EXAMPLE_SRC = examples/count_even_example.c
EXAMPLE_BIN = bin/count_even_example

.PHONY: all clean examples

all: $(LIB) examples

$(LIB): $(OBJ)
	ar rcs $@ $^

examples: $(LIB) $(EXAMPLE_BIN)

bin/count_even_example: $(EXAMPLE_SRC) $(LIB)
	@mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(LIB)
	rm -rf bin
