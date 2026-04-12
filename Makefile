CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./include
TARGET = notecmd
SRC = src/main.c
BUILD_DIR = build
BIN = $(BUILD_DIR)/$(TARGET)
TEST_SCRIPT = tests/test.sh

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

test: $(BIN)
	./$(TEST_SCRIPT)

clean:
	rm -f $(BIN)

install: $(BIN)
	cp $(BIN) /usr/local/bin/

.PHONY: all test clean install
