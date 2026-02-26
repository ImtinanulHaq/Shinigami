CC      := gcc
HAL_DIR := ../../dev/hal
UNITY_DIR := ../service_manager_testing
BUILD_DIR := build

HAVE_ALSA ?= 0

# Adding -Wno-builtin-macro-redefined prevents errors if a .c file also defines these
CFLAGS := \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic \
    -Wno-builtin-macro-redefined \
    -D_POSIX_C_SOURCE=200809L \
    -I$(HAL_DIR) \
    -I$(UNITY_DIR) \
    -g -O0 -DDEBUG

# ... rest of your Makefile remains the same ...
ifeq ($(HAVE_ALSA),1)
    CFLAGS  += -DHAVE_ALSA
    LDFLAGS := -lasound
else
    LDFLAGS := 
endif

LDFLAGS += -lpthread -lm -lrt

HAL_SRCS := $(wildcard $(HAL_DIR)/*.c)
UNITY_SRC := $(UNITY_DIR)/unity.c
TEST_SRCS := $(wildcard test_*.c)
TEST_BINS := $(patsubst %.c, $(BUILD_DIR)/%, $(TEST_SRCS))

GREEN  := \033[32m
YELLOW := \033[33m
RED    := \033[31m
RESET  := \033[0m

.PHONY: all test clean

all: $(BUILD_DIR) $(TEST_BINS)

$(BUILD_DIR)/test_%: test_%.c $(HAL_SRCS) $(UNITY_SRC) | $(BUILD_DIR)
	@echo "$(GREEN)[CC]$(RESET) $@"
	@$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

test: all
	@echo "--- Running HAL Tests ---"
	@for t in $(TEST_BINS); do ./$$t; done

clean:
	@rm -rf $(BUILD_DIR)
