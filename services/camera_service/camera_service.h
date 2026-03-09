/**
 * @file camera_service.h
 * @brief Public types, structs, and API for the Camera Service daemon.
 */

#ifndef CAMERA_SERVICE_H
#define CAMERA_SERVICE_H

#include "../common/service_base.h"
#include "../common/service_config.h"
#include "../common/service_ipc.h"

#include <stdint.h>

/* ── defaults ─────────────────────────────────────────────────────────── */

#define CAMERA_SERVICE_NAME           "camera_service"
#define CAMERA_SERVICE_CONF           "/etc/camera_service/camera_service.conf"
#define CAMERA_SERVICE_DEFAULT_DEV    "/dev/video0"
#define CAMERA_SERVICE_DEFAULT_WIDTH  640
#define CAMERA_SERVICE_DEFAULT_HEIGHT 480
#define CAMERA_SERVICE_DEFAULT_FPS    30
#define CAMERA_SERVICE_DEFAULT_BUFS   4
#define CAMERA_SERVICE_DEFAULT_FMT    1   /* CAMERA_FORMAT_YUYV */

/* ── context ──────────────────────────────────────────────────────────── */

typedef struct {
    svc_context_t   base;
    svc_ipc_t       ipc;
    config_t        config;
    void           *hal_device;    /**< hw_device_t* from camera HAL        */
    int             security_applied;

    /* Camera-specific config */
    char            v4l2_device[64];
    uint32_t        width;
    uint32_t        height;
    uint32_t        fps;
    uint32_t        buffer_count;
    int             format;        /**< camera_format_t enum value          */
} camera_service_ctx_t;

/* ── lifecycle API ────────────────────────────────────────────────────── */

int  camera_service_init(camera_service_ctx_t *ctx, int argc, char **argv);
int  camera_service_run(camera_service_ctx_t *ctx);
void camera_service_shutdown(camera_service_ctx_t *ctx);

#endif /* CAMERA_SERVICE_H */
