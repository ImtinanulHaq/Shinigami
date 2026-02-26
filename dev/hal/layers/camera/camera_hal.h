/**
 * @file camera_hal.h
 * @brief Camera HAL — V4L2 streaming capture interface.
 *
 * Frames are delivered zero-copy via kernel mmap buffers.  Callers must
 * return every frame with @ref camera_hal_return_frame before the ring
 * buffer stalls.
 */

#ifndef CAMERA_HAL_H
#define CAMERA_HAL_H

#include "hal_interface.h"

/* ── pixel format ─────────────────────────────────────────────────────── */

/**
 * @brief Supported pixel encodings for captured frames.
 */
typedef enum {
  CAMERA_FORMAT_YUYV = 0x01,
  CAMERA_FORMAT_MJPEG = 0x02,
  CAMERA_FORMAT_RGB24 = 0x03,
  CAMERA_FORMAT_NV12 = 0x04,
  CAMERA_FORMAT_H264 = 0x05,
} camera_format_t;

/* ── resolution presets ───────────────────────────────────────────────── */

/**
 * @brief Common resolution presets; use CAMERA_RES_CUSTOM for arbitrary sizes.
 */
typedef enum {
  CAMERA_RES_QVGA = 0, /* 320×240   */
  CAMERA_RES_VGA = 1,  /* 640×480   */
  CAMERA_RES_HD = 2,   /* 1280×720  */
  CAMERA_RES_FHD = 3,  /* 1920×1080 */
  CAMERA_RES_4K = 4,   /* 3840×2160 */
  CAMERA_RES_CUSTOM = 99,
} camera_resolution_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Stream parameters for a V4L2 capture device.
 *
 * @p buffer_count determines the depth of the kernel-side ring buffer.
 * Minimum 2; 4 is recommended for smooth 30+ fps streaming.
 */
typedef struct {
  uint32_t width;
  uint32_t height;
  camera_format_t format;
  uint32_t fps;
  uint32_t buffer_count;
} camera_config_t;

/* ── frame descriptor ─────────────────────────────────────────────────── */

/**
 * @brief Descriptor for one captured video frame.
 *
 * @p data points directly into a kernel mmap buffer — there is no copy.
 * The buffer remains owned by the HAL until @ref camera_hal_return_frame
 * is called.  Do not free @p data.
 *
 * @p _buffer_index is set by the HAL and used internally by
 * @ref camera_hal_return_frame to perform an O(1) re-queue without
 * scanning the buffer array.  Callers must not modify it.
 */
typedef struct {
  void *data;
  size_t size;
  uint64_t timestamp;
  uint32_t sequence;
  uint32_t _buffer_index;
} camera_frame_t;

/* ── device info ──────────────────────────────────────────────────────── */

/**
 * @brief Runtime snapshot of camera device state.
 */
typedef struct {
  char v4l2_device[64];
  camera_config_t config;
  uint32_t frame_count;
  uint32_t dropped_frames;
  int streaming;
} camera_info_t;

/* ── control commands ─────────────────────────────────────────────────── */

/**
 * @brief Commands accepted by the camera device control() operation.
 *
 *   CAMERA_CMD_SET_RESOLUTION — arg: const camera_resolution_t *
 *   CAMERA_CMD_SET_FPS        — arg: const uint32_t *
 *   CAMERA_CMD_GET_CONFIG     — arg: camera_config_t *
 *   CAMERA_CMD_GET_FRAME      — arg: camera_frame_t *  (blocking capture)
 *   CAMERA_CMD_RETURN_FRAME   — arg: camera_frame_t *  (re-queue buffer)
 */
typedef enum {
  CAMERA_CMD_SET_RESOLUTION = 0x3000,
  CAMERA_CMD_SET_FPS = 0x3001,
  CAMERA_CMD_GET_CONFIG = 0x3009,
  CAMERA_CMD_GET_FRAME = 0x3007,
  CAMERA_CMD_RETURN_FRAME = 0x3008,
} camera_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a camera HAL device.
 *
 * @param device_name    Human-readable name for registry lookup.
 * @param v4l2_dev_path  Path to the V4L2 character device, e.g. "/dev/video0".
 * @param camera_config  Stream parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
hw_device_t *camera_hal_create(const char *device_name,
                               const char *v4l2_dev_path,
                               const camera_config_t *camera_config);

/**
 * @brief Release all resources held by a camera device.
 * @param device_ptr  Device returned by @ref camera_hal_create.
 */
void camera_hal_destroy(hw_device_t *device_ptr);

/**
 * @brief Return the default camera configuration.
 *
 * 640×480 YUYV at 30 fps with 4 ring-buffer slots.
 *
 * @return Populated camera_config_t; no heap allocation.
 */
camera_config_t camera_hal_default_config(void);

/**
 * @brief Fill @p width_out and @p height_out for a resolution preset.
 *
 * @param preset      Resolution preset code.
 * @param width_out   Receives pixel width.
 * @param height_out  Receives pixel height.
 */
void camera_hal_get_resolution(camera_resolution_t preset, uint32_t *width_out,
                               uint32_t *height_out);

/**
 * @brief Return a short string identifying a pixel format.
 * @param format  Pixel encoding.
 * @return Static string; never NULL.
 */
const char *camera_hal_format_string(camera_format_t format);

/**
 * @brief Dequeue the next available frame from the V4L2 ring buffer.
 *
 * Blocks until a frame is ready.  @p frame->data points into a kernel
 * mmap buffer; the caller must call @ref camera_hal_return_frame when
 * done to avoid stalling the pipeline.
 *
 * @param device_ptr  Active (ACTIVE state) camera device.
 * @param frame_out   Caller-allocated descriptor to fill.
 * @param timeout_ms  Not yet used; reserved for poll()-based future impl.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int camera_hal_capture_frame(hw_device_t *device_ptr, camera_frame_t *frame_out,
                             uint32_t timeout_ms);

/**
 * @brief Re-queue a dequeued buffer back to the V4L2 driver.
 *
 * Must be called exactly once per frame obtained from
 * @ref camera_hal_capture_frame.  Uses @p frame->_buffer_index for O(1)
 * lookup — no linear scan of the buffer array.
 *
 * @param device_ptr  Camera device that produced the frame.
 * @param frame_ptr   Frame whose buffer is to be returned.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int camera_hal_return_frame(hw_device_t *device_ptr, camera_frame_t *frame_ptr);

#endif /* CAMERA_HAL_H */
