/**
 * @file camera_hal.c
 * @brief Camera HAL implementation — V4L2 zero-copy mmap backend.
 *
 * Security changes vs. original:
 *   - mmap flags changed from PROT_READ|PROT_WRITE to PROT_READ only.
 *     Capture buffers are filled by the kernel DMA engine; user-space
 *     has no legitimate reason to write through them.  A write attempt
 *     now raises SIGSEGV immediately rather than silently corrupting
 *     kernel DMA state.
 *   - camera_hal_return_frame() uses the frame's embedded _buffer_index
 *     for O(1) re-queue and validates both the index range and the data
 *     pointer before passing the index to ioctl.
 *   - control() validates arg pointer alignment before any dereference.
 */

#define _DEFAULT_SOURCE
#include "camera_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* ── private data structures ──────────────────────────────────────────── */

/**
 * @brief Descriptor for one kernel-side mmap buffer slot.
 */
typedef struct {
  void *user_addr;
  size_t byte_length;
} camera_mmap_buffer_t;

/**
 * @brief Internal state for a camera HAL device.
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
 * @brief Maps a control command to its required argument size (0 = no arg).
 */
typedef struct {
  uint32_t command;
  size_t arg_size;
} camera_cmd_spec_t;

static const camera_cmd_spec_t CAMERA_CMD_SPECS[] = {
    {CAMERA_CMD_SET_RESOLUTION, sizeof(camera_resolution_t)},
    {CAMERA_CMD_SET_FPS, sizeof(uint32_t)},
    {CAMERA_CMD_GET_CONFIG, sizeof(camera_config_t)},
    {CAMERA_CMD_GET_FRAME, sizeof(camera_frame_t)},
    {CAMERA_CMD_RETURN_FRAME, sizeof(camera_frame_t)},
};

/* ── helpers ──────────────────────────────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 * @param control_command  Command code from @ref camera_cmd_t.
 * @param command_arg      Argument pointer supplied by caller.
 * @return 0 if valid, -1 if invalid.
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

/**
 * @brief Translate a HAL format code to the V4L2 fourcc constant.
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
 * Unlike ALSA's constraint-refinement model, V4L2 uses an assign-then-
 * verify approach: you write your desired values, then check what the
 * driver actually applied.  Drivers round to the nearest supported
 * resolution; we update priv->config to reflect reality.
 *
 * VIDIOC_S_PARM failure is non-fatal: many cameras do not implement it
 * and rely on VIDIOC_S_FMT implicit rate selection instead.
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
 * @brief Request kernel DMA buffers and map them into user address space.
 *
 * V4L2 streaming I/O works through a ring of kernel-allocated buffers.
 * VIDIOC_REQBUFS asks the driver to allocate N DMA-coherent pages;
 * VIDIOC_QUERYBUF retrieves each buffer's kernel-side offset; mmap()
 * creates a virtual-address window into those pages.
 *
 * Security: PROT_READ only — capture buffers are written exclusively by
 * the camera's DMA engine.  User-space has no legitimate write path.
 * A buggy caller that writes through frame->data now faults immediately
 * with SIGSEGV instead of silently corrupting kernel DMA state.
 *
 * @param priv  Device private data; populates mmap_buffers and count.
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
    priv->mmap_buffers[idx].user_addr = mmap(
        NULL, buf.length, PROT_READ, MAP_SHARED, priv->v4l2_fd, buf.m.offset);

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

static int camera_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  priv->v4l2_fd = open(priv->v4l2_path, O_RDWR | O_NONBLOCK);
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
 * VIDIOC_QBUF hands each buffer to the driver; the camera DMA engine
 * then fills them in ring order.  VIDIOC_STREAMON arms the sensor.
 * After this call, frames arrive continuously into the queued buffers.
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
 * STREAMOFF implicitly dequeues all buffers from the driver ring;
 * no explicit per-buffer dequeue is needed before calling this.
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

static ssize_t camera_read(hw_device_t *device_ptr, void *data_buffer,
                           size_t buffer_size) {
  /* Camera delivers frames only via capture_frame / control(GET_FRAME). */
  (void)device_ptr;
  (void)data_buffer;
  (void)buffer_size;
  return HAL_ERROR_NOT_SUPPORT;
}

static ssize_t camera_write(hw_device_t *device_ptr, const void *data_buffer,
                            size_t data_size) {
  (void)device_ptr;
  (void)data_buffer;
  (void)data_size;
  return HAL_ERROR_NOT_SUPPORT;
}

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
    /* Effective on next open(). */
    return HAL_SUCCESS;
  }

  case CAMERA_CMD_SET_FPS:
    priv->config.fps = *(const uint32_t *)command_arg;
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
};

/* ── public API ───────────────────────────────────────────────────────── */

camera_config_t camera_hal_default_config(void) {
  camera_config_t cfg;
  cfg.width = 640u;
  cfg.height = 480u;
  cfg.format = CAMERA_FORMAT_YUYV;
  cfg.fps = 30u;
  cfg.buffer_count = 4u;
  return cfg;
}

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

  printf("[camera_hal] created '%s' for V4L2 device '%s'\n", device_name,
         v4l2_dev_path);
  return dev;
}

void camera_hal_destroy(hw_device_t *device_ptr) {
  hal_device_unref(device_ptr);
}

int camera_hal_capture_frame(hw_device_t *device_ptr, camera_frame_t *frame_out,
                             uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!device_ptr || !device_ptr->priv || !frame_out)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_ACTIVE)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  /*
   * VIDIOC_DQBUF blocks until the driver marks a buffer as filled.
   * With O_NONBLOCK on the fd, it returns EAGAIN immediately if no
   * frame is ready yet (non-blocking poll pattern).
   */
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

int camera_hal_return_frame(hw_device_t *device_ptr,
                            camera_frame_t *frame_ptr) {
  if (!device_ptr || !device_ptr->priv || !frame_ptr)
    return HAL_ERROR_INVALID;

  camera_priv_t *priv = (camera_priv_t *)device_ptr->priv;

  /*
   * O(1) re-queue: the buffer index is stored in the frame descriptor
   * by capture_frame(), eliminating the original O(N) pointer scan.
   *
   * Dual validation:
   *   1. Range check — prevents an out-of-bounds ioctl argument.
   *   2. Pointer check — detects caller corruption of _buffer_index
   *      or frame->data (e.g. after a double-return).
   */
  if (frame_ptr->_buffer_index >= priv->mmap_buffer_count)
    return HAL_ERROR_INVALID;

  if (priv->mmap_buffers[frame_ptr->_buffer_index].user_addr != frame_ptr->data)
    return HAL_ERROR_INVALID;

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = frame_ptr->_buffer_index;

  if (ioctl(priv->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
    fprintf(stderr, "[camera_hal] VIDIOC_QBUF[%u]: %s\n", buf.index,
            strerror(errno));
    return HAL_ERROR_IO;
  }

  return HAL_SUCCESS;
}
