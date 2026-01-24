# ============================================================================
# MicroOS Phase 2 - Makefile
# Middleware Process Manager with C++17
# ARM64 Cross-Compilation for QEMU virt machine
# ============================================================================

# ============================================================================
# Compiler Configuration
# ============================================================================
ARCH = arm64
CROSS_COMPILE = aarch64-linux-gnu-
CXX = $(CROSS_COMPILE)g++
CXXFLAGS = -std=c++17 -march=armv8-a -O2 -Wall -Wextra -static
LDFLAGS = -static
RM = rm -f

# ============================================================================
# Project Structure
# ============================================================================
SRCDIR = .
OBJDIR = ../build
BINDIR = ../build

# ============================================================================
# Source Files
# ============================================================================
SOURCES = \
	process_manager.cpp \
	app_registry.cpp \
	lifecycle_manager.cpp \
	service_discovery.cpp \
	commands_v2.cpp \
	daemon.cpp

# ============================================================================
# Object Files
# ============================================================================
OBJECTS = $(SOURCES:.cpp=.o)
OBJS = $(addprefix $(OBJDIR)/, $(OBJECTS))

# ============================================================================
# Binaries
# ============================================================================
DAEMON = $(BINDIR)/daemon
CLIENT = $(BINDIR)/client

# ============================================================================
# Default Target
# ============================================================================
all: $(DAEMON) $(CLIENT)
	@echo "✓ Phase 2 build complete"

# ============================================================================
# Build Daemon Binary
# ============================================================================
$(DAEMON): daemon.o process_manager.o app_registry.o lifecycle_manager.o service_discovery.o commands_v2.o
	@echo "Linking daemon..."
	$(CXX) $(LDFLAGS) -o $(DAEMON) daemon.o process_manager.o app_registry.o lifecycle_manager.o service_discovery.o commands_v2.o
	@echo "✓ Daemon built: $(DAEMON)"

# ============================================================================
# Build Client Binary
# ============================================================================
$(CLIENT): client.o process_manager.o app_registry.o service_discovery.o commands_v2.o lifecycle_manager.o
	@echo "Linking client..."
	$(CXX) $(LDFLAGS) -o $(CLIENT) client.o process_manager.o app_registry.o service_discovery.o commands_v2.o lifecycle_manager.o
	@echo "✓ Client built: $(CLIENT)"

# ============================================================================
# Compilation Rules
# ============================================================================
%.o: %.cpp
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# ============================================================================
# Clean Targets
# ============================================================================
clean:
	@echo "Cleaning object files..."
	$(RM) *.o
	@echo "✓ Clean complete"

clean-all: clean
	@echo "Removing binaries..."
	$(RM) $(DAEMON) $(CLIENT)
	@echo "✓ Full cleanup complete"

# ============================================================================
# Help Target
# ============================================================================
help:
	@echo "================================"
	@echo "MicroOS Phase 2 - Makefile"
	@echo "================================"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build daemon and client (default)"
	@echo "  clean      - Remove object files"
	@echo "  clean-all  - Remove all build artifacts"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  ARCH:          $(ARCH)"
	@echo "  COMPILER:      $(CXX)"
	@echo "  CXXFLAGS:      $(CXXFLAGS)"
	@echo ""
	@echo "Output:"
	@echo "  Daemon:        $(DAEMON)"
	@echo "  Client:        $(CLIENT)"

.PHONY: all clean clean-all help
