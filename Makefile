CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./include
TARGET = notecmd
SRC = src/main.c
HEADERS = include/notecmd.h
BUILD_DIR = build
BIN = $(BUILD_DIR)/$(TARGET)
TEST_SCRIPT = tests/test.sh
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

test: $(BIN)
	./$(TEST_SCRIPT)

clean:
	rm -f $(BIN)

install: $(BIN)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all test clean install
