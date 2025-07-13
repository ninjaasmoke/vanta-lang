CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -g -O0
LDFLAGS ?=

SRC_DIR := src
BUILD   := build
SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))
BIN     := vanta

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) $(BIN)
