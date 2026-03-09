/**
 * @file sensor_service_main.c
 * @brief Sensor service daemon entry point.
 *
 * Mandatory 11-step startup and graceful shutdown as per the Service Layer
 * specification.
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/service_base.h"
#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "sensor_service.h"
#include "sensor_service_hal.h"
#include "sensor_service_security.h"
#include "sensor_service_loop.h"

static sensor_service_ctx_t  g_ctx;
static service_config_t      g_cfg;
static svc_ipc_t             g_ipc;

static const char *SHORT_OPTS = "c:fvh";

static const struct option LONG_OPTS[] = {
    { "config",     required_argument, NULL, 'c' },
    { "foreground", no_argument,       NULL, 'f' },
    { "verbose",    no_argument,       NULL, 'v' },
    { "help",       no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 }
};

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -c, --config FILE      Path to config file (required)\n"
        "  -f, --foreground       Run in foreground (skip daemonize)\n"
        "  -v, --verbose          Enable DEBUG-level logging\n"
        "  -h, --help             Show this help\n",
        prog);
}

static const cfg_required_t REQUIRED_KEYS[] = {
    { "server",   "socket_path"      },
    { "server",   "log_file"         },
    { "hardware", "device_path"      },
    { "hardware", "sensor_type"      },
    { "security", "verify_key_file"  },
    { NULL, NULL }
};

static void do_shutdown(sensor_service_ctx_t *ctx, svc_ipc_t *ipc,
                        const char *svc_name, int hal_started)
{
    ctx->base.state = SVC_STATE_STOPPING;

    if (ctx->hal_device) {
        hw_device_t *dev = (hw_device_t *)ctx->hal_device;
        if (hal_started && dev->ops) {
            dev->ops->stop(dev);
            dev->ops->close(dev);
        }
        sensor_hal_destroy(dev);
        ctx->hal_device = NULL;
    }

    if (service_ipc_is_connected(ipc)) {
        service_ipc_unregister(ipc);
        service_ipc_disconnect(ipc);
    }

    service_base_remove_pid(svc_name);
    LOG_INFO("sensor_service exiting.");
    service_base_close_log();
    ctx->base.state = SVC_STATE_STOPPED;
}

int main(int argc, char *argv[])
{
    const char *config_path = NULL;
    int foreground = 0, verbose = 0;
    int opt;

    /* ── Step 1: Argument parsing ─────────────────────────────────────── */
    while ((opt = getopt_long(argc, argv, SHORT_OPTS, LONG_OPTS, NULL)) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'f': foreground  = 1;      break;
        case 'v': verbose     = 1;      break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (!config_path) {
        fprintf(stderr, "sensor_service: -c <config_file> is required\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── Step 2: Config loading ───────────────────────────────────────── */
    if (service_config_load(&g_cfg, config_path, "SENSOR_SERVICE") != 0) {
        fprintf(stderr, "sensor_service: failed to load config '%s'\n",
                config_path);
        return EXIT_FAILURE;
    }
    if (service_config_validate(&g_cfg, REQUIRED_KEYS) != 0) {
        fprintf(stderr, "sensor_service: config validation failed\n");
        return EXIT_FAILURE;
    }

    service_base_init(&g_ctx.base, "sensor_service");
    strncpy(g_ctx.base.config_path, config_path,
            sizeof(g_ctx.base.config_path) - 1);
    g_ctx.base.foreground = foreground;
    g_ctx.base.verbose    = verbose;
    strncpy(g_ctx.base.log_path,
            service_config_get_string(&g_cfg, "server", "log_file",
                                      "/var/log/sensor_service.log"),
            sizeof(g_ctx.base.log_path) - 1);

    strncpy(g_ctx.iio_device,
            service_config_get_string(&g_cfg, "hardware", "device_path",
                                      "iio:device0"),
            sizeof(g_ctx.iio_device) - 1);
    g_ctx.sampling_rate_hz = service_config_get_uint32(&g_cfg, "hardware",
                                  "sampling_rate_hz", 100);
    g_ctx.enable_buffer    = service_config_get_bool(&g_cfg, "hardware",
                                  "enable_buffer",    0);

    /* Map sensor_type string to integer code */
    const char *stype = service_config_get_string(&g_cfg, "hardware",
                            "sensor_type", "accel");
    if      (strcasecmp(stype, "accel") == 0) g_ctx.sensor_type = 1;
    else if (strcasecmp(stype, "gyro")  == 0) g_ctx.sensor_type = 2;
    else if (strcasecmp(stype, "mag")   == 0) g_ctx.sensor_type = 3;
    else {
        fprintf(stderr, "sensor_service: unknown sensor_type '%s' "
                        "(valid: accel, gyro, mag)\n", stype);
        return EXIT_FAILURE;
    }

    /* ── Step 3: Daemonize ────────────────────────────────────────────── */
    if (!foreground) {
        if (service_base_daemonize(&g_ctx.base) != SVC_OK) {
            fprintf(stderr, "sensor_service: daemonize failed\n");
            return EXIT_FAILURE;
        }
    }

    /* ── Step 4: PID file ─────────────────────────────────────────────── */
    service_base_open_log("sensor_service", g_ctx.base.log_path);

    int rc = service_base_write_pid("sensor_service");
    if (rc == SVC_ERR_ALREADY) {
        LOG_ERR("duplicate instance detected — exiting");
        service_base_close_log();
        return EXIT_FAILURE;
    }
    if (rc != SVC_OK) {
        LOG_ERR("PID file error: %s", svc_error_string(rc));
        service_base_close_log();
        return EXIT_FAILURE;
    }
    g_ctx.base.pid = getpid();

    /* ── Step 5: Logging already open ────────────────────────────────── */
    LOG_INFO("sensor_service v%s starting (pid=%d, type=%s)",
             SERVICE_LAYER_VERSION_STR, (int)g_ctx.base.pid, stype);

    /* ── Step 6: Signal handlers ──────────────────────────────────────── */
    if (service_base_install_signals() != SVC_OK) {
        LOG_ERR("failed to install signal handlers");
        do_shutdown(&g_ctx, &g_ipc, "sensor_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 7: Connect to Service Manager ──────────────────────────── */
    const char *sm_sock = service_config_get_string(&g_cfg, "server",
                              "socket_path", SM_SOCKET_PATH);
    const char *vkey    = service_config_get_string(&g_cfg, "security",
                              "verify_key_file", NULL);

    service_ipc_init(&g_ipc, "sensor_service", vkey);
    strncpy(g_ipc.socket_path, sm_sock, SERVICE_MAX_PATH - 1);

    int connected = 0;
    for (int attempt = 1; attempt <= SM_CONNECT_RETRIES && !connected; attempt++) {
        if (attempt > 1) {
            LOG_WARN("SM connect attempt %d/%d...", attempt, SM_CONNECT_RETRIES);
            sleep(SM_CONNECT_RETRY_DELAY);
        }
        if (service_ipc_connect(&g_ipc) == SVC_OK)
            connected = 1;
    }
    if (!connected) {
        LOG_ERR("cannot connect to SM at %s after %d retries",
                sm_sock, SM_CONNECT_RETRIES);
        do_shutdown(&g_ctx, &g_ipc, "sensor_service", 0);
        return EXIT_FAILURE;
    }
    if (service_ipc_register(&g_ipc, g_ctx.base.exe_path,
                             SERVICE_LAYER_VERSION_STR, g_ctx.base.pid)
            != SVC_OK) {
        LOG_ERR("SM registration failed");
        do_shutdown(&g_ctx, &g_ipc, "sensor_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 8: Apply Security Module ───────────────────────────────── */
    if (sensor_service_security_apply(&g_ctx, &g_cfg) != SVC_OK) {
        LOG_ERR("security apply failed — aborting");
        do_shutdown(&g_ctx, &g_ipc, "sensor_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 9: Initialise HAL ──────────────────────────────────────── */
    if (sensor_service_hal_init(&g_ctx) != SVC_OK) {
        LOG_ERR("HAL init failed");
        do_shutdown(&g_ctx, &g_ipc, "sensor_service", 0);
        return EXIT_FAILURE;
    }
    if (sensor_service_hal_start(&g_ctx) != SVC_OK) {
        LOG_ERR("HAL start failed");
        do_shutdown(&g_ctx, &g_ipc, "sensor_service", 0);
        return EXIT_FAILURE;
    }
    LOG_INFO("sensor HAL on %s started (type=%s, rate=%u Hz)",
             g_ctx.iio_device, stype, g_ctx.sampling_rate_hz);

    g_ctx.base.state = SVC_STATE_RUNNING;

    /* ── Step 10: Event loop ─────────────────────────────────────────── */
    rc = sensor_service_loop_run(&g_ctx, &g_ipc, &g_cfg);
    if (rc != SVC_OK)
        LOG_ERR("event loop exited with error: %s", svc_error_string(rc));

    /* ── Step 11: Graceful shutdown ──────────────────────────────────── */
    do_shutdown(&g_ctx, &g_ipc, "sensor_service", 1);

    service_config_free(&g_cfg);
    return (rc == SVC_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
