#define _POSIX_C_SOURCE 200809L

/*
 * sm_main_integration.c - Integration of all 10 enterprise features
 *
 * Comprehensive initialization and cleanup with proper error handling
 */

#include "../enterprise/sm_main_integration.h"
#include "../observability/sm_logging.h"

/* Import headers for all 10 features */
#include "../enterprise/sm_threadpool.h"
#include "../enterprise/sm_discovery.h"
#include "../enterprise/sm_watchdog.h"
#include "../enterprise/sm_monitoring.h"
#include "../enterprise/sm_tls.h"
#include "../enterprise/sm_rolling_restart.h"
#include "../enterprise/sm_plugin.h"
#include "../enterprise/sm_container.h"
#include "../enterprise/sm_eventbus.h"
#include "../enterprise/sm_cli.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Feature initialization state */
typedef struct {
    int threadpool_initialized;
    int discovery_initialized;
    int watchdog_initialized;
    int monitoring_initialized;
    int tls_initialized;
    int rolling_restart_initialized;
    int plugin_initialized;
    int container_initialized;
    int eventbus_initialized;
    int cli_initialized;
} feature_state_t;

static feature_state_t g_features = {0};

int sm_main_init_all_features(void)
{
    int init_success_count = 0;
    int init_failure_count = 0;
    
    sm_log(SM_LOG_INFO, "=== INITIALIZING ALL 10 ENTERPRISE FEATURES ===");
    
    /* 1. Initialize Thread Pool */
    sm_log(SM_LOG_INFO, "[1/10] Initializing Thread Pool...");
    if (sm_threadpool_init(8, 128) == 0) {
        g_features.threadpool_initialized = 1;
        sm_log(SM_LOG_INFO, "✓ Thread Pool initialized (8 workers, 128 queue)");
        init_success_count++;
    } else {
        sm_log(SM_LOG_ERROR, "✗ Thread Pool initialization FAILED");
        init_failure_count++;
    }
    
    /* 2. Initialize Service Discovery */
    sm_log(SM_LOG_INFO, "[2/10] Initializing Service Discovery...");
    if (sm_discovery_init(256) == 0) {
        g_features.discovery_initialized = 1;
        sm_log(SM_LOG_INFO, "✓ Service Discovery initialized (max 256 subscribers)");
        init_success_count++;
    } else {
        sm_log(SM_LOG_ERROR, "✗ Service Discovery initialization FAILED");
        init_failure_count++;
    }
    
    /* 3. Initialize Watchdog Timer */
    sm_log(SM_LOG_INFO, "[3/10] Initializing Watchdog Timer...");
    if (sm_watchdog_init() == 0) {
        g_features.watchdog_initialized = 1;
        sm_log(SM_LOG_INFO, "✓ Watchdog Timer initialized");
        init_success_count++;
    } else {
        /* Non-fatal if watchdog unavailable */
        sm_log(SM_LOG_WARN, "⚠ Watchdog Timer initialization failed (continuing without watchdog)");
        init_failure_count++;
    }
    
    /* 4. Initialize Resource Monitoring */
    sm_log(SM_LOG_INFO, "[4/10] Initializing Resource Monitoring...");
    if (sm_monitoring_init("service_manager") == 0) {
        g_features.monitoring_initialized = 1;
        sm_monitoring_set_memory_threshold(512 * 1024 * 1024);  /* 512 MB */
        sm_monitoring_set_fd_threshold(256);
        sm_log(SM_LOG_INFO, "✓ Resource Monitoring initialized");
        init_success_count++;
    } else {
        sm_log(SM_LOG_ERROR, "✗ Resource Monitoring initialization FAILED");
        init_failure_count++;
    }
    
    /* 5. Initialize TLS Transport */
    sm_log(SM_LOG_INFO, "[5/10] Initializing TLS Transport...");
    if (sm_tls_init(NULL, NULL) == 0) {
        g_features.tls_initialized = 1;
        sm_log(SM_LOG_INFO, "✓ TLS Transport initialized (passthrough mode)");
        init_success_count++;
    } else {
        sm_log(SM_LOG_WARN, "⚠ TLS Transport initialization failed (continuing without TLS)");
        init_failure_count++;
    }
    
    /* 6. Initialize Rolling Restart */
    sm_log(SM_LOG_INFO, "[6/10] Initializing Rolling Restart System...");
    if (sm_rolling_restart_init(1) == 0) {
        g_features.rolling_restart_initialized = 1;
        sm_log(SM_LOG_INFO, "✓ Rolling Restart initialized (1 concurrent restart)");
        init_success_count++;
    } else {
        sm_log(SM_LOG_ERROR, "✗ Rolling Restart initialization FAILED");
        init_failure_count++;
    }
    
    /* 7. Initialize Plugin System */
    sm_log(SM_LOG_INFO, "[7/10] Initializing Plugin System...");
    if (sm_plugin_init("/etc/servicemanager/plugins/") == 0) {
        g_features.plugin_initialized = 1;
        sm_plugin_set_execution_timeout(30);
        sm_log(SM_LOG_INFO, "✓ Plugin System initialized (dir=/etc/servicemanager/plugins/)");
        init_success_count++;
    } else {
        sm_log(SM_LOG_WARN, "⚠ Plugin System initialization failed (continuing without plugins)");
        init_failure_count++;
    }
    
    /* 8. Initialize Container Support */
    sm_log(SM_LOG_INFO, "[8/10] Initializing Container Support...");
    if (sm_container_init() == 0) {
        g_features.container_initialized = 1;
        sm_container_set_namespace_path("/proc");
        sm_log(SM_LOG_INFO, "✓ Container Support initialized");
        init_success_count++;
    } else {
        sm_log(SM_LOG_ERROR, "✗ Container Support initialization FAILED");
        init_failure_count++;
    }
    
    /* 9. Initialize Event Bus */
    sm_log(SM_LOG_INFO, "[9/10] Initializing Event Bus...");
    if (sm_eventbus_init(512) == 0) {
        g_features.eventbus_initialized = 1;
        sm_log(SM_LOG_INFO, "✓ Event Bus initialized (max 512 subs/event)");
        init_success_count++;
    } else {
        sm_log(SM_LOG_ERROR, "✗ Event Bus initialization FAILED");
        init_failure_count++;
    }
    
    /* 10. Initialize CLI Interface */
    sm_log(SM_LOG_INFO, "[10/10] Initializing CLI Interface...");
    /* CLI doesn't need initialization, just note it */
    g_features.cli_initialized = 1;
    sm_log(SM_LOG_INFO, "✓ CLI Interface ready");
    init_success_count++;
    
    /* Summary */
    sm_log(SM_LOG_INFO, "===========================================");
    sm_log(SM_LOG_INFO, "FEATURE INITIALIZATION COMPLETE");
    sm_log(SM_LOG_INFO, "  Succeeded: %d/10", init_success_count);
    sm_log(SM_LOG_INFO, "  Failed: %d/10", init_failure_count);
    sm_log(SM_LOG_INFO, "===========================================");
    
    /* Return success if most features initialized */
    return (init_failure_count <= 3) ? 0 : -1;
}

int sm_main_cleanup_all_features(void)
{
    int cleanup_success_count = 0;
    
    sm_log(SM_LOG_INFO, "=== CLEANING UP ALL 10 ENTERPRISE FEATURES ===");
    
    /* 1. Cleanup Thread Pool */
    if (g_features.threadpool_initialized) {
        sm_log(SM_LOG_INFO, "[1/10] Cleaning up Thread Pool...");
        sm_threadpool_shutdown(10);  /* Wait 10 seconds for graceful shutdown */
        sm_log(SM_LOG_INFO, "✓ Thread Pool cleanup complete");
        cleanup_success_count++;
    }
    
    /* 2. Cleanup Service Discovery */
    if (g_features.discovery_initialized) {
        sm_log(SM_LOG_INFO, "[2/10] Cleaning up Service Discovery...");
        sm_discovery_cleanup();
        sm_log(SM_LOG_INFO, "✓ Service Discovery cleanup complete");
        cleanup_success_count++;
    }
    
    /* 3. Cleanup Watchdog Timer */
    if (g_features.watchdog_initialized) {
        sm_log(SM_LOG_INFO, "[3/10] Cleaning up Watchdog Timer...");
        sm_watchdog_cleanup();
        sm_log(SM_LOG_INFO, "✓ Watchdog Timer cleanup complete");
        cleanup_success_count++;
    }
    
    /* 4. Cleanup Resource Monitoring */
    if (g_features.monitoring_initialized) {
        sm_log(SM_LOG_INFO, "[4/10] Cleaning up Resource Monitoring...");
        sm_monitoring_cleanup();
        sm_log(SM_LOG_INFO, "✓ Resource Monitoring cleanup complete");
        cleanup_success_count++;
    }
    
    /* 5. Cleanup TLS Transport */
    if (g_features.tls_initialized) {
        sm_log(SM_LOG_INFO, "[5/10] Cleaning up TLS Transport...");
        sm_tls_cleanup();
        sm_log(SM_LOG_INFO, "✓ TLS Transport cleanup complete");
        cleanup_success_count++;
    }
    
    /* 6. Cleanup Rolling Restart */
    if (g_features.rolling_restart_initialized) {
        sm_log(SM_LOG_INFO, "[6/10] Cleaning up Rolling Restart System...");
        sm_rolling_restart_cleanup();
        sm_log(SM_LOG_INFO, "✓ Rolling Restart cleanup complete");
        cleanup_success_count++;
    }
    
    /* 7. Cleanup Plugin System */
    if (g_features.plugin_initialized) {
        sm_log(SM_LOG_INFO, "[7/10] Cleaning up Plugin System...");
        sm_plugin_cleanup();
        sm_log(SM_LOG_INFO, "✓ Plugin System cleanup complete");
        cleanup_success_count++;
    }
    
    /* 8. Cleanup Container Support */
    if (g_features.container_initialized) {
        sm_log(SM_LOG_INFO, "[8/10] Cleaning up Container Support...");
        sm_container_cleanup();
        sm_log(SM_LOG_INFO, "✓ Container Support cleanup complete");
        cleanup_success_count++;
    }
    
    /* 9. Cleanup Event Bus */
    if (g_features.eventbus_initialized) {
        sm_log(SM_LOG_INFO, "[9/10] Cleaning up Event Bus...");
        sm_eventbus_cleanup();
        sm_log(SM_LOG_INFO, "✓ Event Bus cleanup complete");
        cleanup_success_count++;
    }
    
    /* 10. Cleanup CLI Interface */
    if (g_features.cli_initialized) {
        sm_log(SM_LOG_INFO, "[10/10] Cleaning up CLI Interface...");
        sm_cli_disconnect();
        sm_log(SM_LOG_INFO, "✓ CLI Interface cleanup complete");
        cleanup_success_count++;
    }
    
    /* Summary */
    sm_log(SM_LOG_INFO, "===========================================");
    sm_log(SM_LOG_INFO, "ALL FEATURES CLEANED UP");
    sm_log(SM_LOG_INFO, "  Cleaned: %d/10", cleanup_success_count);
    sm_log(SM_LOG_INFO, "===========================================");
    
    memset(&g_features, 0, sizeof(g_features));
    
    return 0;
}
