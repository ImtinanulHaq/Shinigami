CC       := gcc
CFLAGS   := -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L \
            -I./core \
            -I./core/service_manager \
            -I./core/service_manager/enterprise \
            -I./core/service_manager/infrastructure \
            -I./core/service_manager/security \
            -I./core/service_manager/observability \
            -I./core/service_manager/lifecycle
LDFLAGS  := -lpthread

SM_DIR   := core/service_manager
BUILD_DIR := build

# ============================================================================
# Service Manager Source Files - Organized by Functional Layer
# ============================================================================

# Enterprise Features (10 new features + integration layer)
SM_ENTERPRISE := $(SM_DIR)/enterprise/sm_threadpool.c \
                 $(SM_DIR)/enterprise/sm_discovery.c \
                 $(SM_DIR)/enterprise/sm_watchdog.c \
                 $(SM_DIR)/enterprise/sm_monitoring.c \
                 $(SM_DIR)/enterprise/sm_tls.c \
                 $(SM_DIR)/enterprise/sm_rolling_restart.c \
                 $(SM_DIR)/enterprise/sm_plugin.c \
                 $(SM_DIR)/enterprise/sm_container.c \
                 $(SM_DIR)/enterprise/sm_eventbus.c \
                 $(SM_DIR)/enterprise/sm_cli.c \
                 $(SM_DIR)/enterprise/sm_main_integration.c

# Infrastructure Layer (Core IPC & protocol)
SM_INFRASTRUCTURE := $(SM_DIR)/infrastructure/sm_socket.c \
                     $(SM_DIR)/infrastructure/sm_registry.c \
                     $(SM_DIR)/infrastructure/sm_handlers.c \
                     $(SM_DIR)/infrastructure/sm_protocol.c \
                     $(SM_DIR)/infrastructure/sm_connection_pool.c \
                     $(SM_DIR)/infrastructure/sm_request_id.c

# Security Layer (Crypto, access control, privilege management, seccomp)
SM_SECURITY := $(SM_DIR)/security/sm_crypto.c \
               $(SM_DIR)/security/sm_rate_limit.c \
               $(SM_DIR)/security/sm_advanced_ratelimit.c \
               $(SM_DIR)/security/sm_security.c

# Observability Layer (Health, monitoring, logging, audit)
SM_OBSERVABILITY := $(SM_DIR)/observability/sm_health.c \
                    $(SM_DIR)/observability/sm_health_callbacks.c \
                    $(SM_DIR)/observability/sm_metrics.c \
                    $(SM_DIR)/observability/sm_audit.c \
                    $(SM_DIR)/observability/sm_logging.c \
                    $(SM_DIR)/observability/sm_structured_log.c

# Lifecycle Layer (Service lifecycle & configuration)
SM_LIFECYCLE := $(SM_DIR)/lifecycle/sm_main.c \
                $(SM_DIR)/lifecycle/sm_graceful_shutdown.c \
                $(SM_DIR)/lifecycle/sm_config.c \
                $(SM_DIR)/lifecycle/sm_management.c \
                $(SM_DIR)/lifecycle/sm_dependencies.c \
                $(SM_DIR)/lifecycle/sm_service_tier.c \
                $(SM_DIR)/lifecycle/sm_persistence.c

# Aggregate all sources
SM_SRCS  := $(SM_ENTERPRISE) $(SM_INFRASTRUCTURE) $(SM_SECURITY) $(SM_OBSERVABILITY) $(SM_LIFECYCLE)

SM_OBJS  := $(patsubst %.c, $(BUILD_DIR)/%.o, $(SM_SRCS))
TARGET   := servicemanager

GREEN  := \033[32m
YELLOW := \033[33m
RESET  := \033[0m

# ============================================================================

all: $(TARGET)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)/$(SM_DIR)/enterprise
	@mkdir -p $(BUILD_DIR)/$(SM_DIR)/infrastructure
	@mkdir -p $(BUILD_DIR)/$(SM_DIR)/security
	@mkdir -p $(BUILD_DIR)/$(SM_DIR)/observability
	@mkdir -p $(BUILD_DIR)/$(SM_DIR)/lifecycle

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "$(GREEN)[CC]$(RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(SM_OBJS)
	@echo "$(GREEN)[LD]$(RESET) $@"
	@$(CC) $(SM_OBJS) -o $@ $(LDFLAGS)
	@echo "$(GREEN)✓ Built: $@$(RESET)"

debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

hardened: CFLAGS += -fPIE
hardened: LDFLAGS += -pie
hardened: $(TARGET)

check: $(TARGET)
	@file $(TARGET) && echo "$(GREEN)✓ Binary OK$(RESET)"

clean:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "$(GREEN)✓ Cleaned$(RESET)"

install: $(TARGET)
	@install -m 755 $(TARGET) /usr/local/bin/
	@echo "$(GREEN)✓ Installed to /usr/local/bin$(RESET)"

help:
	@echo "$(GREEN)Service Manager$(RESET)"
	@echo "  make         - Build binary"
	@echo "  make debug   - Build with debug"
	@echo "  make hardened - Build hardened PIE"
	@echo "  make clean   - Remove artifacts"
	@echo "  make check   - Validate binary"
	@echo "  make install - Install to /usr/local/bin"

.PHONY: all debug hardened check clean install help
