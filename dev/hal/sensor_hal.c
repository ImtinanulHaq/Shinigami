/**
 * @file sensor_hal.c
 * @brief Sensor HAL implementation — Linux IIO sysfs and buffer backend.
 *
 * Security changes vs. original:
 *   - validate_iio_device_id() enforces an allowlist regex on the device
 *     name and calls realpath() to ensure the resolved path is anchored
 *     under IIO_BASE_PATH, defeating "../../../etc" traversal attacks.
 *   - All sysfs text is parsed with strtol/strtof and isfinite() instead
 *     of atoi/atof, detecting overflow, parse failure, and NaN/Inf.
 *   - control() validates arg pointer and alignment before dereference.
 *
 * Performance changes vs. original:
 *   - With enable_buffer=1, reads come from /dev/iio:deviceN via a single
 *     read() syscall that returns all channels atomically.  The hardware
 *     IIO trigger provides the timestamp, eliminating clock_gettime().
 *   - Sysfs mode is retained as a fallback for drivers that do not expose
 *     /dev/iio:deviceN.
 */

#define _DEFAULT_SOURCE
#include "sensor_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── path constants ───────────────────────────────────────────────────── */

#define IIO_BASE_PATH "/sys/bus/iio/devices"
#define IIO_DEV_PREFIX "/dev/iio:"

/* ── IIO scan layout ──────────────────────────────────────────────────── */

/**
 * @brief Byte layout of one IIO hardware-buffer scan for a 3-axis sensor.
 *
 * The IIO subsystem packs channels in index order, each occupying its
 * natural storage width (typically 2 bytes for 16-bit IMU channels),
 * with trailing padding to align the int64 timestamp to an 8-byte boundary.
 */
typedef struct {
  uint32_t x_byte_offset;
  uint32_t y_byte_offset;
  uint32_t z_byte_offset;
  uint32_t ts_byte_offset;
  uint32_t stride;
  int has_timestamp;
} iio_scan_layout_t;

/* ── private device data ──────────────────────────────────────────────── */

/**
 * @brief Internal state for a sensor HAL device.
 */
typedef struct {
  char iio_sysfs_path[256];
  char iio_dev_node[64];
  sensor_config_t config;
  float scale_value;
  float offset_x;
  float offset_y;
  float offset_z;
  int buffer_fd;
  iio_scan_layout_t scan_layout;
} sensor_priv_t;

/* ── control-command specification table ──────────────────────────────── */

/**
 * @brief Maps a control command to its required argument size (0 = no arg).
 */
typedef struct {
  uint32_t command;
  size_t arg_size;
} sensor_cmd_spec_t;

static const sensor_cmd_spec_t SENSOR_CMD_SPECS[] = {
    {SENSOR_CMD_SET_RATE, sizeof(uint32_t)},
    {SENSOR_CMD_GET_RATE, sizeof(uint32_t)},
    {SENSOR_CMD_ENABLE_BUFFER, sizeof(int)},
    {SENSOR_CMD_CALIBRATE, 0},
    {SENSOR_CMD_GET_CONFIG, sizeof(sensor_config_t)},
};

/* ── security: device name validation ────────────────────────────────── */

/**
 * @brief Confirm that @p iio_device_id is a well-formed IIO device name
 *        and that its resolved sysfs path is anchored under IIO_BASE_PATH.
 *
 * Valid names match "iio:device" followed by 1–4 decimal digits with no
 * other characters.  The subsequent realpath() call resolves any symlinks
 * or path components (e.g. "../../etc") before the prefix check, defeating
 * path-traversal attacks that bypass a naive string comparison.
 *
 * @param iio_device_id  Caller-supplied device identifier string.
 * @return 0 if valid and accessible, -1 otherwise.
 */
static int validate_iio_device_id(const char *iio_device_id) {
  if (!iio_device_id)
    return -1;

  static const char EXPECTED_PREFIX[] = "iio:device";
  if (strncmp(iio_device_id, EXPECTED_PREFIX, sizeof(EXPECTED_PREFIX) - 1u) !=
      0)
    return -1;

  const char *digits = iio_device_id + (sizeof(EXPECTED_PREFIX) - 1u);
  size_t digit_count = strlen(digits);

  if (digit_count == 0u || digit_count > 4u)
    return -1;

  for (size_t i = 0u; i < digit_count; i++) {
    if (digits[i] < '0' || digits[i] > '9')
      return -1;
  }

  char candidate_path[PATH_MAX];
  snprintf(candidate_path, sizeof(candidate_path), "%s/%s", IIO_BASE_PATH,
           iio_device_id);

  char resolved_path[PATH_MAX];
  if (realpath(candidate_path, resolved_path) == NULL)
    return -1;

  /*
   * realpath() guarantees resolved_path is the canonical absolute path
   * with all symlinks and ".." components expanded.  The prefix check
   * now operates on the real filesystem location.
   */
  if (strncmp(resolved_path, IIO_BASE_PATH, strlen(IIO_BASE_PATH)) != 0)
    return -1;

  return 0;
}

/* ── security: control arg validation ────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 * @param control_command  Command code from @ref sensor_cmd_t.
 * @param command_arg      Argument pointer supplied by caller.
 * @return 0 if valid, -1 if invalid or unknown command.
 */
static int validate_control_arg(uint32_t control_command,
                                const void *command_arg) {
  for (size_t i = 0; i < HAL_ARRAY_SIZE(SENSOR_CMD_SPECS); i++) {
    if (SENSOR_CMD_SPECS[i].command != control_command)
      continue;
    if (SENSOR_CMD_SPECS[i].arg_size == 0)
      return 0;
    if (!command_arg)
      return -1;
    if ((uintptr_t)command_arg % sizeof(uint32_t) != 0)
      return -1;
    return 0;
  }
  return -1;
}

/* ── safe sysfs parsing ───────────────────────────────────────────────── */

/**
 * @brief Read a sysfs file into @p buf_out.
 *
 * @param sysfs_path  Absolute path to the sysfs attribute file.
 * @param buf_out     Buffer to receive the null-terminated text.
 * @param buf_size    Size of @p buf_out in bytes.
 * @return 0 on success, -1 on I/O error.
 */
static int read_sysfs_text(const char *sysfs_path, char *buf_out,
                           size_t buf_size) {
  int fd = open(sysfs_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  ssize_t bytes = read(fd, buf_out, buf_size - 1u);
  close(fd);

  if (bytes <= 0)
    return -1;

  buf_out[bytes] = '\0';
  if (bytes > 0 && buf_out[bytes - 1] == '\n')
    buf_out[bytes - 1] = '\0';

  return 0;
}

/**
 * @brief Write a text string to a sysfs attribute file.
 * @return 0 on success, -1 on error.
 */
static int write_sysfs_text(const char *sysfs_path, const char *value) {
  int fd = open(sysfs_path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  ssize_t len = (ssize_t)strlen(value);
  ssize_t written = write(fd, value, (size_t)len);
  close(fd);

  return (written == len) ? 0 : -1;
}

/**
 * @brief Parse a sysfs integer value using strtol with full error detection.
 *
 * Unlike atoi(), strtol() detects overflow (sets errno to ERANGE) and
 * distinguishes "no digits consumed" from a legitimate zero.
 *
 * @param sysfs_path  Path to the sysfs attribute file.
 * @param value_out   Receives the parsed integer.
 * @return 0 on success, -1 on parse or range error.
 */
static int read_sysfs_int(const char *sysfs_path, int *value_out) {
  char buf[32];
  if (read_sysfs_text(sysfs_path, buf, sizeof(buf)) != 0)
    return -1;

  char *end;
  errno = 0;
  long val = strtol(buf, &end, 10);

  if (errno != 0)
    return -1;
  if (end == buf)
    return -1;
  if (*end != '\0' && *end != '\n')
    return -1;
  if (val > (long)INT_MAX || val < (long)INT_MIN)
    return -1;

  *value_out = (int)val;
  return 0;
}

/**
 * @brief Parse a sysfs float value using strtof with overflow / NaN detection.
 *
 * A sysfs scale file that contains "inf" or "nan" (possible if a buggy
 * driver returns garbage) would propagate silently through atof().  strtof()
 * plus isfinite() catches both the overflow case (errno == ERANGE → ±HUGE_VALF)
 * and any NaN the standard library may produce.
 *
 * @param sysfs_path  Path to the sysfs attribute file.
 * @param value_out   Receives the parsed float.
 * @return 0 on success, -1 on parse, range, or non-finite error.
 */
static int read_sysfs_float(const char *sysfs_path, float *value_out) {
  char buf[64];
  if (read_sysfs_text(sysfs_path, buf, sizeof(buf)) != 0)
    return -1;

  char *end;
  errno = 0;
  float val = strtof(buf, &end);

  if (errno != 0)
    return -1;
  if (end == buf)
    return -1;
  if (!isfinite(val))
    return -1;

  *value_out = val;
  return 0;
}

/* ── sysfs helpers ────────────────────────────────────────────────────── */

/**
 * @brief Return the IIO sysfs channel prefix for a sensor type.
 */
static const char *iio_channel_prefix(sensor_type_t sensor_type) {
  switch (sensor_type) {
  case SENSOR_TYPE_ACCEL:
    return "in_accel";
  case SENSOR_TYPE_GYRO:
    return "in_anglvel";
  case SENSOR_TYPE_MAGNET:
    return "in_magn";
  case SENSOR_TYPE_LIGHT:
    return "in_illuminance";
  case SENSOR_TYPE_PROXIMITY:
    return "in_proximity";
  case SENSOR_TYPE_PRESSURE:
    return "in_pressure";
  case SENSOR_TYPE_TEMPERATURE:
    return "in_temp";
  case SENSOR_TYPE_HUMIDITY:
    return "in_humidity";
  default:
    return "in_unknown";
  }
}

/**
 * @brief Read one raw axis value from sysfs and apply the scale factor.
 *
 * @param priv       Device private data.
 * @param axis_name  Axis suffix: "x", "y", "z", or "input" (1-axis sensors).
 * @param value_out  Receives the scaled physical-unit value.
 * @return 0 on success, -1 on error.
 */
static int read_sysfs_axis(const sensor_priv_t *priv, const char *axis_name,
                           float *value_out) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s_%s_raw", priv->iio_sysfs_path,
           iio_channel_prefix(priv->config.type), axis_name);

  int raw_value;
  if (read_sysfs_int(path, &raw_value) != 0)
    return -1;

  *value_out = (float)raw_value * priv->scale_value;
  return 0;
}

/* ── IIO hardware buffer ──────────────────────────────────────────────── */

/**
 * @brief Compute the binary scan layout for a 3-axis sensor IIO buffer.
 *
 * Each scan element exposes its channel index via the scan_elements
 * subdirectory.  We read x, y, z indices to determine the order channels
 * appear in the binary scan, assign byte offsets (2 bytes each for 16-bit
 * sensors), then compute the timestamp offset aligned to an 8-byte boundary
 * as required by the IIO ABI.
 *
 * @param priv    Device private data; populates priv->scan_layout.
 * @param prefix  IIO channel prefix (e.g. "in_accel").
 * @return 0 on success, -1 if the layout cannot be determined.
 */
static int compute_iio_scan_layout(sensor_priv_t *priv, const char *prefix) {
  char path[512];
  int idx_x = -1, idx_y = -1, idx_z = -1, idx_ts = -1;

  snprintf(path, sizeof(path), "%s/scan_elements/%s_x_index",
           priv->iio_sysfs_path, prefix);
  read_sysfs_int(path, &idx_x);

  snprintf(path, sizeof(path), "%s/scan_elements/%s_y_index",
           priv->iio_sysfs_path, prefix);
  read_sysfs_int(path, &idx_y);

  snprintf(path, sizeof(path), "%s/scan_elements/%s_z_index",
           priv->iio_sysfs_path, prefix);
  read_sysfs_int(path, &idx_z);

  snprintf(path, sizeof(path), "%s/scan_elements/timestamp_index",
           priv->iio_sysfs_path);
  priv->scan_layout.has_timestamp = (read_sysfs_int(path, &idx_ts) == 0);

  if (idx_x < 0 || idx_y < 0 || idx_z < 0)
    return -1;

  /*
   * Build a minimal index→offset table.  Each axis channel is 2 bytes
   * (int16_t, the standard for IIO IMU channels on all common Linux drivers).
   * Sort the three axis indices and assign contiguous 2-byte slots.
   */
  int sorted[3] = {idx_x, idx_y, idx_z};
  int axis_id[3] = {0, 1, 2}; /* 0=x, 1=y, 2=z */

  /* Simple insertion sort on the three elements. */
  for (int i = 1; i < 3; i++) {
    for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
      int tmp_idx = sorted[j];
      sorted[j] = sorted[j - 1];
      sorted[j - 1] = tmp_idx;
      int tmp_id = axis_id[j];
      axis_id[j] = axis_id[j - 1];
      axis_id[j - 1] = tmp_id;
    }
  }

  uint32_t offsets[3];
  for (int slot = 0; slot < 3; slot++)
    offsets[slot] = (uint32_t)slot * 2u;

  /*
   * Reassign from sorted order back to x/y/z named fields so callers
   * can use priv->scan_layout.x_byte_offset directly.
   */
  for (int slot = 0; slot < 3; slot++) {
    switch (axis_id[slot]) {
    case 0:
      priv->scan_layout.x_byte_offset = offsets[slot];
      break;
    case 1:
      priv->scan_layout.y_byte_offset = offsets[slot];
      break;
    case 2:
      priv->scan_layout.z_byte_offset = offsets[slot];
      break;
    }
  }

  uint32_t after_axes = 3u * 2u;

  if (priv->scan_layout.has_timestamp) {
    /*
     * The IIO ABI requires the timestamp to be aligned to 8 bytes.
     * Round up after_axes to the next 8-byte boundary.
     */
    uint32_t ts_offset = (after_axes + 7u) & ~7u;
    priv->scan_layout.ts_byte_offset = ts_offset;
    priv->scan_layout.stride = ts_offset + 8u;
  } else {
    priv->scan_layout.ts_byte_offset = 0u;
    priv->scan_layout.stride = after_axes;
  }

  return 0;
}

/**
 * @brief Enable IIO hardware buffer channels and open /dev/iio:deviceN.
 *
 * The IIO buffer pipeline:
 *   1. Enable each scan element (channel) via scan_elements/*_en = "1".
 *   2. Set the kernel ring-buffer depth (buffer/length).
 *   3. Arm the buffer (buffer/enable = "1").
 *   4. Open /dev/iio:deviceN; each read() returns one complete scan.
 *
 * A hardware trigger (e.g. a timer or the sensor's DRDY interrupt) drives
 * the ring — without a trigger the buffer will not produce data.  Trigger
 * association is assumed to have been done externally (via
 * /sys/bus/iio/devices/iio:deviceN/trigger/current_trigger).
 *
 * @param priv    Device private data; populates buffer_fd and scan_layout.
 * @param prefix  IIO channel prefix string.
 * @return 0 on success, -1 on failure.
 */
static int open_iio_buffer(sensor_priv_t *priv, const char *prefix) {
  char path[512];
  const char *axes[] = {"x", "y", "z"};

  /* Enable axis channels. */
  for (int i = 0; i < 3; i++) {
    snprintf(path, sizeof(path), "%s/scan_elements/%s_%s_en",
             priv->iio_sysfs_path, prefix, axes[i]);
    write_sysfs_text(path, "1");
  }

  /* Enable timestamp channel if present. */
  snprintf(path, sizeof(path), "%s/scan_elements/timestamp_en",
           priv->iio_sysfs_path);
  write_sysfs_text(path, "1");

  if (compute_iio_scan_layout(priv, prefix) != 0) {
    fprintf(stderr, "[sensor_hal] could not compute scan layout — "
                    "falling back to sysfs mode\n");
    return -1;
  }

  /* Set ring-buffer depth (number of scans the kernel holds). */
  snprintf(path, sizeof(path), "%s/buffer/length", priv->iio_sysfs_path);
  write_sysfs_text(path, "64");

  /* Arm the buffer. */
  snprintf(path, sizeof(path), "%s/buffer/enable", priv->iio_sysfs_path);
  write_sysfs_text(path, "1");

  /* Open the character device that exposes the ring as a byte stream. */
  priv->buffer_fd = open(priv->iio_dev_node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (priv->buffer_fd < 0) {
    /* Disable the buffer if the device node is not present. */
    snprintf(path, sizeof(path), "%s/buffer/enable", priv->iio_sysfs_path);
    write_sysfs_text(path, "0");
    fprintf(stderr, "[sensor_hal] open(%s): %s — using sysfs fallback\n",
            priv->iio_dev_node, strerror(errno));
    return -1;
  }

  printf("[sensor_hal] IIO buffer enabled: stride=%u bytes, ts=%s\n",
         priv->scan_layout.stride,
         priv->scan_layout.has_timestamp ? "yes" : "no");
  return 0;
}

/**
 * @brief Disable the IIO hardware buffer and close its file descriptor.
 * @param priv  Device private data.
 */
static void close_iio_buffer(sensor_priv_t *priv) {
  if (priv->buffer_fd >= 0) {
    close(priv->buffer_fd);
    priv->buffer_fd = -1;
  }

  char path[512];
  snprintf(path, sizeof(path), "%s/buffer/enable", priv->iio_sysfs_path);
  write_sysfs_text(path, "0");
}

/* ── timestamp fallback ───────────────────────────────────────────────── */

/**
 * @brief Return current CLOCK_MONOTONIC time in nanoseconds.
 *
 * Used only in sysfs-fallback mode.  In buffer mode the hardware timestamp
 * embedded in the scan is used instead (more accurate: it reflects the
 * DMA completion time rather than the time the process dequeued the data).
 */
static uint64_t monotonic_timestamp_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── vtable implementations ───────────────────────────────────────────── */

static int sensor_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;
  char path[512];
  char rate_buf[32];

  /* Cache scale factor from sysfs so the read hot-path avoids file I/O. */
  snprintf(path, sizeof(path), "%s/%s_scale", priv->iio_sysfs_path,
           iio_channel_prefix(priv->config.type));

  if (read_sysfs_float(path, &priv->scale_value) != 0) {
    fprintf(stderr, "[sensor_hal] scale not found — defaulting to 1.0\n");
    priv->scale_value = 1.0f;
  }

  snprintf(path, sizeof(path), "%s/sampling_frequency", priv->iio_sysfs_path);
  snprintf(rate_buf, sizeof(rate_buf), "%u", priv->config.sampling_rate_hz);
  write_sysfs_text(path, rate_buf);

  if (priv->config.enable_buffer) {
    if (open_iio_buffer(priv, iio_channel_prefix(priv->config.type)) != 0)
      priv->buffer_fd = -1;
  }

  device_ptr->state = HAL_STATE_OPEN;
  printf("[sensor_hal] %s opened (%s, %s mode)\n", device_ptr->name,
         sensor_hal_type_string(priv->config.type),
         priv->buffer_fd >= 0 ? "buffer" : "sysfs");
  return HAL_SUCCESS;
}

static int sensor_close(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;

  close_iio_buffer(priv);

  device_ptr->state = HAL_STATE_CLOSED;
  printf("[sensor_hal] %s closed\n", device_ptr->name);
  return HAL_SUCCESS;
}

static int sensor_start(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_OPEN)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_ACTIVE;
  printf("[sensor_hal] %s started\n", device_ptr->name);
  return HAL_SUCCESS;
}

static int sensor_stop(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_OPEN;
  printf("[sensor_hal] %s stopped\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Read one sensor sample using IIO buffer or sysfs fallback.
 *
 * Buffer path: one read() syscall returns priv->scan_layout.stride bytes
 * containing all enabled channels packed in index order.  The int16 axis
 * values are extracted at their pre-computed byte offsets and the hardware
 * timestamp (int64, nanoseconds on CLOCK_MONOTONIC) is copied verbatim.
 *
 * Sysfs path: three separate open/read/close cycles, one per axis, each
 * producing a decimal ASCII string that is parsed and scaled.
 */
static ssize_t sensor_read(hw_device_t *device_ptr, void *data_buffer,
                           size_t buffer_size) {
  if (!device_ptr || !device_ptr->priv || !data_buffer)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_ACTIVE)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;

  int is_3axis = (priv->config.type == SENSOR_TYPE_ACCEL ||
                  priv->config.type == SENSOR_TYPE_GYRO ||
                  priv->config.type == SENSOR_TYPE_MAGNET);

  if (is_3axis) {
    if (buffer_size < sizeof(sensor_data_3axis_t))
      return HAL_ERROR_INVALID;

    sensor_data_3axis_t *data = (sensor_data_3axis_t *)data_buffer;

    if (priv->buffer_fd >= 0) {
      /*
       * Buffer mode: single read() from the IIO character device.
       * The kernel DMA engine writes int16 samples from the sensor
       * into the ring; we extract them at the pre-computed byte
       * offsets inside the scan_layout struct.
       */
      uint8_t scan_buf[64];
      if (priv->scan_layout.stride > sizeof(scan_buf))
        return HAL_ERROR_INVALID;

      ssize_t bytes = read(priv->buffer_fd, scan_buf, priv->scan_layout.stride);
      if (bytes < 0) {
        if (errno == EAGAIN)
          return HAL_ERROR_TIMEOUT;
        return HAL_ERROR_IO;
      }
      if ((size_t)bytes < priv->scan_layout.stride)
        return HAL_ERROR_IO;

      int16_t raw_x, raw_y, raw_z;
      memcpy(&raw_x, scan_buf + priv->scan_layout.x_byte_offset,
             sizeof(int16_t));
      memcpy(&raw_y, scan_buf + priv->scan_layout.y_byte_offset,
             sizeof(int16_t));
      memcpy(&raw_z, scan_buf + priv->scan_layout.z_byte_offset,
             sizeof(int16_t));

      data->x = (float)raw_x * priv->scale_value + priv->offset_x;
      data->y = (float)raw_y * priv->scale_value + priv->offset_y;
      data->z = (float)raw_z * priv->scale_value + priv->offset_z;

      if (priv->scan_layout.has_timestamp) {
        memcpy(&data->timestamp, scan_buf + priv->scan_layout.ts_byte_offset,
               sizeof(uint64_t));
      } else {
        data->timestamp = monotonic_timestamp_ns();
      }

    } else {
      /* Sysfs fallback: three separate file reads. */
      if (read_sysfs_axis(priv, "x", &data->x) != 0)
        return HAL_ERROR_IO;
      if (read_sysfs_axis(priv, "y", &data->y) != 0)
        return HAL_ERROR_IO;
      if (read_sysfs_axis(priv, "z", &data->z) != 0)
        return HAL_ERROR_IO;
      data->x += priv->offset_x;
      data->y += priv->offset_y;
      data->z += priv->offset_z;
      data->timestamp = monotonic_timestamp_ns();
    }

    return (ssize_t)sizeof(sensor_data_3axis_t);

  } else {
    if (buffer_size < sizeof(sensor_data_1axis_t))
      return HAL_ERROR_INVALID;

    sensor_data_1axis_t *data = (sensor_data_1axis_t *)data_buffer;

    if (read_sysfs_axis(priv, "input", &data->value) != 0)
      return HAL_ERROR_IO;
    data->timestamp = monotonic_timestamp_ns();

    return (ssize_t)sizeof(sensor_data_1axis_t);
  }
}

static ssize_t sensor_write(hw_device_t *device_ptr, const void *data_buffer,
                            size_t data_size) {
  (void)device_ptr;
  (void)data_buffer;
  (void)data_size;
  return HAL_ERROR_NOT_SUPPORT;
}

static int sensor_control(hw_device_t *device_ptr, uint32_t control_command,
                          void *command_arg) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;
  if (validate_control_arg(control_command, command_arg) != 0)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;

  switch (control_command) {
  case SENSOR_CMD_SET_RATE: {
    uint32_t new_rate = *(const uint32_t *)command_arg;
    priv->config.sampling_rate_hz = new_rate;
    char path[512], rate_buf[32];
    snprintf(path, sizeof(path), "%s/sampling_frequency", priv->iio_sysfs_path);
    snprintf(rate_buf, sizeof(rate_buf), "%u", new_rate);
    write_sysfs_text(path, rate_buf);
    return HAL_SUCCESS;
  }

  case SENSOR_CMD_GET_RATE:
    *(uint32_t *)command_arg = priv->config.sampling_rate_hz;
    return HAL_SUCCESS;

  case SENSOR_CMD_ENABLE_BUFFER: {
    int enable = *(const int *)command_arg;
    priv->config.enable_buffer = enable;
    if (enable && priv->buffer_fd < 0 &&
        device_ptr->state != HAL_STATE_CLOSED) {
      open_iio_buffer(priv, iio_channel_prefix(priv->config.type));
    } else if (!enable && priv->buffer_fd >= 0) {
      close_iio_buffer(priv);
    }
    return HAL_SUCCESS;
  }

  case SENSOR_CMD_CALIBRATE:
    if (priv->config.type == SENSOR_TYPE_ACCEL ||
        priv->config.type == SENSOR_TYPE_GYRO ||
        priv->config.type == SENSOR_TYPE_MAGNET) {
      float x = 0.0f, y = 0.0f, z = 0.0f;
      read_sysfs_axis(priv, "x", &x);
      read_sysfs_axis(priv, "y", &y);
      read_sysfs_axis(priv, "z", &z);
      priv->offset_x = -x;
      priv->offset_y = -y;
      priv->offset_z = -z;
      printf("[sensor_hal] calibrated: offsets x=%.4f y=%.4f z=%.4f\n",
             priv->offset_x, priv->offset_y, priv->offset_z);
    }
    return HAL_SUCCESS;

  case SENSOR_CMD_GET_CONFIG:
    memcpy(command_arg, &priv->config, sizeof(sensor_config_t));
    return HAL_SUCCESS;

  default:
    return HAL_ERROR_NOT_SUPPORT;
  }
}

static int sensor_get_info(hw_device_t *device_ptr, void *info_out) {
  if (!device_ptr || !device_ptr->priv || !info_out)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;
  sensor_info_t *sensor_info = (sensor_info_t *)info_out;

  strncpy(sensor_info->iio_device_path, priv->iio_sysfs_path,
          sizeof(sensor_info->iio_device_path) - 1u);
  sensor_info->iio_device_path[sizeof(sensor_info->iio_device_path) - 1u] =
      '\0';
  sensor_info->type = priv->config.type;
  sensor_info->sampling_rate_hz = priv->config.sampling_rate_hz;
  sensor_info->resolution = priv->scale_value;
  sensor_info->max_range = 0.0f;
  sensor_info->fifo_size = 0u;
  sensor_info->buffer_mode_active = (priv->buffer_fd >= 0);

  return HAL_SUCCESS;
}

/* ── cleanup callback ─────────────────────────────────────────────────── */

/**
 * @brief Free sensor private data; registered as hw_device_t::cleanup.
 * @param device_ptr  Device whose priv is to be freed.
 */
static void sensor_priv_cleanup(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return;
  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;
  close_iio_buffer(priv);
  explicit_bzero(priv, sizeof(sensor_priv_t));
  free(priv);
  device_ptr->priv = NULL;
}

/* ── vtable ───────────────────────────────────────────────────────────── */

static const hw_device_ops_t sensor_ops = {
    .open = sensor_open,
    .close = sensor_close,
    .start = sensor_start,
    .stop = sensor_stop,
    .read = sensor_read,
    .write = sensor_write,
    .control = sensor_control,
    .get_info = sensor_get_info,
};

/* ── public API ───────────────────────────────────────────────────────── */

const char *sensor_hal_type_string(sensor_type_t sensor_type) {
  switch (sensor_type) {
  case SENSOR_TYPE_ACCEL:
    return "accelerometer";
  case SENSOR_TYPE_GYRO:
    return "gyroscope";
  case SENSOR_TYPE_MAGNET:
    return "magnetometer";
  case SENSOR_TYPE_LIGHT:
    return "light";
  case SENSOR_TYPE_PROXIMITY:
    return "proximity";
  case SENSOR_TYPE_PRESSURE:
    return "pressure";
  case SENSOR_TYPE_TEMPERATURE:
    return "temperature";
  case SENSOR_TYPE_HUMIDITY:
    return "humidity";
  default:
    return "unknown";
  }
}

sensor_config_t sensor_hal_default_config(sensor_type_t sensor_type) {
  sensor_config_t cfg;
  cfg.type = sensor_type;
  cfg.sampling_rate_hz = 100u;
  cfg.scale = 1u;
  cfg.enable_buffer = 0;
  return cfg;
}

hw_device_t *sensor_hal_create(const char *device_name,
                               const char *iio_device_id,
                               const sensor_config_t *sensor_config) {
  if (!device_name || !iio_device_id || !sensor_config)
    return NULL;

  if (validate_iio_device_id(iio_device_id) != 0) {
    fprintf(stderr, "[sensor_hal] invalid or inaccessible IIO device: '%s'\n",
            iio_device_id);
    return NULL;
  }

  hw_device_t *dev = malloc(sizeof(hw_device_t));
  if (!dev)
    return NULL;

  if (hal_device_init(dev, device_name, HAL_DEVICE_TYPE_SENSOR) !=
      HAL_SUCCESS) {
    free(dev);
    return NULL;
  }

  sensor_priv_t *priv = calloc(1u, sizeof(sensor_priv_t));
  if (!priv) {
    hal_device_destroy(dev);
    free(dev);
    return NULL;
  }

  snprintf(priv->iio_sysfs_path, sizeof(priv->iio_sysfs_path), "%s/%s",
           IIO_BASE_PATH, iio_device_id);

  snprintf(priv->iio_dev_node, sizeof(priv->iio_dev_node), "%s%s",
           IIO_DEV_PREFIX, iio_device_id);

  memcpy(&priv->config, sensor_config, sizeof(sensor_config_t));
  priv->scale_value = 1.0f;
  priv->offset_x = 0.0f;
  priv->offset_y = 0.0f;
  priv->offset_z = 0.0f;
  priv->buffer_fd = -1;

  dev->ops = &sensor_ops;
  dev->priv = priv;
  dev->cleanup = sensor_priv_cleanup;

  printf("[sensor_hal] created '%s' for '%s' (%s)\n", device_name,
         iio_device_id, sensor_hal_type_string(sensor_config->type));
  return dev;
}

void sensor_hal_destroy(hw_device_t *device_ptr) {
  hal_device_unref(device_ptr);
}

int sensor_hal_read_3axis(hw_device_t *device_ptr,
                          sensor_data_3axis_t *data_out) {
  if (!device_ptr || !data_out)
    return HAL_ERROR_INVALID;

  ssize_t bytes =
      device_ptr->ops->read(device_ptr, data_out, sizeof(sensor_data_3axis_t));
  return (bytes < 0) ? (int)bytes : HAL_SUCCESS;
}

int sensor_hal_read_1axis(hw_device_t *device_ptr,
                          sensor_data_1axis_t *data_out) {
  if (!device_ptr || !data_out)
    return HAL_ERROR_INVALID;

  ssize_t bytes =
      device_ptr->ops->read(device_ptr, data_out, sizeof(sensor_data_1axis_t));
  return (bytes < 0) ? (int)bytes : HAL_SUCCESS;
}
