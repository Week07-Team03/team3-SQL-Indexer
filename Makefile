CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2

APP_SRCS = main.c cli.c parser.c executor.c storage.c display.c bptree.c
TEST_SRCS = tests.c parser.c storage.c bptree.c

all: mini_sql mini_sql_tests

mini_sql: $(APP_SRCS)
	$(CC) $(CFLAGS) -o $@ $(APP_SRCS)

mini_sql_tests: $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -f mini_sql mini_sql_tests

.PHONY: all clean
