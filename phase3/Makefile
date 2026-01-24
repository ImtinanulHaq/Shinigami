# Phase 3 Makefile - Professional Embedded OS with Event-Driven Microservices
# Build: ARM64 static binaries
# Architecture: Event-driven microservices, capability-based security, professional GUI

CC = aarch64-linux-gnu-gcc
CXX = aarch64-linux-gnu-g++
STRIP = aarch64-linux-gnu-strip

# Compiler flags for professional production build
CXXFLAGS = -std=c++17 \
           -Wall -Wextra -Werror \
           -O3 \
           -march=armv8-a \
           -fstack-protector-strong \
           -fPIE \
           -ffunction-sections \
           -fdata-sections \
           -pthread

LDFLAGS = -static \
          -static-libgcc \
          -static-libstdc++ \
          -Wl,--gc-sections \
          -Wl,-Map=$(BUILD_DIR)/phase3.map

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Source files - organized by module
EVENT_SOURCES = \
	core/events/event_types.hpp \
	core/events/event_bus.hpp \
	core/events/event_bus.cpp

CAPABILITY_SOURCES = \
	core/capabilities/capability.hpp \
	core/capabilities/capability.cpp

SERVICE_SOURCES = \
	core/services/microservice.hpp \
	core/services/microservice.cpp \
	core/services/service_registry.hpp \
	core/services/service_registry.cpp

GRAPHICS_SOURCES = \
	utilities/graphics/graphics_types.hpp \
	utilities/graphics/graphics_types.cpp

THEME_SOURCES = \
	gui/themes/theme.hpp \
	gui/themes/theme.cpp

WIDGET_SOURCES = \
	gui/framework/widget.hpp \
	gui/framework/widget.cpp

WINDOW_SOURCES = \
	gui/framework/window.hpp \
	gui/framework/window.cpp

ANIMATION_SOURCES = \
	gui/effects/animation.hpp \
	gui/effects/animation.cpp

COMPONENT_SOURCES = \
	gui/components/gui_components.hpp \
	gui/components/gui_components.cpp

MAIN_SOURCES = main.cpp

# Combine all sources
ALL_SOURCES = $(EVENT_SOURCES) $(CAPABILITY_SOURCES) $(SERVICE_SOURCES) \
              $(GRAPHICS_SOURCES) $(THEME_SOURCES) $(WIDGET_SOURCES) \
              $(WINDOW_SOURCES) $(ANIMATION_SOURCES) $(COMPONENT_SOURCES)

# Extract only .cpp files for compilation
CPP_SOURCES = $(filter %.cpp, $(ALL_SOURCES)) $(MAIN_SOURCES)

# Object files
OBJECTS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CPP_SOURCES))

# Target binaries
DAEMON_TARGET = $(BIN_DIR)/phase3-daemon
CLIENT_TARGET = $(BIN_DIR)/phase3-client

# Default target
all: $(DAEMON_TARGET) $(CLIENT_TARGET)
	@echo "==================================="
	@echo "Phase 3 Build Complete"
	@echo "Daemon: $(DAEMON_TARGET)"
	@echo "Client: $(CLIENT_TARGET)"
	@echo "==================================="

# Daemon binary (main event-driven system)
$(DAEMON_TARGET): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking daemon: $@"
	$(CXX) $(LDFLAGS) -o $@ $^ -lpthread
	@$(STRIP) --strip-all $@
	@ls -lh $@

# Client binary (GUI/interface)
$(CLIENT_TARGET): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking client: $@"
	$(CXX) $(LDFLAGS) -o $@ $^ -lpthread
	@$(STRIP) --strip-all $@
	@ls -lh $@

# Object file compilation
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning Phase 3 build..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Full rebuild
rebuild: clean all

# Static analysis (if clang-tidy available)
analyze:
	@echo "Running static analysis..."
	@for f in $(CPP_SOURCES); do \
		echo "Analyzing $$f..."; \
		clang-tidy $$f -- -std=c++17 || true; \
	done

# Source code documentation
docs:
	@echo "Generating documentation..."
	@doxygen 2>/dev/null || echo "Doxygen not installed"

# Display build info
info:
	@echo "Phase 3 Build Configuration"
	@echo "==================================="
	@echo "Compiler: $(CXX)"
	@echo "Architecture: ARM64"
	@echo "Build Type: Static Release"
	@echo "C++ Standard: C++17"
	@echo "==================================="
	@echo "Components:"
	@echo "  - Event Bus (publish/subscribe)"
	@echo "  - Capability Manager (40+ capabilities)"
	@echo "  - Microservice Framework"
	@echo "  - Professional GUI System"
	@echo "  - Theme System (light/dark)"
	@echo "  - Animation Engine (8+ easing)"
	@echo "  - Widget Framework"
	@echo "  - Service Registry"
	@echo "==================================="

.PHONY: all clean rebuild analyze docs info
