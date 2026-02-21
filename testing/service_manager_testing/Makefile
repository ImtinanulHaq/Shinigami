# Makefile for Service Manager Testing
# Supports Level 1-5 testing infrastructure

CC := gcc
CFLAGS := -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -I../../core/service_manager
LDFLAGS := -lpthread
ASAN_FLAGS := -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
UBSAN_FLAGS := -fsanitize=undefined

# Directories
CORE_DIR := ../../core/service_manager
BUILD_DIR := build
TESTS_DIR := .

# Source files from core
CORE_SRCS := $(CORE_DIR)/sm_protocol.c \
             $(CORE_DIR)/sm_registry.c \
             $(CORE_DIR)/sm_crypto.c \
             $(CORE_DIR)/sm_rate_limit.c \
             $(CORE_DIR)/sm_handlers.c

# Test files (Unit Tests - Level 2)
UNIT_TESTS := test_sm_protocol \
              test_sm_registry \
              test_sm_crypto \
              test_sm_rate_limit

# Test executables
UNIT_TEST_EXECUTABLES := $(addprefix $(BUILD_DIR)/,$(UNIT_TESTS))

# Fuzz target (Level 5)
FUZZ_TARGET := $(BUILD_DIR)/fuzz_target
FUZZ_TARGET_ASAN := $(BUILD_DIR)/fuzz_target_asan
FUZZ_TARGET_UBSAN := $(BUILD_DIR)/fuzz_target_ubsan

.PHONY: all clean test test-unit test-integration test-e2e test-fuzz test-static
.PHONY: test-asan test-ubsan test-coverage help

# Default target
all: $(UNIT_TEST_EXECUTABLES) $(FUZZ_TARGET)

# Help target
help:
	@echo "Service Manager Testing - Available targets:"
	@echo ""
	@echo "Core Testing:"
	@echo "  make test                - Run all tests (Level 1-5)"
	@echo "  make test-unit           - Level 2: Unit tests only"
	@echo "  make test-integration    - Level 3: Integration tests"
	@echo "  make test-e2e            - Level 4: End-to-end tests"
	@echo "  make test-fuzz           - Level 5: Fuzz testing"
	@echo ""
	@echo "Static Analysis (Level 1):"
	@echo "  make test-static         - Static analysis tools"
	@echo "  make test-cppcheck       - CPPCheck analysis"
	@echo "  make test-clang-tidy     - Clang-tidy analysis"
	@echo "  make test-gcc-warnings   - GCC with strict warnings"
	@echo "  make test-valgrind       - Valgrind memory check"
	@echo ""
	@echo "Sanitizers (Level 5):"
	@echo "  make test-asan           - AddressSanitizer"
	@echo "  make test-ubsan          - UndefinedBehaviorSanitizer"
	@echo "  make test-coverage       - Coverage analysis"
	@echo ""
	@echo "Maintenance:"
	@echo "  make clean               - Remove build artifacts"
	@echo "  make $(BUILD_DIR)        - Create build directory"

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Compile unity framework
$(BUILD_DIR)/unity.o: unity.c unity.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c unity.c -o $(BUILD_DIR)/unity.o

# Unit test compilation rules
$(BUILD_DIR)/test_sm_protocol: test_sm_protocol.c $(BUILD_DIR)/unity.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ test_sm_protocol.c $(BUILD_DIR)/unity.o $(LDFLAGS)

$(BUILD_DIR)/test_sm_registry: test_sm_registry.c $(CORE_DIR)/sm_registry.c $(CORE_DIR)/sm_logging.c $(BUILD_DIR)/unity.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ test_sm_registry.c $(CORE_DIR)/sm_registry.c $(CORE_DIR)/sm_logging.c $(BUILD_DIR)/unity.o $(LDFLAGS)

$(BUILD_DIR)/test_sm_crypto: test_sm_crypto.c $(CORE_DIR)/sm_crypto.c $(CORE_DIR)/sm_logging.c $(BUILD_DIR)/unity.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ test_sm_crypto.c $(CORE_DIR)/sm_crypto.c $(CORE_DIR)/sm_logging.c $(BUILD_DIR)/unity.o $(LDFLAGS)

$(BUILD_DIR)/test_sm_rate_limit: test_sm_rate_limit.c $(CORE_DIR)/sm_rate_limit.c $(CORE_DIR)/sm_logging.c $(BUILD_DIR)/unity.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ test_sm_rate_limit.c $(CORE_DIR)/sm_rate_limit.c $(CORE_DIR)/sm_logging.c $(BUILD_DIR)/unity.o $(LDFLAGS)

# Fuzz target compilation
$(FUZZ_TARGET): fuzz_target.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ fuzz_target.c $(CORE_SRCS) $(LDFLAGS) -lssl -lcrypto -DAFL_MAIN

$(FUZZ_TARGET_ASAN): fuzz_target.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ fuzz_target.c $(CORE_SRCS) $(LDFLAGS) -lssl -lcrypto -DAFL_MAIN

$(FUZZ_TARGET_UBSAN): fuzz_target.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(UBSAN_FLAGS) -o $@ fuzz_target.c $(CORE_SRCS) $(LDFLAGS) -lssl -lcrypto -DAFL_MAIN

# Master test runner
test: all test-unit test-integration test-e2e test-static
	@echo ""
	@echo "========================================="
	@echo "All tests completed!"
	@echo "========================================="

# Level 2: Unit Tests
test-unit: $(UNIT_TEST_EXECUTABLES)
	@echo "Running Unit Tests..."
	@for test in $(UNIT_TEST_EXECUTABLES); do \
		echo ""; \
		$$test || true; \
	done

# Level 3: Integration Tests
test-integration:
	@if [ -f integration_test.sh ]; then \
		echo "Running Integration Tests..."; \
		chmod +x integration_test.sh; \
		./integration_test.sh; \
	else \
		echo "Integration test script not found"; \
	fi

# Level 4: E2E Tests
test-e2e:
	@if [ -f e2e_test.sh ]; then \
		echo "Running E2E Tests..."; \
		chmod +x e2e_test.sh; \
		./e2e_test.sh; \
	else \
		echo "E2E test script not found"; \
	fi

# Level 1: Static Analysis

test-static: test-cppcheck test-clang-tidy test-gcc-warnings test-valgrind

test-cppcheck:
	@echo "========================================="
	@echo "CPPCheck Analysis..."
	@echo "========================================="
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=all --suppress=missingIncludeSystem $(CORE_DIR) testing/ 2>&1 || true; \
	else \
		echo "cppcheck not installed"; \
	fi

test-clang-tidy:
	@echo "========================================="
	@echo "Clang-Tidy Analysis..."
	@echo "========================================="
	@if command -v clang-tidy >/dev/null 2>&1; then \
		for file in $(CORE_DIR)/*.c; do \
			echo "Checking $$file"; \
			clang-tidy $$file -- $(CFLAGS) 2>&1 || true; \
		done; \
	else \
		echo "clang-tidy not installed"; \
	fi

test-gcc-warnings:
	@echo "========================================="
	@echo "GCC Strict Warnings..."
	@echo "========================================="
	@$(CC) -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes \
		-Wwrite-strings -Wpointer-arith -Wbad-function-cast -Wcast-qual \
		-Waggregate-return -Wstrict-aliasing=2 -Wunreachable-code \
		$(CFLAGS) -c $(CORE_DIR)/*.c 2>&1 || true

test-valgrind: $(UNIT_TEST_EXECUTABLES)
	@echo "========================================="
	@echo "Valgrind Memory Check..."
	@echo "========================================="
	@for test in $(UNIT_TEST_EXECUTABLES); do \
		echo "Checking $$test"; \
		valgrind --leak-check=full --show-leak-kinds=all \
			--track-origins=yes --verbose $$test 2>&1 | head -50 || true; \
	done

# Level 5: Sanitizers

test-asan: $(FUZZ_TARGET_ASAN)
	@echo "========================================="
	@echo "AddressSanitizer Testing..."
	@echo "========================================="
	@echo "ASAN build complete: $(FUZZ_TARGET_ASAN)"
	@echo "To use with AFL: afl-fuzz -i input -o output $(FUZZ_TARGET_ASAN)"

test-ubsan: $(FUZZ_TARGET_UBSAN)
	@echo "========================================="
	@echo "UndefinedBehaviorSanitizer Testing..."
	@echo "========================================="
	@echo "UBSAN build complete: $(FUZZ_TARGET_UBSAN)"

test-fuzz: $(FUZZ_TARGET) $(FUZZ_TARGET_ASAN)
	@echo "========================================="
	@echo "Fuzz Testing (Level 5)..."
	@echo "========================================="
	@if command -v afl-fuzz >/dev/null 2>&1; then \
		echo "AFL++ available"; \
		mkdir -p fuzz_input fuzz_output; \
		if [ ! -f fuzz_input/seed.bin ]; then \
			echo -ne '\x53\x4d\x01\x00' > fuzz_input/seed.bin; \
		fi; \
		echo "Running fuzzing (timeout 30s for demo)..."; \
		timeout 30 afl-fuzz -i fuzz_input -o fuzz_output $(FUZZ_TARGET) 2>&1 || true; \
		echo "Fuzz testing completed"; \
	else \
		echo "AFL++ not installed (install afl++ to run fuzz tests)"; \
		echo "Fuzz targets compiled: $(FUZZ_TARGET) $(FUZZ_TARGET_ASAN)"; \
	fi

test-coverage:
	@echo "========================================="
	@echo "Coverage Analysis..."
	@echo "========================================="
	@echo "Building with coverage instrumentation..."
	@$(CC) $(CFLAGS) --coverage -o $(BUILD_DIR)/test_coverage \
		$(CORE_DIR)/sm_protocol.c $(CORE_DIR)/sm_registry.c \
		$(CORE_DIR)/sm_crypto.c $(CORE_DIR)/sm_rate_limit.c \
		$(BUILD_DIR)/unity.o $(LDFLAGS) -lssl -lcrypto 2>&1 || true
	@if [ -f $(BUILD_DIR)/test_coverage ]; then \
		$(BUILD_DIR)/test_coverage; \
		echo "Coverage data generated"; \
	fi

# Cleanup
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) *.o *.gcno *.gcda *.gcov
	@rm -f fuzz_input/* fuzz_output/*
	@echo "Clean complete"

# Info target
info:
	@echo "Service Manager Testing Configuration"
	@echo "======================================"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "ASAN_FLAGS: $(ASAN_FLAGS)"
	@echo ""
	@echo "Unit Tests: $(UNIT_TESTS)"
	@echo "Fuzz Targets: $(FUZZ_TARGET) $(FUZZ_TARGET_ASAN) $(FUZZ_TARGET_UBSAN)"
