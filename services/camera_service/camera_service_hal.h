/**
 * @file camera_service_hal.h
 * @brief Camera HAL integration interface.
 */

#ifndef CAMERA_SERVICE_HAL_H
#define CAMERA_SERVICE_HAL_H

#include "camera_service.h"

int   camera_service_hal_init(camera_service_ctx_t *ctx);
int   camera_service_hal_start(camera_service_ctx_t *ctx);
int   camera_service_hal_capture(camera_service_ctx_t *ctx,
                                 void **frame_data, size_t *frame_size,
                                 uint32_t *buffer_index);
int   camera_service_hal_return(camera_service_ctx_t *ctx,
                                uint32_t buffer_index);
int   camera_service_hal_stop(camera_service_ctx_t *ctx);
void  camera_service_hal_cleanup(camera_service_ctx_t *ctx);

#endif /* CAMERA_SERVICE_HAL_H */
