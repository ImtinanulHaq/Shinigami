# =============================================================================
# GNUmakefile — convenience wrapper around cmake --preset
#
# This is NOT the build system. CMake is the build system.
# This file exists solely to give developers familiar Make entry points.
#
# All targets delegate to cmake --preset. Never add raw compiler invocations
# here — that's exactly the kind of thing CMake Presets handle properly.
#
# Usage:
#   make              → configure + build relwithdebinfo (default)
#   make debug        → configure + build debug preset
#   make release      → configure + build release preset
#   make ci           → configure + build + test ci preset
#   make test         → run tests on current build
#   make install      → install (requires sudo or a prefix)
#   make clean        → wipe build directories
#   make package      → build .deb + .rpm + .tgz packages
#   make format       → run clang-format on all sources
#   make tidy         → run clang-tidy (configures static-analysis preset)
#   make help         → this list
# =============================================================================

.PHONY: all debug release relwithdebinfo ci tsan
.PHONY: test test-debug test-ci
.PHONY: install clean distclean package
.PHONY: format tidy
.PHONY: cross-aarch64 cross-arm
.PHONY: help

CMAKE  ?= cmake
NINJA  ?= ninja
NPROC  := $(shell nproc 2>/dev/null || echo 4)

# Default target
all: relwithdebinfo

# ---------------------------------------------------------------------------
# Configure + Build presets
# ---------------------------------------------------------------------------
relwithdebinfo:
	$(CMAKE) --preset relwithdebinfo
	$(CMAKE) --build --preset relwithdebinfo --parallel $(NPROC)

debug:
	$(CMAKE) --preset debug
	$(CMAKE) --build --preset debug --parallel $(NPROC)

release:
	$(CMAKE) --preset release
	$(CMAKE) --build --preset release --parallel $(NPROC)

ci:
	$(CMAKE) --preset ci
	$(CMAKE) --build --preset ci --parallel $(NPROC)
	$(CMAKE) --test-preset ci

tsan:
	$(CMAKE) --preset tsan
	$(CMAKE) --build --preset tsan --parallel $(NPROC)

cross-aarch64:
	$(CMAKE) --preset cross-aarch64
	$(CMAKE) --build --preset cross-aarch64 --parallel $(NPROC)

cross-arm:
	$(CMAKE) --preset cross-arm
	$(CMAKE) --build --preset cross-arm --parallel $(NPROC)

# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------
test: test-debug

test-debug:
	$(CMAKE) --test-preset debug

test-ci:
	$(CMAKE) --test-preset ci

# ---------------------------------------------------------------------------
# Install (prefix=/usr/local by default; override with PREFIX=...)
# ---------------------------------------------------------------------------
install: release
	$(CMAKE) --install build/release \
		$(if $(PREFIX),--prefix $(PREFIX),)

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
package: release
	$(CMAKE) --build --preset release --target package
	@echo ""
	@echo "Packages written to build/release/packages/"

# ---------------------------------------------------------------------------
# Static analysis
# ---------------------------------------------------------------------------
tidy:
	$(CMAKE) --preset static-analysis
	$(CMAKE) --build --preset static-analysis --parallel $(NPROC)

# ---------------------------------------------------------------------------
# Formatting — runs clang-format on all .c/.h/.cpp files
# ---------------------------------------------------------------------------
format:
	@echo "Running clang-format..."
	@find dev -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
		-not -path '*/build/*' \
		| xargs clang-format -i --style=file
	@echo "Done."

format-check:
	@echo "Checking clang-format compliance..."
	@find dev -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
		-not -path '*/build/*' \
		| xargs clang-format --style=file --dry-run --Werror
	@echo "All files are correctly formatted."

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	@echo "Removing build directories..."
	@rm -rf build/debug build/release build/relwithdebinfo \
	         build/ci build/tsan build/msan
	@echo "Done."

distclean: clean
	@echo "Removing install and package directories..."
	@rm -rf install packages
	@echo "Done."

# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------
help:
	@echo ""
	@echo "linux-middleware build targets:"
	@echo ""
	@echo "  make                → build relwithdebinfo (default)"
	@echo "  make debug          → build debug + ASan/UBSan"
	@echo "  make release        → build release + LTO + hardened"
	@echo "  make ci             → build + test (CI preset)"
	@echo "  make tsan           → build with ThreadSanitizer"
	@echo "  make cross-aarch64  → cross-compile for AArch64"
	@echo "  make cross-arm      → cross-compile for ARMv7-A"
	@echo ""
	@echo "  make test           → run tests (debug preset)"
	@echo "  make test-ci        → run tests (ci preset)"
	@echo ""
	@echo "  make install        → install to /usr/local  (PREFIX=... to override)"
	@echo "  make package        → build .deb/.rpm/.tgz packages"
	@echo ""
	@echo "  make format         → auto-format all sources with clang-format"
	@echo "  make format-check   → verify clang-format compliance (CI)"
	@echo "  make tidy           → run clang-tidy static analysis"
	@echo ""
	@echo "  make clean          → remove build directories"
	@echo "  make distclean      → remove build + install + package directories"
	@echo ""
	@echo "Direct cmake usage:"
	@echo "  cmake --list-presets"
	@echo "  cmake --preset <name>"
	@echo "  cmake --build --preset <name>"
	@echo "  cmake --test-preset <name>"
	@echo ""
