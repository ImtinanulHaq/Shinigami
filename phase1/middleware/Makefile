# Middleware Build System (C++)
# Compiles all components for ARM64

ARCH = arm64
CROSS_COMPILE = aarch64-linux-gnu-
CXX = $(CROSS_COMPILE)g++
AR = $(CROSS_COMPILE)ar

CXXFLAGS = -std=c++17 -march=armv8-a -O2 -Wall -Wextra -static
LDFLAGS = -static

# Source files
DAEMON_SRC = daemon.cpp logger.cpp commands.cpp socket.cpp
CLIENT_SRC = client.cpp socket.cpp logger.cpp

# Object files
DAEMON_OBJ = $(DAEMON_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

# Targets
DAEMON = daemon
CLIENT = client

.PHONY: all clean

all: $(DAEMON) $(CLIENT)

$(DAEMON): $(DAEMON_OBJ)
	@echo "Linking daemon..."
	$(CXX) $(LDFLAGS) -o $@ $^
	@echo "✓ Daemon built: $@"

$(CLIENT): $(CLIENT_OBJ)
	@echo "Linking client..."
	$(CXX) $(LDFLAGS) -o $@ $^
	@echo "✓ Client built: $@"

%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	@echo "Cleaning..."
	rm -f $(DAEMON) $(CLIENT) *.o
	@echo "✓ Clean complete"

help:
	@echo "Middleware Build Targets (C++):"
	@echo "  all    - Build daemon and client"
	@echo "  clean  - Remove built files"
	@echo "  help   - Show this message"
