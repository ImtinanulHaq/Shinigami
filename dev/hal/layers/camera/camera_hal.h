/**
 * @file camera_hal.h
 * @brief Camera HAL — V4L2 streaming capture interface.
 *
 * Frames are delivered zero-copy via kernel mmap buffers.  Callers must
 * return every frame with camera_hal_return_frame() before the ring buffer
 * stalls.
 *
 * PROPOSED CHANGES:
 *   - Added CAMERA_CMD_SET_FORMAT for changing the pixel format at runtime
 *     without a full close/open cycle.
 *   - Added camera_hal_validate_device_path() as a public helper to confirm
 *     a V4L2 path is anchored under /dev/ before use.
 *   - timeout_ms in camera_hal_capture_frame() is now honoured via poll().
 */

#ifndef CAMERA_HAL_H
#define CAMERA_HAL_H

#include "hal_interface.h"

/* ── pixel format ─────────────────────────────────────────────────────── */

/**
 * @brief Supported pixel encodings for captured frames.
 *
 * @p CAMERA_FORMAT_YUYV   YUV 4:2:2 packed (V4L2_PIX_FMT_YUYV).
 * @p CAMERA_FORMAT_MJPEG  Motion-JPEG compressed (V4L2_PIX_FMT_MJPEG).
 * @p CAMERA_FORMAT_RGB24  24-bit RGB (V4L2_PIX_FMT_RGB24).
 * @p CAMERA_FORMAT_NV12   YUV 4:2:0 semi-planar (V4L2_PIX_FMT_NV12).
 * @p CAMERA_FORMAT_H264   H.264 compressed (V4L2_PIX_FMT_H264).
 */
typedef enum {
  CAMERA_FORMAT_YUYV  = 0x01,
  CAMERA_FORMAT_MJPEG = 0x02,
  CAMERA_FORMAT_RGB24 = 0x03,
  CAMERA_FORMAT_NV12  = 0x04,
  CAMERA_FORMAT_H264  = 0x05,
} camera_format_t;

/* ── resolution presets ───────────────────────────────────────────────── */

/**
 * @brief Common resolution presets; use CAMERA_RES_CUSTOM for arbitrary sizes.
 *
 * @p CAMERA_RES_QVGA    320 × 240.
 * @p CAMERA_RES_VGA     640 × 480.
 * @p CAMERA_RES_HD      1280 × 720.
 * @p CAMERA_RES_FHD     1920 × 1080.
 * @p CAMERA_RES_4K      3840 × 2160.
 * @p CAMERA_RES_CUSTOM  Caller sets config.width and config.height directly.
 */
typedef enum {
  CAMERA_RES_QVGA   = 0,
  CAMERA_RES_VGA    = 1,
  CAMERA_RES_HD     = 2,
  CAMERA_RES_FHD    = 3,
  CAMERA_RES_4K     = 4,
  CAMERA_RES_CUSTOM = 99,
} camera_resolution_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Stream parameters for a V4L2 capture device.
 *
 * buffer_count determines the depth of the kernel-side ring buffer.
 * A minimum of 2 is required; 4 is recommended for smooth 30+ fps streaming.
 *
 * @p width         Requested capture width in pixels.
 * @p height        Requested capture height in pixels.
 * @p format        Pixel encoding from camera_format_t.
 * @p fps           Requested frames per second.
 * @p buffer_count  Number of mmap ring-buffer slots to request.
 */
typedef struct {
  uint32_t        width;
  uint32_t        height;
  camera_format_t format;
  uint32_t        fps;
  uint32_t        buffer_count;
} camera_config_t;

/* ── frame descriptor ─────────────────────────────────────────────────── */

/**
 * @brief Descriptor for one captured video frame.
 *
 * data points directly into a kernel mmap buffer — there is no copy.
 * The buffer remains owned by the HAL until camera_hal_return_frame() is
 * called.  Do not free data.
 *
 * @p data           Pointer into the kernel mmap buffer; do not free.
 * @p size           Number of valid bytes at data (may be < buffer length).
 * @p timestamp      Microseconds from the V4L2 timeval at capture time.
 * @p sequence       Monotonically increasing capture sequence number.
 * @p _buffer_index  HAL-internal ring-buffer index; callers must not modify.
 */
typedef struct {
  void    *data;
  size_t   size;
  uint64_t timestamp;
  uint32_t sequence;
  uint32_t _buffer_index;
} camera_frame_t;

/* ── device info ──────────────────────────────────────────────────────── */

/**
 * @brief Runtime snapshot of camera device state.
 *
 * @p v4l2_device    V4L2 character device path (e.g. "/dev/video0").
 * @p config         Copy of the active stream configuration.
 * @p frame_count    Total frames successfully dequeued since start.
 * @p dropped_frames Frames not dequeued before the driver recycled them.
 * @p streaming      Non-zero while VIDIOC_STREAMON is active.
 */
typedef struct {
  char            v4l2_device[64];
  camera_config_t config;
  uint32_t        frame_count;
  uint32_t        dropped_frames;
  int             streaming;
} camera_info_t;

/* ── control commands ─────────────────────────────────────────────────── */

/**
 * @brief Commands accepted by the camera device control() operation.
 *
 * @p CAMERA_CMD_SET_RESOLUTION  arg: const camera_resolution_t *
 * @p CAMERA_CMD_SET_FPS         arg: const uint32_t *
 * @p CAMERA_CMD_SET_FORMAT      [PROPOSED] arg: const camera_format_t *
 * @p CAMERA_CMD_GET_CONFIG      arg: camera_config_t *
 * @p CAMERA_CMD_GET_FRAME       arg: camera_frame_t *  (blocking capture)
 * @p CAMERA_CMD_RETURN_FRAME    arg: camera_frame_t *  (re-queue buffer)
 */
typedef enum {
  CAMERA_CMD_SET_RESOLUTION = 0x3000,
  CAMERA_CMD_SET_FPS        = 0x3001,
  CAMERA_CMD_SET_FORMAT     = 0x3002,  /* [PROPOSED] */
  CAMERA_CMD_GET_CONFIG     = 0x3009,
  CAMERA_CMD_GET_FRAME      = 0x3007,
  CAMERA_CMD_RETURN_FRAME   = 0x3008,
} camera_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/** @brief Allocate and initialise a camera HAL device. */
hw_device_t *camera_hal_create(const char *device_name,
                               const char *v4l2_dev_path,
                               const camera_config_t *camera_config);

/** @brief Release all resources held by a camera device. */
void camera_hal_destroy(hw_device_t *device_ptr);

/** @brief Return the default camera configuration. */
camera_config_t camera_hal_default_config(void);

/** @brief Fill width_out and height_out for a resolution preset. */
void camera_hal_get_resolution(camera_resolution_t preset,
                               uint32_t *width_out, uint32_t *height_out);

/** @brief Return a short string identifying a pixel format. */
const char *camera_hal_format_string(camera_format_t format);

/** @brief Dequeue the next available frame from the V4L2 ring buffer. */
int camera_hal_capture_frame(hw_device_t *device_ptr,
                             camera_frame_t *frame_out, uint32_t timeout_ms);

/** @brief Re-queue a dequeued buffer back to the V4L2 driver. */
int camera_hal_return_frame(hw_device_t *device_ptr,
                            camera_frame_t *frame_ptr);

/**
 * [PROPOSED] Validate that a V4L2 device path is anchored under /dev/.
 * @brief Confirm v4l2_dev_path is under /dev/ before opening it.
 */
int camera_hal_validate_device_path(const char *v4l2_dev_path);

#endif /* CAMERA_HAL_H */
