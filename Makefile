# MicroOS Master Makefile
# Phase 1: Foundation

.PHONY: all clean test help phase1 build run

# Directories
PHASE1_DIR := phase1
MIDDLEWARE_DIR := $(PHASE1_DIR)/middleware
ROOTFS_DIR := $(PHASE1_DIR)/rootfs
BUILD_DIR := $(PHASE1_DIR)/build
SCRIPTS_DIR := $(PHASE1_DIR)/scripts

help:
	@echo "╔════════════════════════════════════════════╗"
	@echo "║   MicroOS Build System - Phase 1           ║"
	@echo "╚════════════════════════════════════════════╝"
	@echo ""
	@echo "Targets:"
	@echo "  make all       - Build all components"
	@echo "  make build     - Build middleware & RootFS"
	@echo "  make run       - Run QEMU with compiled system"
	@echo "  make test      - Run test suite"
	@echo "  make clean     - Remove all build artifacts"
	@echo "  make help      - Show this message"
	@echo ""
	@echo "Quick start:"
	@echo "  make all && make run"
	@echo ""

all: build

build:
	@echo "Building Phase 1..."
	@bash $(SCRIPTS_DIR)/build.sh

run:
	@echo "Starting QEMU..."
	@bash $(SCRIPTS_DIR)/run-qemu.sh

test:
	@echo "Running tests..."
	@bash $(SCRIPTS_DIR)/test.sh

clean:
	@echo "Cleaning..."
	@make -C $(MIDDLEWARE_DIR) clean 2>/dev/null || true
	@rm -rf $(BUILD_DIR)
	@echo "✓ Clean complete"

.PHONY: help all build run test clean
