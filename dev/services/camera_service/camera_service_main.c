/**
 * @file camera_service_main.c
 * @brief Camera service daemon entry point.
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
#include "camera_service.h"
#include "camera_service_hal.h"
#include "camera_service_security.h"
#include "camera_service_loop.h"

static camera_service_ctx_t  g_ctx;
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
    { "hardware", "device"           },
    { "hardware", "width"            },
    { "hardware", "height"           },
    { "security", "verify_key_file"  },
    { NULL, NULL }
};

static void do_shutdown(camera_service_ctx_t *ctx, svc_ipc_t *ipc,
                        const char *svc_name, int hal_started)
{
    ctx->base.state = SVC_STATE_STOPPING;

    /* Destroy memory pools before HAL */
    if (ctx->frame_pool) { memory_pool_destroy(ctx->frame_pool); ctx->frame_pool = NULL; }
    if (ctx->ipc_pool)   { memory_pool_destroy(ctx->ipc_pool);   ctx->ipc_pool   = NULL; }

    if (ctx->hal_device) {
        hw_device_t *dev = (hw_device_t *)ctx->hal_device;
        if (hal_started && dev->ops) {
            dev->ops->stop(dev);
            dev->ops->close(dev);
        }
        hal_device_destroy(dev);
        ctx->hal_device = NULL;
    }

    if (service_ipc_is_connected(ipc)) {
        service_ipc_unregister(ipc);
        service_ipc_disconnect(ipc);
    }

    service_base_remove_pid(svc_name);
    LOG_INFO("camera_service exiting.");
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
        fprintf(stderr, "camera_service: -c <config_file> is required\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── Step 2: Config loading ───────────────────────────────────────── */
    if (service_config_load(&g_cfg, config_path, "CAMERA_SERVICE") != 0) {
        fprintf(stderr, "camera_service: failed to load config '%s'\n",
                config_path);
        return EXIT_FAILURE;
    }
    if (service_config_validate(&g_cfg, REQUIRED_KEYS) != 0) {
        fprintf(stderr, "camera_service: config validation failed\n");
        return EXIT_FAILURE;
    }

    service_base_init(&g_ctx.base, "camera_service");
    /* Copy parsed config into context so security/other modules can read it */
    g_ctx.config = g_cfg;
    strncpy(g_ctx.base.config_path, config_path,
            sizeof(g_ctx.base.config_path) - 1);
    g_ctx.base.foreground = foreground;
    g_ctx.base.verbose    = verbose;
    strncpy(g_ctx.base.log_path,
            service_config_get_string(&g_cfg, "server", "log_file",
                                      "/var/log/camera_service.log"),
            sizeof(g_ctx.base.log_path) - 1);

    strncpy(g_ctx.v4l2_device,
            service_config_get_string(&g_cfg, "hardware", "device",
                                      "/dev/video0"),
            sizeof(g_ctx.v4l2_device) - 1);
    g_ctx.width        = service_config_get_uint32(&g_cfg, "hardware",
                              "width",        640);
    g_ctx.height       = service_config_get_uint32(&g_cfg, "hardware",
                              "height",       480);
    g_ctx.fps          = service_config_get_uint32(&g_cfg, "hardware",
                              "fps",          30);
    g_ctx.buffer_count = service_config_get_uint32(&g_cfg, "hardware",
                              "buffer_count", 4);

    const char *fmt_str = service_config_get_string(&g_cfg, "hardware",
                              "format", "YUYV");
    g_ctx.format = (strcasecmp(fmt_str, "MJPEG") == 0) ? 1 : 0;

    /* Validate */
    if (g_ctx.width == 0 || g_ctx.height == 0) {
        fprintf(stderr, "camera_service: width and height must be > 0\n");
        return EXIT_FAILURE;
    }
    if (g_ctx.fps == 0 || g_ctx.fps > 240) {
        fprintf(stderr, "camera_service: fps must be 1–240 (got %u)\n",
                g_ctx.fps);
        return EXIT_FAILURE;
    }

    /* ── Step 3: Daemonize ────────────────────────────────────────────── */
    if (!foreground) {
        if (service_base_daemonize(&g_ctx.base) != SVC_OK) {
            fprintf(stderr, "camera_service: daemonize failed\n");
            return EXIT_FAILURE;
        }
    }

    /* ── Step 4: PID file ─────────────────────────────────────────────── */
    service_base_open_log("camera_service", g_ctx.base.log_path);

    int rc = service_base_write_pid("camera_service");
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

    /* ── Step 5: Logging is already open ─────────────────────────────── */
    LOG_INFO("camera_service v%s starting (pid=%d)", SERVICE_LAYER_VERSION_STR,
             (int)g_ctx.base.pid);

    /* ── Step 6: Signal handlers ──────────────────────────────────────── */
    if (service_base_install_signals() != SVC_OK) {
        LOG_ERR("failed to install signal handlers");
        do_shutdown(&g_ctx, &g_ipc, "camera_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 7: Connect to Service Manager ──────────────────────────── */
    const char *sm_sock = service_config_get_string(&g_cfg, "server",
                              "socket_path", SM_SOCKET_PATH);
    const char *vkey    = service_config_get_string(&g_cfg, "security",
                              "verify_key_file", NULL);

    service_ipc_init(&g_ipc, "camera_service", vkey);
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
        do_shutdown(&g_ctx, &g_ipc, "camera_service", 0);
        return EXIT_FAILURE;
    }
    if (service_ipc_register(&g_ipc, g_ctx.base.exe_path,
                             SERVICE_LAYER_VERSION_STR, g_ctx.base.pid)
            != SVC_OK) {
        LOG_ERR("SM registration failed");
        do_shutdown(&g_ctx, &g_ipc, "camera_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 8: Apply Security Module ───────────────────────────────── */
    if (camera_service_apply_security(&g_ctx) != SVC_OK) {
        LOG_ERR("security apply failed — aborting");
        do_shutdown(&g_ctx, &g_ipc, "camera_service", 0);
        return EXIT_FAILURE;
    }

    /* ── Step 9: Initialise HAL ──────────────────────────────────────── */
    int skip_hal = service_config_get_bool(&g_cfg, "security", "skip_hal_init", 0);
    if (skip_hal) {
        LOG_INFO("HAL init skipped (skip_hal_init=1 in config)");
    } else if (camera_service_hal_init(&g_ctx) != SVC_OK) {
        LOG_ERR("HAL init failed");
        do_shutdown(&g_ctx, &g_ipc, "camera_service", 0);
        return EXIT_FAILURE;
    }
    if (!skip_hal && camera_service_hal_start(&g_ctx) != SVC_OK) {
        LOG_ERR("HAL start failed");
        do_shutdown(&g_ctx, &g_ipc, "camera_service", 0);
        return EXIT_FAILURE;
    }
    if (!skip_hal) LOG_INFO("camera HAL on %s started (%ux%u @ %ufps)",
             g_ctx.v4l2_device, g_ctx.width, g_ctx.height, g_ctx.fps);

    /* ── Step 9b: Create memory pools ──────────────────────────────── */
    {
        size_t frame_sz = (size_t)g_ctx.width * g_ctx.height * 2u; /* YUYV */
        size_t buf_cnt  = g_ctx.buffer_count > 0 ? g_ctx.buffer_count : 4u;
        memory_pool_config_t fcfg = {
            .block_size  = frame_sz,
            .block_count = buf_cnt,
            .thread_safe = 0,
            .name        = "camera_frame_pool"
        };
        g_ctx.frame_pool = memory_pool_create(&fcfg);
        if (!g_ctx.frame_pool)
            LOG_WARN("camera frame pool creation failed — continuing without pool");

        memory_pool_config_t icfg = {
            .block_size  = 512,
            .block_count = 64,
            .thread_safe = 0,
            .name        = "camera_ipc_pool"
        };
        g_ctx.ipc_pool = memory_pool_create(&icfg);
        if (!g_ctx.ipc_pool)
            LOG_WARN("camera IPC pool creation failed — continuing without pool");
    }

    g_ctx.base.state = SVC_STATE_RUNNING;

    /* ── Step 10: Event loop ─────────────────────────────────────────── */
    rc = camera_service_loop_run(&g_ctx, &g_ipc, &g_cfg);
    if (rc != SVC_OK)
        LOG_ERR("event loop exited with error: %s", svc_error_string(rc));

    /* ── Step 11: Graceful shutdown ──────────────────────────────────── */
    do_shutdown(&g_ctx, &g_ipc, "camera_service", 1);

    service_config_free(&g_cfg);
    return (rc == SVC_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
