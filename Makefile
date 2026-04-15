CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L -Iinclude

BUILD_DIR = build
APP_BIN = $(BUILD_DIR)/mini_sql
TEST_BIN = $(BUILD_DIR)/mini_sql_tests

APP_SRCS = \
	src/app/main.c \
	src/app/cli.c \
	src/core/parser.c \
	src/core/executor.c \
	src/core/display.c \
	src/storage/storage.c \
	src/storage/database.c \
	src/storage/files.c \
	src/storage/benchmark.c \
	src/index/bptree.c

TEST_SRCS = \
	tests/tests.c \
	src/core/parser.c \
	src/storage/storage.c \
	src/storage/database.c \
	src/storage/files.c \
	src/index/bptree.c

all: $(APP_BIN) $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(APP_BIN): $(APP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(APP_SRCS)

$(TEST_BIN): $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
