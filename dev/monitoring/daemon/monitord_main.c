/**
 * @file    monitord_main.c
 * @brief   Entry point for middleware_monitord daemon.
 *
 * Usage:
 *   middleware_monitord [--config /path/to/monitord.ini]
 *
 * Architecture:
 *   1. Load configuration (monitord_config_load)
 *   2. Initialize state (monitord_state_init)
 *   3. Start collectors with staggered startup (collector_start_all)
 *   4. Start Unix socket server (monitord_server_start)
 *   5. Start HTTP server (monitord_http_start)
 *   6. Main loop: serialize snapshot + broadcast every refresh_interval_ms
 *   7. Signal handling: SIGINT/SIGTERM → clean shutdown
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "monitord_config.h"
#include "monitord_state.h"
#include "monitord_server.h"
#include "monitord_http.h"
#include "../collectors/collector_base.h"

/* Import all collector registration functions */
#include "../collectors/collector_sysinfo.h"
#include "../collectors/collector_processes.h"
#include "../collectors/collector_sm.h"
#include "../collectors/collector_watchdog.h"
#include "../collectors/collector_hal.h"
#include "../collectors/collector_services.h"
#include "../collectors/collector_memory_pool.h"
#include "../collectors/collector_io_uring.h"
#include "../collectors/collector_ring_buffer.h"
#include "../collectors/collector_security.h"
#include "../collectors/collector_proxy.h"
#include "../collectors/collector_ipc_channels.h"
#include "../collectors/collector_config_watcher.h"

#include "../alerts/alert_notify.h"
#include "../alerts/alert_rules.h"

static volatile sig_atomic_t g_stop_flag = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_stop_flag = 1;
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
    const char *config_path = NULL;

    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--config /path/to/monitord.ini]\n", argv[0]);
            return 0;
        }
    }

    /* Load configuration */
    monitord_config_t config;
    if (monitord_config_load(&config, config_path) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    printf("[monitord] Starting with config:\n");
    printf("  Unix socket: %s\n", config.unix_socket_path);
    printf("  HTTP port: %u\n", config.http_port);
    printf("  Refresh interval: %u ms\n", config.refresh_interval_ms);

    /* Initialize state */
    monitord_state_t state;
    if (monitord_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize state\n");
        return 1;
    }

    /* Register all collectors */
    collector_sysinfo_register();
    collector_processes_register();
    collector_sm_register();
    collector_watchdog_register();
    collector_hal_register();
    collector_services_register();
    collector_memory_pool_register();
    collector_io_uring_register();
    collector_ring_buffer_register();
    collector_security_register();
    collector_proxy_register();
    collector_ipc_channels_register();
    collector_config_watcher_register();

    /* Start all collectors (with staggered startup) */
    if (collector_start_all(&state) != 0) {
        fprintf(stderr, "Failed to start collectors\n");
        monitord_state_destroy(&state);
        return 1;
    }

    /* Initialize alert notification */
    alert_notify_init(config.alert_log_path, config.webhook_url);

    /* Start Unix socket server */
    monitord_server_t *server = monitord_server_start(&config, &state);
    if (!server) {
        fprintf(stderr, "Failed to start Unix socket server\n");
        collector_stop_all(5000);
        monitord_state_destroy(&state);
        return 1;
    }

    /* Start HTTP server (non-fatal — TUI uses Unix socket, not HTTP) */
    monitord_http_t *http = monitord_http_start(&config, &state);
    if (!http) {
        fprintf(stderr, "[monitord] WARNING: HTTP server failed to start (port %u in use?) "
                "— continuing without Prometheus metrics\n", config.http_port);
    }

    /* Install signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("[monitord] Running (Unix socket: %s). Press Ctrl+C to stop.\n",
           config.unix_socket_path);

    /* Main loop */
    while (!g_stop_flag) {
        uint64_t loop_start = monotonic_ms();

        /* Serialize snapshot */
        mon_snapshot_t snapshot;
        monitord_state_serialize_snapshot(&state, &snapshot);

        /* Evaluate alert rules */
        alert_rules_evaluate(&snapshot, &state.alert_state, loop_start);

        /* Broadcast to TUI clients */
        monitord_server_broadcast_snapshot(server, &snapshot);

        /* Sleep for refresh interval */
        uint64_t loop_end = monotonic_ms();
        uint64_t elapsed = loop_end - loop_start;
        if (elapsed < config.refresh_interval_ms) {
            sleep_ms(config.refresh_interval_ms - (uint32_t)elapsed);
        }
    }

    printf("\n[monitord] Shutting down...\n");

    /* Cleanup */
    monitord_http_stop(http);
    monitord_server_stop(server);
    collector_stop_all(5000);
    alert_notify_shutdown();
    monitord_state_destroy(&state);

    printf("[monitord] Stopped.\n");
    return 0;
}
