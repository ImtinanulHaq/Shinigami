/**
 * @file audio_service_main.c
 * @brief Audio service daemon entry point.
 *
 * Startup sequence (Steps 1–10) and graceful shutdown (Step 11) follow the
 * mandatory ordering defined in the Service Layer specification.
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
#include "audio_service.h"
#include "audio_service_hal.h"
#include "audio_service_security.h"
#include "audio_service_loop.h"

/* ── static context ───────────────────────────────────────────────────── */

static audio_service_ctx_t  g_ctx;
static service_config_t     g_cfg;
static svc_ipc_t            g_ipc;

/* ── CLI ──────────────────────────────────────────────────────────────── */

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

/* ── validation ───────────────────────────────────────────────────────── */

static const cfg_required_t REQUIRED_KEYS[] = {
    { "server",   "socket_path"      },
    { "server",   "log_file"         },
    { "hardware", "device"           },
    { "security", "verify_key_file"  },
    { NULL, NULL }
};

/* ── shutdown helper (also called on fatal error paths) ──────────────── */

static void do_shutdown(audio_service_ctx_t *ctx, svc_ipc_t *ipc,
                        const char *svc_name, int hal_started)
{
    ctx->base.state = SVC_STATE_STOPPING;

    /* Step 11a — stop / close / destroy HAL */
    if (ctx->hal_device) {
        hw_device_t *dev = (hw_device_t *)ctx->hal_device;
        if (hal_started && dev->ops) {
            dev->ops->stop(dev);
            dev->ops->close(dev);
        }
        hal_device_destroy(dev);
        ctx->hal_device = NULL;
    }

    /* Step 11b — unregister from SM and close socket */
    if (service_ipc_is_connected(ipc)) {
        service_ipc_unregister(ipc);
        service_ipc_disconnect(ipc);
    }

    /* Step 11c — remove PID file */
    service_base_remove_pid(svc_name);

    /* Step 11d — close log */
    LOG_INFO("audio_service exiting.");
    service_base_close_log();

    ctx->base.state = SVC_STATE_STOPPED;
}

/* ── entry point ──────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *config_path = NULL;
    int foreground = 0, verbose = 0;
    int opt;

    /* ── Step 1: Argument parsing ─────────────────────────────────────── */
    while ((opt = getopt_long(argc, argv, SHORT_OPTS, LONG_OPTS, NULL)) != -1) {
        switch (opt) {
        case 'c': config_path = optarg;  break;
        case 'f': foreground  = 1;       break;
        case 'v': verbose     = 1;       break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (!config_path) {
        fprintf(stderr, "audio_service: -c <config_file> is required\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── Step 2: Config loading ───────────────────────────────────────── */
    if (service_config_load(&g_cfg, config_path, "AUDIO_SERVICE") != 0) {
        fprintf(stderr, "audio_service: failed to load config '%s'\n",
                config_path);
        return EXIT_FAILURE;
    }
    if (service_config_validate(&g_cfg, REQUIRED_KEYS) != 0) {
        fprintf(stderr, "audio_service: config validation failed\n");
        return EXIT_FAILURE;
    }

    /* Populate context from config */
    service_base_init(&g_ctx.base, "audio_service");
    strncpy(g_ctx.base.config_path, config_path,
            sizeof(g_ctx.base.config_path) - 1);
    g_ctx.base.foreground = foreground;
    g_ctx.base.verbose    = verbose;
    strncpy(g_ctx.base.log_path,
            service_config_get_string(&g_cfg, "server", "log_file",
                                      "/var/log/audio_service.log"),
            sizeof(g_ctx.base.log_path) - 1);

    strncpy(g_ctx.alsa_device,
            service_config_get_string(&g_cfg, "hardware", "device", "hw:0"),
            sizeof(g_ctx.alsa_device) - 1);
    g_ctx.sample_rate  = service_config_get_uint32(&g_cfg, "hardware",
                              "sample_rate",  44100);
    g_ctx.channels     = service_config_get_uint32(&g_cfg, "hardware",
                              "channels",     2);
    g_ctx.period_size  = service_config_get_uint32(&g_cfg, "hardware",
                              "period_size",  1024);
    g_ctx.buffer_size  = service_config_get_uint32(&g_cfg, "hardware",
                              "buffer_size",  4096);

    /* Validate hardware ranges */
    if (g_ctx.sample_rate < 8000 || g_ctx.sample_rate > 192000) {
        fprintf(stderr, "audio_service: sample_rate %u out of range "
                        "[8000–192000]\n", g_ctx.sample_rate);
        return EXIT_FAILURE;
    }
    if (g_ctx.channels != 1 && g_ctx.channels != 2) {
        fprintf(stderr, "audio_service: channels must be 1 or 2 "
                        "(got %u)\n", g_ctx.channels);
        return EXIT_FAILURE;
    }

    /* ── Step 3: Daemonize ────────────────────────────────────────────── */
    if (!foreground) {
        if (service_base_daemonize(&g_ctx.base) != SVC_OK) {
            fprintf(stderr, "audio_service: daemonize failed\n");
            return EXIT_FAILURE;
        }
    }

    /* ── Step 4: PID file ─────────────────────────────────────────────── */
    /* Open log first so write_pid errors are visible in syslog */
    service_base_open_log("audio_service", g_ctx.base.log_path);

    int rc = service_base_write_pid("audio_service");
    if (rc == SVC_ERR_ALREADY) {
        LOG_ERR("duplicate instance detected — exiting");
        service_base_close_log();
        return EXIT_FAILURE;
    }
    if (rc != SVC_OK) {
        LOG_ERR("failed to write PID file: %s", svc_error_string(rc));
        service_base_close_log();
        return EXIT_FAILURE;
    }
    g_ctx.base.pid = getpid();

    /* ── Step 5: Syslog + file logging already open above ─────────────── */
    LOG_INFO("audio_service v%s starting (pid=%d, config=%s)",
             SERVICE_LAYER_VERSION_STR, (int)g_ctx.base.pid, config_path);

    /* ── Step 6: Signal handlers ──────────────────────────────────────── */
    if (service_base_install_signals() != SVC_OK) {
        LOG_ERR("failed to install signal handlers");
        do_shutdown(&g_ctx, &g_ipc, "audio_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 7: Connect to Service Manager (3 retries, 2s backoff) ───── */
    const char *sm_sock = service_config_get_string(&g_cfg, "server",
                              "socket_path", SM_SOCKET_PATH);
    const char *vkey    = service_config_get_string(&g_cfg, "security",
                              "verify_key_file", NULL);

    service_ipc_init(&g_ipc, "audio_service", vkey);
    strncpy(g_ipc.socket_path, sm_sock, SERVICE_MAX_PATH - 1);

    int connected = 0;
    for (int attempt = 1; attempt <= SM_CONNECT_RETRIES && !connected; attempt++) {
        if (attempt > 1) {
            LOG_WARN("SM connect attempt %d/%d (retry in %ds)...",
                     attempt, SM_CONNECT_RETRIES, SM_CONNECT_RETRY_DELAY);
            sleep(SM_CONNECT_RETRY_DELAY);
        }
        if (service_ipc_connect(&g_ipc) == SVC_OK)
            connected = 1;
    }
    if (!connected) {
        LOG_ERR("cannot connect to Service Manager at %s after %d retries "
                "— aborting", sm_sock, SM_CONNECT_RETRIES);
        do_shutdown(&g_ctx, &g_ipc, "audio_service", 0);
        return EXIT_FAILURE;
    }

    if (service_ipc_register(&g_ipc, g_ctx.base.exe_path,
                             SERVICE_LAYER_VERSION_STR, g_ctx.base.pid)
            != SVC_OK) {
        LOG_ERR("SM registration failed — aborting");
        do_shutdown(&g_ctx, &g_ipc, "audio_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 8: Apply Security Module ───────────────────────────────── */
    /* Security is irreversible; any failure is fatal. */
    if (audio_service_apply_security(&g_ctx) != SVC_OK) {
        LOG_ERR("security apply failed — aborting");
        do_shutdown(&g_ctx, &g_ipc, "audio_service", 0);
        return EXIT_FAILURE;
    }
    LOG_INFO("security module applied successfully");

    /* ── Step 9: Initialise HAL ──────────────────────────────────────── */
    if (audio_service_hal_init(&g_ctx) != SVC_OK) {
        LOG_ERR("HAL init failed: %s", svc_error_string(SVC_ERR_HAL));
        do_shutdown(&g_ctx, &g_ipc, "audio_service", 0);
        return EXIT_FAILURE;
    }
    if (audio_service_hal_start(&g_ctx) != SVC_OK) {
        LOG_ERR("HAL start failed");
        do_shutdown(&g_ctx, &g_ipc, "audio_service", 0);
        return EXIT_FAILURE;
    }
    LOG_INFO("audio HAL on device '%s' started (rate=%u, ch=%u)",
             g_ctx.alsa_device, g_ctx.sample_rate, g_ctx.channels);

    g_ctx.base.state = SVC_STATE_RUNNING;

    /* ── Step 10: Enter event loop ───────────────────────────────────── */
    rc = audio_service_loop_run(&g_ctx, &g_ipc, &g_cfg);
    if (rc != SVC_OK)
        LOG_ERR("event loop exited with error: %s", svc_error_string(rc));

    /* ── Step 11: Graceful shutdown ──────────────────────────────────── */
    do_shutdown(&g_ctx, &g_ipc, "audio_service", 1 /* hal started */);

    service_config_free(&g_cfg);
    return (rc == SVC_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
