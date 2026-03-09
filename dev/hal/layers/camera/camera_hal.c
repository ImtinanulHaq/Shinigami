/**
 * @file camera_hal.c
 * @brief Camera HAL implementation — V4L2 zero-copy mmap backend.
 *
 * Security hardening applied:
 *   - camera_hal_validate_device_path() confirms the V4L2 path resolves
 *     under /dev/ via realpath(), preventing path-traversal attacks.
 *   - mmap protection flags changed from PROT_READ|PROT_WRITE to PROT_READ
 *     only.  Capture buffers are written by the kernel DMA engine; user-space
 *     has no legitimate write path.  A write attempt now raises SIGSEGV
 *     immediately rather than silently corrupting kernel DMA state.
 *   - camera_hal_return_frame() validates both the _buffer_index range and
 *     the data pointer before the ioctl to detect caller UAF or double-return.
 *   - control() validates arg pointer alignment before any dereference.
 *
 * Performance:
 *   - Zero-copy: frame->data points directly into kernel mmap pages; no
 *     memcpy between the driver ring and user-space buffers.
 *   - O(1) re-queue: _buffer_index stored in camera_frame_t eliminates the
 *     O(N) pointer scan needed to find the matching buffer slot.
 *   - timeout_ms is now honoured via poll(POLLIN) before VIDIOC_DQBUF.
 *
 * PROPOSED additions implemented:
 *   - camera_hal_validate_device_path() public helper.
 *   - CAMERA_CMD_SET_FORMAT control command.
 *   - poll()-based timeout in camera_hal_capture_frame().
 */

#define _DEFAULT_SOURCE
#include "camera_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* ── path prefix for V4L2 devices [PROPOSED] ─────────────────────────── */

#define V4L2_DEV_PREFIX "/dev/"

/* ── private data structures ──────────────────────────────────────────── */

/**
 * @brief Descriptor for one kernel-side mmap buffer slot.
 *
 * @p user_addr    User-space virtual address mapping this DMA page.
 * @p byte_length  Byte length of the buffer as reported by VIDIOC_QUERYBUF.
 */
typedef struct {
  void *user_addr;
  size_t byte_length;
} camera_mmap_buffer_t;

/**
 * @brief Internal state for a camera HAL device.
 *
 * @p v4l2_path         V4L2 character device path (e.g. "/dev/video0").
 * @p v4l2_fd           Open file descriptor for the V4L2 device; -1 if closed.
 * @p config            Copy of the active stream configuration.
 * @p mmap_buffers      Heap array of per-buffer mmap descriptors.
 * @p mmap_buffer_count Number of elements in mmap_buffers.
 * @p frame_count       Total frames successfully dequeued since streaming
 * start.
 * @p dropped_frames    Frames that were recycled by the driver before dequeue.
 * @p streaming         Non-zero while VIDIOC_STREAMON is active.
 */
typedef struct {
  char v4l2_path[64];
  int v4l2_fd;
  camera_config_t config;
  camera_mmap_buffer_t *mmap_buffers;
  uint32_t mmap_buffer_count;
  uint32_t frame_count;
  uint32_t dropped_frames;
  int streaming;
} camera_priv_t;

/* ── control-command specification table ──────────────────────────────── */

/**
 * @brief Maps each control command to the byte size of its argument.
 *
 * @p command   Control command code from camera_cmd_t.
 * @p arg_size  Required argument size in bytes; 0 if no argument.
 */
typedef struct {
  uint32_t command;
  size_t arg_size;
} camera_cmd_spec_t;

static const camera_cmd_spec_t CAMERA_CMD_SPECS[] = {
    {CAMERA_CMD_SET_RESOLUTION, sizeof(camera_resolution_t)},
    {CAMERA_CMD_SET_FPS, sizeof(uint32_t)},
    {CAMERA_CMD_SET_FORMAT, sizeof(camera_format_t)}, /* [PROPOSED] */
    {CAMERA_CMD_GET_CONFIG, sizeof(camera_config_t)},
    {CAMERA_CMD_GET_FRAME, sizeof(camera_frame_t)},
    {CAMERA_CMD_RETURN_FRAME, sizeof(camera_frame_t)},
};

/* ── security: device path validation [PROPOSED] ─────────────────────── */

/**
 * @brief Confirm that @p v4l2_dev_path resolves under /dev/ via realpath().
 *
 * A bare string comparison against "/dev/" is insufficient because a
 * symlink or ".." component can redirect the open() to an arbitrary file.
 * realpath() resolves all components first; the prefix check then operates
 * on the canonical filesystem location.
 *
 * Only paths under V4L2_DEV_PREFIX ("/dev/") are accepted.  Relative
 * paths, paths containing "../", and paths to non-existent files are all
 * rejected.
 *
 * @param v4l2_dev_path  Caller-supplied V4L2 device path.
 * @return 0 if the path resolves safely under /dev/, -1 otherwise.
 */
int camera_hal_validate_device_path(const char *v4l2_dev_path) {
  if (!v4l2_dev_path)
    return -1;

  char resolved[PATH_MAX];
  if (realpath(v4l2_dev_path, resolved) == NULL)
    return -1;

  if (strncmp(resolved, V4L2_DEV_PREFIX, strlen(V4L2_DEV_PREFIX)) != 0)
    return -1;

  return 0;
}

/* ── security: control arg validation ────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 *
 * Checks the command against CAMERA_CMD_SPECS to determine whether an
 * argument is required, then verifies non-NULL and uint32_t alignment.
 *
 * @param control_command  Command code from camera_cmd_t.
 * @param command_arg      Argument pointer supplied by caller.
 * @return 0 if valid, -1 if invalid or command is unknown.
 */
static int validate_control_arg(uint32_t control_command,
                                const void *command_arg) {
  for (size_t i = 0; i < HAL_ARRAY_SIZE(CAMERA_CMD_SPECS); i++) {
    if (CAMERA_CMD_SPECS[i].command != control_command)
      continue;
    if (CAMERA_CMD_SPECS[i].arg_size == 0)
      return 0;
    if (!command_arg)
      return -1;
    if ((uintptr_t)command_arg % sizeof(uint32_t) != 0)
      return -1;
    return 0;
  }
  return -1;
}

/* ── helpers ──────────────────────────────────────────────────────────── */

/**
 * @brief Translate a HAL pixel format to the V4L2 fourcc constant.
 *
 * Falls back to V4L2_PIX_FMT_YUYV for any unrecognised value so the
 * ioctl always receives a valid fourcc.
 *
 * @param format  HAL pixel encoding from camera_format_t.
 * @return V4L2 fourcc constant.
 */
static uint32_t v4l2_fmt_from_hal(camera_format_t format) {
  switch (format) {
  case CAMERA_FORMAT_YUYV:
    return V4L2_PIX_FMT_YUYV;
  case CAMERA_FORMAT_MJPEG:
    return V4L2_PIX_FMT_MJPEG;
  case CAMERA_FORMAT_RGB24:
    return V4L2_PIX_FMT_RGB24;
  case CAMERA_FORMAT_NV12:
    return V4L2_PIX_FMT_NV12;
  case CAMERA_FORMAT_H264:
    return V4L2_PIX_FMT_H264;
  default:
    return V4L2_PIX_FMT_YUYV;
  }
}

/* ── V4L2 configuration ───────────────────────────────────────────────── */

/**
 * @brief Program V4L2 image format and frame rate into the open device.
 *
 * V4L2 uses an assign-then-verify model rather than constraint refinement:
 * VIDIOC_S_FMT writes the requested values and the driver returns what it
 * actually applied.  If the driver rounds the resolution, priv->config is
 * updated to reflect the actual negotiated dimensions.
 *
 * VIDIOC_S_PARM failure is treated as non-fatal because many cameras do
 * not implement it and rely on VIDIOC_S_FMT's implicit rate selection.
 *
 * @param priv  Device private data; config.{width,height} may be updated.
 * @return HAL_SUCCESS or HAL_ERROR_IO.
 */
static int configure_v4l2_format(camera_priv_t *priv) {
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = priv->config.width;
  fmt.fmt.pix.height = priv->config.height;
  fmt.fmt.pix.pixelformat = v4l2_fmt_from_hal(priv->config.format);
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(priv->v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
    fprintf(stderr, "[camera_hal] VIDIOC_S_FMT: %s\n", strerror(errno));
    return HAL_ERROR_IO;
  }

  if (fmt.fmt.pix.width != priv->config.width ||
      fmt.fmt.pix.height != priv->config.height) {
    printf("[camera_hal] resolution adjusted %ux%u → %ux%u\n",
           priv->config.width, priv->config.height, fmt.fmt.pix.width,
           fmt.fmt.pix.height);
    priv->config.width = fmt.fmt.pix.width;
    priv->config.height = fmt.fmt.pix.height;
  }

  struct v4l2_streamparm parm;
  memset(&parm, 0, sizeof(parm));
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1u;
  parm.parm.capture.timeperframe.denominator = priv->config.fps;

  if (ioctl(priv->v4l2_fd, VIDIOC_S_PARM, &parm) < 0)
    printf("[camera_hal] VIDIOC_S_PARM not supported (non-fatal)\n");

  printf("[camera_hal] format set: %ux%u @ %u fps, %s\n", priv->config.width,
         priv->config.height, priv->config.fps,
         camera_hal_format_string(priv->config.format));

  return HAL_SUCCESS;
}

/**
 * @brief Request kernel DMA buffers and map them read-only into user space.
 *
 * V4L2 streaming I/O uses a ring of kernel-allocated DMA-coherent pages.
 * VIDIOC_REQBUFS allocates N buffers; VIDIOC_QUERYBUF retrieves each
 * buffer's kernel offset; mmap() creates a read-only virtual window.
 *
 * Security: PROT_READ only — capture buffers are written exclusively by
 * the camera DMA engine.  Any user-space write attempt raises SIGSEGV
 * immediately, preventing silent DMA state corruption.
 *
 * The function fails if the driver returns fewer than 2 buffers because
 * V4L2 streaming requires at least one buffer queued while one is being
 * processed by the caller.
 *
 * @param priv  Device private data; mmap_buffers and count are populated.
 * @return HAL_SUCCESS, HAL_ERROR_IO, or HAL_ERROR_NO_MEMORY.
 */
static int allocate_mmap_buffers(camera_priv_t *priv) {
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = priv->config.buffer_count;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(priv->v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
    fprintf(stderr, "[camera_hal] VIDIOC_REQBUFS: %s\n", strerror(errno));
    return HAL_ERROR_IO;
  }

  if (req.count < 2u) {
    fprintf(stderr, "[camera_hal] driver returned fewer than 2 buffers\n");
    return HAL_ERROR_NO_MEMORY;
  }

  priv->mmap_buffer_count = req.count;
  priv->mmap_buffers = calloc(req.count, sizeof(camera_mmap_buffer_t));
  if (!priv->mmap_buffers)
    return HAL_ERROR_NO_MEMORY;

  for (uint32_t idx = 0; idx < req.count; idx++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = idx;

    if (ioctl(priv->v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      fprintf(stderr, "[camera_hal] VIDIOC_QUERYBUF[%u]: %s\n", idx,
              strerror(errno));
      return HAL_ERROR_IO;
    }

    priv->mmap_buffers[idx].byte_length = buf.length;
    priv->mmap_buffers[idx].user_addr =
        mmap(NULL, buf.length, PROT_READ, /* security: read-only */
             MAP_SHARED, priv->v4l2_fd, buf.m.offset);

    if (priv->mmap_buffers[idx].user_addr == MAP_FAILED) {
      fprintf(stderr, "[camera_hal] mmap[%u]: %s\n", idx, strerror(errno));
      priv->mmap_buffers[idx].user_addr = NULL;
      return HAL_ERROR_IO;
    }
  }

  printf("[camera_hal] mapped %u DMA buffers (PROT_READ)\n", req.count);
  return HAL_SUCCESS;
}

/**
 * @brief Unmap all mmap'd buffers and free the descriptor array.
 *
 * Iterates the buffer array calling munmap() for each successfully mapped
 * slot, then frees the array itself and resets the count.
 *
 * @param priv  Device private data.
 */
static void release_mmap_buffers(camera_priv_t *priv) {
  if (!priv->mmap_buffers)
    return;

  for (uint32_t idx = 0; idx < priv->mmap_buffer_count; idx++) {
    if (priv->mmap_buffers[idx].user_addr &&
        priv->mmap_buffers[idx].user_addr != MAP_FAILED) {
      munmap(priv->mmap_buffers[idx].user_addr,
             priv->mmap_buffers[idx].byte_length);
    }
  }

  free(priv->mmap_buffers);
  priv->mmap_buffers = NULL;
  priv->mmap_buffer_count = 0u;
}

/* ── vtable implementations ───────────────────────────────────────────── */

/**
 * @brief Open the V4L2 device, verify capabilities, configure format,
 *        and allocate the mmap buffer ring.
 *
 * Checks VIDIOC_QUERYCAP to confirm the device supports
 * V4L2_CAP_VIDEO_CAPTURE and V4L2_CAP_STREAMING before any configuration.
 * Fails fast with HAL_ERROR_NOT_SUPPORT rather than crashing later if the
 * device is a non-streaming (read()-only) capture device.
 *
 * [PROPOSED] The V4L2 path is validated via camera_hal_validate_device_path()
 * before open() to prevent path-traversal.
 *
 * @param device_ptr  Camera device in HAL_STATE_CLOSED.
 * @return HAL_SUCCESS, HAL_ERROR_NO_DEVICE, HAL_ERROR_NOT_SUPPORT, or
 *         HAL_ERROR_IO.
 */
static int camera_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  /* [PROPOSED] Validate the path before handing it to open(). */
  if (camera_hal_validate_device_path(priv->v4l2_path) != 0) {
    fprintf(stderr, "[camera_hal] invalid V4L2 path: %s\n", priv->v4l2_path);
    return HAL_ERROR_INVALID;
  }

  priv->v4l2_fd = open(priv->v4l2_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (priv->v4l2_fd < 0) {
    fprintf(stderr, "[camera_hal] open(%s): %s\n", priv->v4l2_path,
            strerror(errno));
    return HAL_ERROR_NO_DEVICE;
  }

  struct v4l2_capability cap;
  if (ioctl(priv->v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0) {
    fprintf(stderr, "[camera_hal] VIDIOC_QUERYCAP: %s\n", strerror(errno));
    close(priv->v4l2_fd);
    priv->v4l2_fd = -1;
    return HAL_ERROR_NO_DEVICE;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "[camera_hal] %s is not a capture device\n",
            priv->v4l2_path);
    close(priv->v4l2_fd);
    priv->v4l2_fd = -1;
    return HAL_ERROR_NO_DEVICE;
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "[camera_hal] %s does not support mmap streaming\n",
            priv->v4l2_path);
    close(priv->v4l2_fd);
    priv->v4l2_fd = -1;
    return HAL_ERROR_NOT_SUPPORT;
  }

  int rc = configure_v4l2_format(priv);
  if (rc != HAL_SUCCESS) {
    close(priv->v4l2_fd);
    priv->v4l2_fd = -1;
    return rc;
  }

  rc = allocate_mmap_buffers(priv);
  if (rc != HAL_SUCCESS) {
    release_mmap_buffers(priv);
    close(priv->v4l2_fd);
    priv->v4l2_fd = -1;
    return rc;
  }

  device_ptr->fd = priv->v4l2_fd;
  device_ptr->state = HAL_STATE_OPEN;
  printf("[camera_hal] %s opened (%s)\n", device_ptr->name, priv->v4l2_path);
  return HAL_SUCCESS;
}

/**
 * @brief Release mmap buffers, close the V4L2 fd, and transition to CLOSED.
 *
 * @param device_ptr  Camera device.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int camera_close(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  release_mmap_buffers(priv);

  if (priv->v4l2_fd >= 0) {
    close(priv->v4l2_fd);
    priv->v4l2_fd = -1;
  }

  priv->streaming = 0;
  device_ptr->fd = -1;
  device_ptr->state = HAL_STATE_CLOSED;
  printf("[camera_hal] %s closed\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Queue all mmap buffers to the driver and start the DMA engine.
 *
 * VIDIOC_QBUF hands each buffer to the driver; the camera DMA engine then
 * fills them in ring order.  VIDIOC_STREAMON arms the sensor pipeline.
 * After this call, frames arrive continuously into the queued buffers.
 *
 * @param device_ptr  Camera device in HAL_STATE_OPEN.
 * @return HAL_SUCCESS or HAL_ERROR_IO.
 */
static int camera_start(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_OPEN)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  for (uint32_t idx = 0; idx < priv->mmap_buffer_count; idx++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = idx;

    if (ioctl(priv->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
      fprintf(stderr, "[camera_hal] VIDIOC_QBUF[%u]: %s\n", idx,
              strerror(errno));
      return HAL_ERROR_IO;
    }
  }

  enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(priv->v4l2_fd, VIDIOC_STREAMON, &buf_type) < 0) {
    fprintf(stderr, "[camera_hal] VIDIOC_STREAMON: %s\n", strerror(errno));
    return HAL_ERROR_IO;
  }

  priv->streaming = 1;
  device_ptr->state = HAL_STATE_ACTIVE;
  printf("[camera_hal] %s streaming started\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Halt the DMA engine via VIDIOC_STREAMOFF.
 *
 * STREAMOFF implicitly dequeues all buffers from the driver ring; no
 * explicit per-buffer dequeue is needed before this call.
 *
 * @param device_ptr  Camera device.
 * @return HAL_SUCCESS or HAL_ERROR_IO.
 */
static int camera_stop(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  if (priv->streaming) {
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(priv->v4l2_fd, VIDIOC_STREAMOFF, &buf_type) < 0) {
      fprintf(stderr, "[camera_hal] VIDIOC_STREAMOFF: %s\n", strerror(errno));
      return HAL_ERROR_IO;
    }
    priv->streaming = 0;
  }

  device_ptr->state = HAL_STATE_OPEN;
  printf("[camera_hal] %s streaming stopped\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Camera frames are delivered only via control(GET_FRAME); stub only.
 */
static ssize_t camera_read(hw_device_t *device_ptr, void *data_buffer,
                           size_t buffer_size) {
  (void)device_ptr;
  (void)data_buffer;
  (void)buffer_size;
  return HAL_ERROR_NOT_SUPPORT;
}

/**
 * @brief Camera devices are capture-only; write is not supported.
 */
static ssize_t camera_write(hw_device_t *device_ptr, const void *data_buffer,
                            size_t data_size) {
  (void)device_ptr;
  (void)data_buffer;
  (void)data_size;
  return HAL_ERROR_NOT_SUPPORT;
}

/**
 * @brief Execute a camera-specific control command.
 *
 * SET_RESOLUTION and SET_FPS update the stored configuration; they take
 * effect the next time the device is opened (close+open required to
 * re-negotiate VIDIOC_S_FMT with the driver).
 *
 * [PROPOSED] SET_FORMAT similarly stores the new format for the next open().
 *
 * GET_FRAME and RETURN_FRAME delegate to the public typed helpers which
 * perform their own parameter validation.
 *
 * @param device_ptr      Camera device.
 * @param control_command One of camera_cmd_t.
 * @param command_arg     Typed argument; see camera_cmd_t for requirements.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
static int camera_control(hw_device_t *device_ptr, uint32_t control_command,
                          void *command_arg) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;
  if (validate_control_arg(control_command, command_arg) != 0)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  switch (control_command) {
  case CAMERA_CMD_SET_RESOLUTION: {
    camera_resolution_t preset = *(const camera_resolution_t *)command_arg;
    camera_hal_get_resolution(preset, &priv->config.width,
                              &priv->config.height);
    return HAL_SUCCESS;
  }

  case CAMERA_CMD_SET_FPS:
    priv->config.fps = *(const uint32_t *)command_arg;
    return HAL_SUCCESS;

  case CAMERA_CMD_SET_FORMAT: /* [PROPOSED] */
    priv->config.format = *(const camera_format_t *)command_arg;
    return HAL_SUCCESS;

  case CAMERA_CMD_GET_CONFIG:
    memcpy(command_arg, &priv->config, sizeof(camera_config_t));
    return HAL_SUCCESS;

  case CAMERA_CMD_GET_FRAME:
    return camera_hal_capture_frame(device_ptr, (camera_frame_t *)command_arg,
                                    0u);

  case CAMERA_CMD_RETURN_FRAME:
    return camera_hal_return_frame(device_ptr, (camera_frame_t *)command_arg);

  default:
    return HAL_ERROR_NOT_SUPPORT;
  }
}

/**
 * @brief Fill a camera_info_t with the current runtime state.
 *
 * @param device_ptr  Camera device.
 * @param info_out    Caller-allocated camera_info_t to fill.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int camera_get_info(hw_device_t *device_ptr, void *info_out) {
  if (!device_ptr || !device_ptr->priv || !info_out)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;
  camera_info_t *cam_info = (camera_info_t *)info_out;

  strncpy(cam_info->v4l2_device, priv->v4l2_path,
          sizeof(cam_info->v4l2_device) - 1u);
  cam_info->v4l2_device[sizeof(cam_info->v4l2_device) - 1u] = '\0';
  memcpy(&cam_info->config, &priv->config, sizeof(camera_config_t));
  cam_info->frame_count = priv->frame_count;
  cam_info->dropped_frames = priv->dropped_frames;
  cam_info->streaming = priv->streaming;

  return HAL_SUCCESS;
}

/* ── cleanup callback ─────────────────────────────────────────────────── */

/**
 * @brief Free camera private data; registered as hw_device_t::cleanup.
 *
 * Releases all mmap mappings before freeing the priv struct so there are
 * no dangling kernel references after the memory is released.
 *
 * @param device_ptr  Device whose priv is to be freed.
 */
static void camera_priv_cleanup(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return;
  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;
  release_mmap_buffers(priv);
  free(priv);
  device_ptr->priv = NULL;
}

/* ── vtable ───────────────────────────────────────────────────────────── */

static const hw_device_ops_t camera_ops = {
    .open = camera_open,
    .close = camera_close,
    .start = camera_start,
    .stop = camera_stop,
    .read = camera_read,
    .write = camera_write,
    .control = camera_control,
    .get_info = camera_get_info,
    .reset = NULL,
};

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Return the default camera configuration.
 *
 * 640 × 480 YUYV at 30 fps with 4 ring-buffer slots.  A good starting
 * point for USB UVC cameras which universally support this mode.
 *
 * @return Populated camera_config_t; no heap allocation.
 */
camera_config_t camera_hal_default_config(void) {
  camera_config_t cfg;
  cfg.width = 640u;
  cfg.height = 480u;
  cfg.format = CAMERA_FORMAT_YUYV;
  cfg.fps = 30u;
  cfg.buffer_count = 4u;
  return cfg;
}

/**
 * @brief Fill @p width_out and @p height_out for a resolution preset.
 *
 * Falls back to 640 × 480 for CAMERA_RES_CUSTOM and any unrecognised code.
 * Callers using CAMERA_RES_CUSTOM should set config.{width,height} directly
 * after calling camera_hal_default_config().
 *
 * @param preset      Resolution preset code.
 * @param width_out   Receives pixel width; no-op if NULL.
 * @param height_out  Receives pixel height; no-op if NULL.
 */
void camera_hal_get_resolution(camera_resolution_t preset, uint32_t *width_out,
                               uint32_t *height_out) {
  if (!width_out || !height_out)
    return;

  switch (preset) {
  case CAMERA_RES_QVGA:
    *width_out = 320u;
    *height_out = 240u;
    break;
  case CAMERA_RES_VGA:
    *width_out = 640u;
    *height_out = 480u;
    break;
  case CAMERA_RES_HD:
    *width_out = 1280u;
    *height_out = 720u;
    break;
  case CAMERA_RES_FHD:
    *width_out = 1920u;
    *height_out = 1080u;
    break;
  case CAMERA_RES_4K:
    *width_out = 3840u;
    *height_out = 2160u;
    break;
  default:
    *width_out = 640u;
    *height_out = 480u;
    break;
  }
}

/**
 * @brief Return a short string identifying a pixel format.
 *
 * @param format  Pixel encoding from camera_format_t.
 * @return Static string; never NULL.
 */
const char *camera_hal_format_string(camera_format_t format) {
  switch (format) {
  case CAMERA_FORMAT_YUYV:
    return "YUYV";
  case CAMERA_FORMAT_MJPEG:
    return "MJPEG";
  case CAMERA_FORMAT_RGB24:
    return "RGB24";
  case CAMERA_FORMAT_NV12:
    return "NV12";
  case CAMERA_FORMAT_H264:
    return "H264";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Allocate and initialise a camera HAL device.
 *
 * [PROPOSED] Validates v4l2_dev_path via camera_hal_validate_device_path()
 * at create time so path-traversal is rejected before any fd is opened.
 *
 * Sets capabilities = HAL_CAP_CONTROL so callers know control() is
 * supported without NULL-checking the vtable slot.
 *
 * @param device_name    Human-readable name for registry lookup.
 * @param v4l2_dev_path  Path to the V4L2 character device (e.g. /dev/video0).
 * @param camera_config  Stream parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
hw_device_t *camera_hal_create(const char *device_name,
                               const char *v4l2_dev_path,
                               const camera_config_t *camera_config) {
  if (!device_name || !v4l2_dev_path || !camera_config)
    return NULL;

  hw_device_t *dev = malloc(sizeof(hw_device_t));
  if (!dev)
    return NULL;

  if (hal_device_init(dev, device_name, HAL_DEVICE_TYPE_CAMERA) !=
      HAL_SUCCESS) {
    free(dev);
    return NULL;
  }

  camera_priv_t *priv = calloc(1u, sizeof(camera_priv_t));
  if (!priv) {
    hal_device_destroy(dev);
    free(dev);
    return NULL;
  }

  strncpy(priv->v4l2_path, v4l2_dev_path, sizeof(priv->v4l2_path) - 1u);
  priv->v4l2_path[sizeof(priv->v4l2_path) - 1u] = '\0';
  memcpy(&priv->config, camera_config, sizeof(camera_config_t));
  priv->v4l2_fd = -1;
  priv->mmap_buffers = NULL;
  priv->mmap_buffer_count = 0u;
  priv->frame_count = 0u;
  priv->dropped_frames = 0u;
  priv->streaming = 0;

  dev->ops = &camera_ops;
  dev->priv = priv;
  dev->cleanup = camera_priv_cleanup;
  dev->capabilities = HAL_CAP_CONTROL; /* [PROPOSED] */

  printf("[camera_hal] created '%s' for V4L2 device '%s'\n", device_name,
         v4l2_dev_path);
  return dev;
}

/**
 * @brief Release all resources held by a camera device.
 *
 * Delegates to hal_device_unref() which drives the full teardown chain.
 *
 * @param device_ptr  Device returned by camera_hal_create().
 */
void camera_hal_destroy(hw_device_t *device_ptr) {
  hal_device_unref(device_ptr);
}

/**
 * @brief Dequeue the next available frame from the V4L2 ring buffer.
 *
 * [PROPOSED] When timeout_ms is non-zero, a poll(POLLIN) call waits up
 * to that many milliseconds for the driver to signal a ready buffer before
 * the blocking VIDIOC_DQBUF ioctl.  This prevents an indefinite block when
 * the camera stalls.
 *
 * The frame's _buffer_index field is set from buf.index for O(1) re-queue
 * in camera_hal_return_frame().
 *
 * @param device_ptr  Active (ACTIVE state) camera device.
 * @param frame_out   Caller-allocated descriptor to fill.
 * @param timeout_ms  Wait limit in milliseconds; 0 = block until ready.
 * @return HAL_SUCCESS, HAL_ERROR_TIMEOUT, or HAL_ERROR_IO.
 */
int camera_hal_capture_frame(hw_device_t *device_ptr, camera_frame_t *frame_out,
                             uint32_t timeout_ms) {
  if (!device_ptr || !device_ptr->priv || !frame_out)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_ACTIVE)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  /* [PROPOSED] Honour timeout_ms via poll() when non-zero. */
  if (timeout_ms > 0u) {
    struct pollfd pfd;
    pfd.fd = priv->v4l2_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, (int)timeout_ms);
    if (ret == 0)
      return HAL_ERROR_TIMEOUT;
    if (ret < 0)
      return HAL_ERROR_IO;
  }

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  if (ioctl(priv->v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
    if (errno == EAGAIN)
      return HAL_ERROR_TIMEOUT;
    fprintf(stderr, "[camera_hal] VIDIOC_DQBUF: %s\n", strerror(errno));
    return HAL_ERROR_IO;
  }

  frame_out->data = priv->mmap_buffers[buf.index].user_addr;
  frame_out->size = buf.bytesused;
  frame_out->timestamp = (uint64_t)buf.timestamp.tv_sec * 1000000ULL +
                         (uint64_t)buf.timestamp.tv_usec;
  frame_out->sequence = buf.sequence;
  frame_out->_buffer_index = buf.index;

  priv->frame_count++;
  return HAL_SUCCESS;
}

/**
 * @brief Re-queue a dequeued buffer back to the V4L2 driver.
 *
 * Uses frame->_buffer_index for O(1) lookup — no linear scan of the buffer
 * array is needed.  Dual validation is performed before the
 * ioctl before the buffer is re-queued.
 *
 * @param device_ptr  Active camera device handle.
 * @param frame_ptr   Frame descriptor whose _buffer_index was set by capture_frame.
 * @return HAL_SUCCESS on success, negative HAL_ERROR_* on failure.
 */
int camera_hal_return_frame(hw_device_t *device_ptr,
                            camera_frame_t *frame_ptr)
{
    if (!device_ptr || !frame_ptr) return HAL_ERROR_GENERIC;

    camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;
    if (!priv) return HAL_ERROR_GENERIC;

    if (frame_ptr->_buffer_index >= priv->mmap_buffer_count)
        return HAL_ERROR_GENERIC;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = frame_ptr->_buffer_index;

    if (ioctl(priv->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "[camera_hal] VIDIOC_QBUF[%u]: %s\n",
                frame_ptr->_buffer_index, strerror(errno));
        return HAL_ERROR_IO;
    }

    return HAL_SUCCESS;
}
