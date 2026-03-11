CC       := gcc
CFLAGS   := -Wall -Wextra -Wformat -Wformat-security \
            -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
            -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
            -fPIC -fno-strict-aliasing \
            -I./dev/core \
            -I./dev/core/service_manager \
            -I./dev/core/service_manager/enterprise \
            -I./dev/core/service_manager/infrastructure \
            -I./dev/core/service_manager/security \
            -I./dev/core/service_manager/observability \
            -I./dev/core/service_manager/lifecycle \
            -I./dev/security \
            -I./dev/security/capabilities \
            -I./dev/security/sandbox \
            -I./dev/security/seccomp \
            -I./dev/security/verify \
            -I./dev/security/core \
            -I./dev/hal \
            -I./dev/hal/interface \
            -I./dev/hal/layers/audio \
            -I./dev/hal/layers/camera \
            -I./dev/hal/layers/gpio \
            -I./dev/hal/layers/sensors
LDFLAGS  := -lpthread -lseccomp -lasound -pie \
            -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
SM_DIR   := dev/core/service_manager
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

# Security Layer (Crypto, replay protection, access control, privilege management, seccomp)
SM_SECURITY := $(SM_DIR)/security/sm_crypto.c \
               $(SM_DIR)/security/sm_replay.c \
               $(SM_DIR)/security/sm_rate_limit.c \
               $(SM_DIR)/security/sm_advanced_ratelimit.c \
               $(SM_DIR)/security/sm_security.c

# Platform Security Layer (Capabilities, sandbox, seccomp, verify)
SEC_DIR := dev/security
PLATFORM_SECURITY := $(SEC_DIR)/capabilities/capabilities.c \
                     $(SEC_DIR)/capabilities/capabilities_audit.c \
                     $(SEC_DIR)/capabilities/capabilities_core.c \
                     $(SEC_DIR)/capabilities/capabilities_policy.c \
                     $(SEC_DIR)/sandbox/sandbox.c \
                     $(SEC_DIR)/sandbox/sandbox_cgroup.c \
                     $(SEC_DIR)/sandbox/sandbox_core.c \
                     $(SEC_DIR)/sandbox/sandbox_mount.c \
                     $(SEC_DIR)/sandbox/sandbox_network.c \
                     $(SEC_DIR)/seccomp/seccomp_filter.c \
                     $(SEC_DIR)/seccomp/seccomp_core.c \
                     $(SEC_DIR)/seccomp/seccomp_policy_audio.c \
                     $(SEC_DIR)/seccomp/seccomp_policy_camera.c \
                     $(SEC_DIR)/seccomp/seccomp_policy_sensor.c \
                     $(SEC_DIR)/seccomp/seccomp_policy_network.c \
                     $(SEC_DIR)/seccomp/seccomp_policy_minimal.c \
                     $(SEC_DIR)/verify/verify.c \
                     $(SEC_DIR)/verify/verify_hmac.c \
                     $(SEC_DIR)/verify/verify_replay.c \
                     $(SEC_DIR)/verify/verify_token.c \
                     $(SEC_DIR)/core/security_manager.c

# HAL Layer (Hardware Abstraction)
HAL_DIR := dev/hal
HAL_SRCS := $(HAL_DIR)/interface/hal_interface.c \
            $(HAL_DIR)/layers/audio/audio_hal.c \
            $(HAL_DIR)/layers/camera/camera_hal.c \
            $(HAL_DIR)/layers/gpio/gpio_hal.c \
            $(HAL_DIR)/layers/sensors/sensor_hal.c

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
SM_SRCS  := $(SM_ENTERPRISE) $(SM_INFRASTRUCTURE) $(SM_SECURITY) $(SM_OBSERVABILITY) $(SM_LIFECYCLE) \
            $(PLATFORM_SECURITY) $(HAL_SRCS)

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
	@mkdir -p $(BUILD_DIR)/$(SEC_DIR)/capabilities
	@mkdir -p $(BUILD_DIR)/$(SEC_DIR)/sandbox
	@mkdir -p $(BUILD_DIR)/$(SEC_DIR)/seccomp
	@mkdir -p $(BUILD_DIR)/$(SEC_DIR)/verify
	@mkdir -p $(BUILD_DIR)/$(SEC_DIR)/core
	@mkdir -p $(BUILD_DIR)/$(HAL_DIR)

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

.PHONY: all debug hardened check clean install help analyze

analyze:
	@echo "$(YELLOW)[ANALYZE]$(RESET) Running static analysis..."
	@cppcheck --enable=all --std=c11 --suppress=missingIncludeSystem \
		-I./dev/core -I./dev/security -I./dev/hal \
		$(SM_SRCS) 2>&1 | head -50 || true
	@echo "$(GREEN)✓ Static analysis complete$(RESET)"
