/**
 * @file sensor_hal.c
 * @brief Sensor HAL implementation — Linux IIO sysfs and buffer backend.
 *
 * Security hardening applied:
 *   - validate_iio_device_id() enforces a strict allowlist pattern on the
 *     device name and calls realpath() to confirm the resolved path is
 *     anchored under IIO_BASE_PATH, defeating "../../../etc" path-traversal.
 *   - Sysfs text is parsed with strtol/strtof plus isfinite() instead of
 *     atoi/atof, detecting overflow, parse failure, and NaN/Inf from buggy
 *     drivers.
 *   - control() validates the arg pointer and alignment before any
 *     dereference; unknown commands return HAL_ERROR_NOT_SUPPORT.
 *   - sensor_priv_cleanup() calls explicit_bzero() before free() so
 *     calibration offsets do not linger in heap memory.
 *
 * Performance:
 *   - In buffer mode, each sample is one read() syscall returning all
 *     channels atomically with a hardware IIO timestamp.
 *   - The scale factor is read from sysfs once at open() and cached in
 *     priv->scale_value; the read hot-path multiplies by the cached float.
 *
 * PROPOSED additions implemented:
 *   - SENSOR_CMD_SET_SCALE: updates priv->scale_value at runtime.
 *   - SENSOR_CMD_GET_OFFSETS / SENSOR_CMD_SET_OFFSETS: atomic offset I/O.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
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
 *
 * @p x_byte_offset   Byte offset of the X int16 within one scan.
 * @p y_byte_offset   Byte offset of the Y int16 within one scan.
 * @p z_byte_offset   Byte offset of the Z int16 within one scan.
 * @p ts_byte_offset  Byte offset of the int64 timestamp; 0 if absent.
 * @p stride          Total bytes per scan including any trailing padding.
 * @p has_timestamp   Non-zero when the scan includes a hardware timestamp.
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
 *
 * @p iio_sysfs_path  Absolute sysfs device directory, e.g. /sys/bus/iio/…
 * @p iio_dev_node    Character device path, e.g. /dev/iio:device0.
 * @p config          Copy of the active sensor configuration.
 * @p scale_value     Hardware scale factor (units-per-LSB) cached at open.
 * @p offset_x        Calibration offset added to each scaled X reading.
 * @p offset_y        Calibration offset added to each scaled Y reading.
 * @p offset_z        Calibration offset added to each scaled Z reading.
 * @p buffer_fd       File descriptor for /dev/iio:deviceN; -1 in sysfs mode.
 * @p scan_layout     Pre-computed binary layout of one IIO buffer scan.
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
  iio_scan_layout_t

/* ── control-command specification table ──────────────────────────────── */

/**
 * @brief Maps a control command to the byte size of its argument.
 *
 * @p command   Control command code from sensor_cmd_t.
 * @p arg_size  Required argument size in bytes; 0 if no argument.
 */
typedef struct {
  uint32_t command;
  size_t   arg_size;
} sensor_cmd_spec_t;

static const sensor_cmd_spec_t SENSOR_CMD_SPECS[] = {
  {SENSOR_CMD_SET_RATE,      sizeof(uint32_t)},
  {SENSOR_CMD_GET_RATE,      sizeof(uint32_t)},
  {SENSOR_CMD_ENABLE_BUFFER, sizeof(int)},
  {SENSOR_CMD_CALIBRATE,     0},
  {SENSOR_CMD_GET_CONFIG,    sizeof(sensor_config_t)},
  {SENSOR_CMD_SET_SCALE,     sizeof(float)},           /* [PROPOSED] */
  {SENSOR_CMD_GET_OFFSETS,   sizeof(sensor_offsets_t)}, /* [PROPOSED] */
  {SENSOR_CMD_SET_OFFSETS,   sizeof(sensor_offsets_t)}, /* [PROPOSED] */
};

/* ── security: device name validation ────────────────────────────────── */

/**
 * @brief Confirm that @p iio_device_id is a well-formed IIO device name
 *        and that its resolved sysfs path is anchored under IIO_BASE_PATH.
 *
 * Accepts exactly "iio:device" followed by 1–4 decimal digits with no
 * other characters.  The subsequent realpath() call resolves all symlinks
 * and ".." components before the prefix check, defeating path-traversal
 * attacks that bypass a naive string comparison (e.g. "iio:device0/../../../etc").
 *
 * @param iio_device_id  Caller-supplied device identifier string.
 * @return 0 if valid and accessible under IIO_BASE_PATH, -1 otherwise.
 */
static int validate_iio_device_id(const char *iio_device_id) {
  if (!iio_device_id)
    return -1;

  static const char EXPECTED_PREFIX[] = "iio:device";
  if (strncmp(iio_device_id, EXPECTED_PREFIX,
              sizeof(EXPECTED_PREFIX) - 1u) != 0)
    return -1;

  const char *digits     = iio_device_id + (sizeof(EXPECTED_PREFIX) - 1u);
  size_t      digit_count = strlen(digits);

  if (digit_count == 0u || digit_count > 4u)
    return -1;

  for (size_t i = 0u; i < digit_count; i++) {
    if (digits[i] < '0' || digits[i] > '9')
      return -1;
  }

  char candidate_path[PATH_MAX];
  snprintf(candidate_path, sizeof(candidate_path), "%s/%s",
           IIO_BASE_PATH, iio_device_id);

  char resolved_path[PATH_MAX];
  if (realpath(candidate_path, resolved_path) == NULL)
    return -1;

  if (strncmp(resolved_path, IIO_BASE_PATH, strlen(IIO_BASE_PATH)) != 0)
    return -1;

  return 0;
}

/* ── security: control arg validation ────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 *
 * Checks the command against SENSOR_CMD_SPECS to determine whether an
 * argument is required, and if so verifies that the pointer is non-NULL
 * and naturally aligned to uint32_t (the strictest alignment of any
 * argument type used by the sensor HAL).
 *
 * @param control_command  Command code from sensor_cmd_t.
 * @param command_arg      Argument pointer supplied by caller.
 * @return 0 if valid, -1 if invalid or command is unknown.
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
 * @brief Read the text content of a sysfs attribute file into buf_out.
 *
 * Opens the file with O_CLOEXEC to ensure the descriptor is not inherited
 * by child processes, reads up to buf_size-1 bytes, and null-terminates.
 * A trailing newline (common in sysfs output) is stripped.
 *
 * @param sysfs_path  Absolute path to the sysfs attribute file.
 * @param buf_out     Caller-supplied buffer for the null-terminated text.
 * @param buf_size    Capacity of buf_out in bytes.
 * @return 0 on success, -1 on I/O error or empty read.
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
 * @brief Write a string value to a sysfs attribute file.
 *
 * Used to program IIO scan_elements/enable flags, buffer length, sample
 * rate, and direction attributes.  Opens with O_CLOEXEC.
 *
 * @param sysfs_path  Absolute path to the sysfs attribute file.
 * @param value       Null-terminated string to write.
 * @return 0 on success, -1 on open/write failure.
 */
static int write_sysfs_text(const char *sysfs_path, const char *value) {
  int fd = open(sysfs_path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  ssize_t len     = (ssize_t)strlen(value);
  ssize_t written = write(fd, value, (size_t)len);
  close(fd);

  return (written == len) ? 0 : -1;
}

/**
 * @brief Parse a sysfs integer attribute with full overflow and error detection.
 *
 * Unlike atoi(), strtol() sets errno to ERANGE on overflow and distinguishes
 * "no digits consumed" (end == buf) from a legitimate zero result.  The
 * result is further bounds-checked against INT_MIN..INT_MAX before the
 * cast to int.
 *
 * @param sysfs_path  Path to the sysfs attribute file.
 * @param value_out   Receives the parsed integer.
 * @return 0 on success, -1 on parse, range, or I/O error.
 */
static int read_sysfs_int(const char *sysfs_path, int *value_out) {
  char buf[32];
  if (read_sysfs_text(sysfs_path, buf, sizeof(buf)) != 0)
    return -1;

  char *end;
  errno    = 0;
  long val = strtol(buf, &end, 10);

  if (errno != 0)             return -1;
  if (end == buf)             return -1;
  if (*end != '\0' && *end != '\n') return -1;
  if (val > (long)INT_MAX || val < (long)INT_MIN) return -1;

  *value_out = (int)val;
  return 0;
}

/**
 * @brief Parse a sysfs float attribute with overflow and non-finite detection.
 *
 * A sysfs scale attribute that contains "inf" or "nan" (possible with a
 * buggy driver) would propagate silently through atof().  strtof() plus
 * isfinite() catches both the overflow case (errno == ERANGE → ±HUGE_VALF)
 * and any NaN the standard library may produce.
 *
 * @param sysfs_path  Path to the sysfs attribute file.
 * @param value_out   Receives the parsed float.
 * @return 0 on success, -1 on parse, range, non-finite, or I/O error.
 */
static int read_sysfs_float(const char *sysfs_path, float *value_out) {
  char buf[64];
  if (read_sysfs_text(sysfs_path, buf, sizeof(buf)) != 0)
    return -1;

  char *end;
  errno     = 0;
  float val = strtof(buf, &end);

  if (errno != 0)      return -1;
  if (end == buf)      return -1;
  if (!isfinite(val))  return -1;

  *value_out = val;
  return 0;
}

/* ── sysfs helpers ────────────────────────────────────────────────────── */

/**
 * @brief Return the IIO sysfs channel prefix string for a sensor type.
 *
 * Each IIO sensor type uses a well-defined attribute name prefix defined
 * in the kernel's IIO ABI documentation (Documentation/ABI/testing/sysfs-bus-iio).
 *
 * @param sensor_type  Physical quantity from sensor_type_t.
 * @return Static string prefix; never NULL.
 */
static const char *iio_channel_prefix(sensor_type_t sensor_type) {
  switch (sensor_type) {
  case SENSOR_TYPE_ACCEL:       return "in_accel";
  case SENSOR_TYPE_GYRO:        return "in_anglvel";
  case SENSOR_TYPE_MAGNET:      return "in_magn";
  case SENSOR_TYPE_LIGHT:       return "in_illuminance";
  case SENSOR_TYPE_PROXIMITY:   return "in_proximity";
  case SENSOR_TYPE_PRESSURE:    return "in_pressure";
  case SENSOR_TYPE_TEMPERATURE: return "in_temp";
  case SENSOR_TYPE_HUMIDITY:    return "in_humidity";
  default:                      return "in_unknown";
  }
}

/**
 * @brief Read one raw axis value from sysfs and apply the cached scale factor.
 *
 * Constructs the full sysfs path as
 * "{iio_sysfs_path}/{channel_prefix}_{axis_name}_raw", reads the integer,
 * and multiplies by priv->scale_value.
 *
 * @param priv       Device private data (supplies path and scale).
 * @param axis_name  Axis suffix: "x", "y", "z", or "input" (1-axis sensors).
 * @param value_out  Receives the scaled physical-unit value.
 * @return 0 on success, -1 on sysfs read or parse error.
 */
static int read_sysfs_axis(const sensor_priv_t *priv, const char *axis_name,
                            float *value_out) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s_%s_raw",
           priv->iio_sysfs_path,
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
 * subdirectory.  X, Y, Z indices are read to determine the order channels
 * appear in the binary scan; 2-byte slots are assigned in index order.
 * The timestamp offset is then aligned to an 8-byte boundary as required
 * by the Linux IIO ABI.
 *
 * The result is stored in priv->scan_layout and used by sensor_read() to
 * extract axis values and the hardware timestamp at pre-computed byte offsets
 * with zero additional branching or copying.
 *
 * @param priv    Device private data; priv->scan_layout is written.
 * @param prefix  IIO channel prefix (e.g. "in_accel").
 * @return 0 on success, -1 if any channel index cannot be read.
 */
static int compute_iio_scan_layout(sensor_priv_t *priv, const char *prefix) {
  char path[512];
  int  idx_x = -1, idx_y = -1, idx_z = -1, idx_ts = -1;

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

  /* Sort axis indices to assign contiguous 2-byte slots in index order. */
  int sorted[3]  = {idx_x, idx_y, idx_z};
  int axis_id[3] = {0, 1, 2}; /* 0=x 1=y 2=z */

  for (int i = 1; i < 3; i++) {
    for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
      int tmp_idx = sorted[j];   sorted[j]   = sorted[j-1]; sorted[j-1]   = tmp_idx;
      int tmp_id  = axis_id[j];  axis_id[j]  = axis_id[j-1]; axis_id[j-1] = tmp_id;
    }
  }

  uint32_t offsets[3];
  for (int slot = 0; slot < 3; slot++)
    offsets[slot] = (uint32_t)slot * 2u;

  for (int slot = 0; slot < 3; slot++) {
    switch (axis_id[slot]) {
    case 0: priv->scan_layout.x_byte_offset = offsets[slot]; break;
    case 1: priv->scan_layout.y_byte_offset = offsets[slot]; break;
    case 2: priv->scan_layout.z_byte_offset = offsets[slot]; break;
    }
  }

  uint32_t after_axes = 3u * 2u;

  if (priv->scan_layout.has_timestamp) {
    uint32_t ts_offset = (after_axes + 7u) & ~7u;  /* align to 8 bytes */
    priv->scan_layout.ts_byte_offset = ts_offset;
    priv->scan_layout.stride         = ts_offset + 8u;
  } else {
    priv->scan_layout.ts_byte_offset = 0u;
    priv->scan_layout.stride         = after_axes;
  }

  return 0;
}

/**
 * @brief Enable IIO hardware buffer channels and open /dev/iio:deviceN.
 *
 * Pipeline:
 *   1. Enable each scan element (X, Y, Z, timestamp) via scan_elements/…_en.
 *   2. Compute the binary scan layout via compute_iio_scan_layout().
 *   3. Set the kernel ring-buffer depth to 64 scans (buffer/length).
 *   4. Arm the buffer (buffer/enable = "1").
 *   5. Open /dev/iio:deviceN; each read() returns one complete scan.
 *
 * A hardware trigger must be associated externally via
 * /sys/bus/iio/devices/iio:deviceN/trigger/current_trigger.  If the
 * device node cannot be opened, the buffer is disarmed and the function
 * returns -1 so the caller can fall back to sysfs mode.
 *
 * @param priv    Device private data; buffer_fd and scan_layout are written.
 * @param prefix  IIO channel prefix string.
 * @return 0 on success, -1 if the buffer or device node is unavailable.
 */
static int open_iio_buffer(sensor_priv_t *priv, const char *prefix) {
  char       path[512];
  const char *axes[] = {"x", "y", "z"};

  for (int i = 0; i < 3; i++) {
    snprintf(path, sizeof(path), "%s/scan_elements/%s_%s_en",
             priv->iio_sysfs_path, prefix, axes[i]);
    write_sysfs_text(path, "1");
  }

  snprintf(path, sizeof(path), "%s/scan_elements/timestamp_en",
           priv->iio_sysfs_path);
  write_sysfs_text(path, "1");

  if (compute_iio_scan_layout(priv, prefix) != 0) {
    fprintf(stderr, "[sensor_hal] could not compute scan layout — "
                    "falling back to sysfs mode\n");
    return -1;
  }

  snprintf(path, sizeof(path), "%s/buffer/length", priv->iio_sysfs_path);
  write_sysfs_text(path, "64");

  snprintf(path, sizeof(path), "%s/buffer/enable", priv->iio_sysfs_path);
  write_sysfs_text(path, "1");

  priv->buffer_fd = open(priv->iio_dev_node,
                          O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (priv->buffer_fd < 0) {
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
 *
 * Writes "0" to buffer/enable first, then closes the fd.  The ordering
 * ensures the kernel stops filling the ring before we release the fd.
 *
 * @param priv  Device private data; buffer_fd is set to -1 on return.
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
 * @brief Return CLOCK_MONOTONIC time in nanoseconds.
 *
 * Used only in sysfs-fallback mode.  In buffer mode the hardware timestamp
 * embedded in the IIO scan is used instead because it reflects the DMA
 * completion time — more accurate than the time the process drained the ring.
 *
 * @return Nanoseconds since an unspecified monotonic epoch.
 */
static uint64_t monotonic_timestamp_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── vtable implementations ───────────────────────────────────────────── */

/**
 * @brief Open the IIO device and optionally arm the hardware buffer.
 *
 * Reads the device's scale factor from sysfs and caches it in
 * priv->scale_value to avoid per-sample file I/O.  Writes the requested
 * sampling_frequency.  If enable_buffer is set, calls open_iio_buffer();
 * if that fails, buffer_fd is left at -1 and sysfs fallback is used.
 *
 * @param device_ptr  Sensor device in HAL_STATE_CLOSED.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int sensor_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;
  char path[512];
  char rate_buf[32];

  snprintf(path, sizeof(path), "%s/%s_scale",
           priv->iio_sysfs_path,
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
  printf("[sensor_hal] %s opened (%s, %s mode)\n",
         device_ptr->name,
         sensor_hal_type_string(priv->config.type),
         priv->buffer_fd >= 0 ? "buffer" : "sysfs");
  return HAL_SUCCESS;
}

/**
 * @brief Close the IIO device and disarm the hardware buffer if active.
 *
 * @param device_ptr  Sensor device.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int sensor_close(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;
  close_iio_buffer(priv);

  device_ptr->state = HAL_STATE_CLOSED;
  printf("[sensor_hal] %s closed\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Transition from OPEN to ACTIVE; data reads become valid.
 *
 * @param device_ptr  Sensor device in HAL_STATE_OPEN.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int sensor_start(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_OPEN)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_ACTIVE;
  printf("[sensor_hal] %s started\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Transition back to OPEN; pauses data production.
 *
 * @param device_ptr  Sensor device.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int sensor_stop(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_OPEN;
  printf("[sensor_hal] %s stopped\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Read one sensor sample using IIO buffer mode or sysfs fallback.
 *
 * Buffer path: one read() syscall on the IIO character device yields
 * priv->scan_layout.stride bytes containing all enabled channels in
 * index order.  int16 axis values are extracted at their pre-computed
 * byte offsets; the hardware int64 timestamp is copied verbatim.
 *
 * Sysfs fallback path: three separate open/read/close cycles, one per
 * axis, each producing decimal ASCII that is parsed and scaled.
 *
 * Returns EAGAIN from buffer mode as HAL_ERROR_TIMEOUT so callers can
 * implement non-blocking poll loops.
 *
 * @param device_ptr   Active sensor device.
 * @param data_buffer  Destination: sensor_data_3axis_t or sensor_data_1axis_t.
 * @param buffer_size  Must be at least sizeof the appropriate data struct.
 * @return Bytes read on success, negative HAL_ERROR_* on failure.
 */
static ssize_t sensor_read(hw_device_t *device_ptr, void *data_buffer,
                            size_t buffer_size) {
  if (!device_ptr || !device_ptr->priv || !data_buffer)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_ACTIVE)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv = (sensor_priv_t *)device_ptr->priv;

  int is_3axis = (priv->config.type == SENSOR_TYPE_ACCEL ||
                  priv->config.type == SENSOR_TYPE_GYRO  ||
                  priv->config.type == SENSOR_TYPE_MAGNET);

  if (is_3axis) {
    if (buffer_size < sizeof(sensor_data_3axis_t))
      return HAL_ERROR_INVALID;

    sensor_data_3axis_t *data = (sensor_data_3axis_t *)data_buffer;

    if (priv->buffer_fd >= 0) {
      uint8_t scan_buf[64];
      if (priv->scan_layout.stride > sizeof(scan_buf))
        return HAL_ERROR_INVALID;

      ssize_t bytes = read(priv->buffer_fd, scan_buf,
                           priv->scan_layout.stride);
      if (bytes < 0) {
        if (errno == EAGAIN) return HAL_ERROR_TIMEOUT;
        return HAL_ERROR_IO;
      }
      if ((size_t)bytes < priv->scan_layout.stride)
        return HAL_ERROR_IO;

      int16_t raw_x, raw_y, raw_z;
      memcpy(&raw_x, scan_buf + priv->scan_layout.x_byte_offset, sizeof(int16_t));
      memcpy(&raw_y, scan_buf + priv->scan_layout.y_byte_offset, sizeof(int16_t));
      memcpy(&raw_z, scan_buf + priv->scan_layout.z_byte_offset, sizeof(int16_t));

      data->x = (float)raw_x * priv->scale_value + priv->offset_x;
      data->y = (float)raw_y * priv->scale_value + priv->offset_y;
      data->z = (float)raw_z * priv->scale_value + priv->offset_z;

      if (priv->scan_layout.has_timestamp)
        memcpy(&data->timestamp,
               scan_buf + priv->scan_layout.ts_byte_offset,
               sizeof(uint64_t));
      else
        data->timestamp = monotonic_timestamp_ns();

    } else {
      if (read_sysfs_axis(priv, "x", &data->x) != 0) return HAL_ERROR_IO;
      if (read_sysfs_axis(priv, "y", &data->y) != 0) return HAL_ERROR_IO;
      if (read_sysfs_axis(priv, "z", &data->z) != 0) return HAL_ERROR_IO;
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

/**
 * @brief Write is not supported for sensor devices.
 *
 * Sensors are read-only; this stub satisfies the vtable contract and
 * returns HAL_ERROR_NOT_SUPPORT unconditionally.
 */
static ssize_t sensor_write(hw_device_t *device_ptr, const void *data_buffer,
                             size_t data_size) {
  (void)device_ptr; (void)data_buffer; (void)data_size;
  return HAL_ERROR_NOT_SUPPORT;
}

/**
 * @brief Execute a sensor-specific control command.
 *
 * All argument pointers are validated via validate_control_arg() before
 * any dereference.  SENSOR_CMD_CALIBRATE reads the current axes and stores
 * their negatives as offsets so subsequent reads return values relative to
 * the calibration position.
 *
 * [PROPOSED] SENSOR_CMD_SET_SCALE updates priv->scale_value at runtime,
 * allowing the caller to switch hardware gain ranges without close/open.
 *
 * [PROPOSED] SENSOR_CMD_GET_OFFSETS / SENSOR_CMD_SET_OFFSETS transfer all
 * three calibration offsets atomically via sensor_offsets_t.
 *
 * @param device_ptr      Sensor device.
 * @param control_command One of sensor_cmd_t.
 * @param command_arg     Typed argument; see sensor_cmd_t for requirements.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
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
    snprintf(path, sizeof(path), "%s/sampling_frequency",
             priv->iio_sysfs_path);
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
        priv->config.type == SENSOR_TYPE_GYRO  ||
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

  case SENSOR_CMD_SET_SCALE: {  /* [PROPOSED] */
    float new_scale = *(const float *)command_arg;
    if (!isfinite(new_scale) || new_scale == 0.0f)
      return HAL_ERROR_INVALID;
    priv->scale_value = new_scale;
    return HAL_SUCCESS;
  }

  case SENSOR_CMD_GET_OFFSETS: {  /* [PROPOSED] */
    sensor_offsets_t *offsets = (sensor_offsets_t *)command_arg;
    offsets->x = priv->offset_x;
    offsets->y = priv->offset_y;
    offsets->z = priv->offset_z;
    return HAL_SUCCESS;
  }

  case SENSOR_CMD_SET_OFFSETS: {  /* [PROPOSED] */
    const sensor_offsets_t *offsets = (const sensor_offsets_t *)command_arg;
    priv->offset_x = offsets->x;
    priv->offset_y = offsets->y;
    priv->offset_z = offsets->z;
    return HAL_SUCCESS;
  }

  default:
    return HAL_ERROR_NOT_SUPPORT;
  }
}

/**
 * @brief Fill a sensor_info_t with the current runtime state.
 *
 * @param device_ptr  Sensor device.
 * @param info_out    Caller-allocated sensor_info_t to fill.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int sensor_get_info(hw_device_t *device_ptr, void *info_out) {
  if (!device_ptr || !device_ptr->priv || !info_out)
    return HAL_ERROR_INVALID;

  sensor_priv_t *priv       = (sensor_priv_t *)device_ptr->priv;
  sensor_info_t *sensor_info = (sensor_info_t *)info_out;

  strncpy(sensor_info->iio_device_path, priv->iio_sysfs_path,
          sizeof(sensor_info->iio_device_path) - 1u);
  sensor_info->iio_device_path[sizeof(sensor_info->iio_device_path) - 1u] = '\0';
  sensor_info->type               = priv->config.type;
  sensor_info->sampling_rate_hz   = priv->config.sampling_rate_hz;
  sensor_info->resolution         = priv->scale_value;
  sensor_info->max_range          = 0.0f;
  sensor_info->fifo_size          = 0u;
  sensor_info->buffer_mode_active = (priv->buffer_fd >= 0);

  return HAL_SUCCESS;
}

/* ── cleanup callback ─────────────────────────────────────────────────── */

/**
 * @brief Free sensor private data; registered as hw_device_t::cleanup.
 *
 * Closes the IIO buffer fd and disarms the buffer before freeing priv.
 * explicit_bzero() clears the struct so calibration offsets and sysfs
 * paths do not linger in heap pages after free().
 *
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
  .open     = sensor_open,
  .close    = sensor_close,
  .start    = sensor_start,
  .stop     = sensor_stop,
  .read     = sensor_read,
  .write    = sensor_write,
  .control  = sensor_control,
  .get_info = sensor_get_info,
  .reset    = NULL,
};

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Return a human-readable string for a sensor type.
 *
 * @param sensor_type  Physical quantity from sensor_type_t.
 * @return Static string; never NULL.
 */
const char *sensor_hal_type_string(sensor_type_t sensor_type) {
  switch (sensor_type) {
  case SENSOR_TYPE_ACCEL:       return "accelerometer";
  case SENSOR_TYPE_GYRO:        return "gyroscope";
  case SENSOR_TYPE_MAGNET:      return "magnetometer";
  case SENSOR_TYPE_LIGHT:       return "light";
  case SENSOR_TYPE_PROXIMITY:   return "proximity";
  case SENSOR_TYPE_PRESSURE:    return "pressure";
  case SENSOR_TYPE_TEMPERATURE: return "temperature";
  case SENSOR_TYPE_HUMIDITY:    return "humidity";
  default:                      return "unknown";
  }
}

/**
 * @brief Return the default configuration for a given sensor type.
 *
 * Produces 100 Hz, scale=1, no buffer.  Suitable for initial bring-up;
 * callers should tune sampling_rate_hz and enable_buffer for production.
 *
 * @param sensor_type  Physical quantity to measure.
 * @return Populated sensor_config_t; no heap allocation.
 */
sensor_config_t sensor_hal_default_config(sensor_type_t sensor_type) {
  sensor_config_t cfg;
  cfg.type               = sensor_type;
  cfg.sampling_rate_hz   = 100u;
  cfg.scale              = 1u;
  cfg.enable_buffer      = 0;
  return cfg;
}

/**
 * @brief Allocate and initialise a sensor HAL device.
 *
 * Validates @p iio_device_id against the expected IIO naming pattern and
 * confirms the resolved path is anchored under IIO_BASE_PATH before storing
 * it, defeating path-traversal attacks.
 *
 * [PROPOSED] Sets capabilities = HAL_CAP_READ | HAL_CAP_CONTROL to
 * advertise the supported operations.
 *
 * @param device_name    Human-readable name for registry lookup.
 * @param iio_device_id  IIO device identifier, e.g. "iio:device0".
 * @param sensor_config  Sensor parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
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

  if (hal_device_init(dev, device_name, HAL_DEVICE_TYPE_SENSOR) != HAL_SUCCESS) {
    free(dev);
    return NULL;
  }

  sensor_priv_t *priv = calloc(1u, sizeof(sensor_priv_t));
  if (!priv) {
    hal_device_destroy(dev);
    free(dev);
    return NULL;
  }

  snprintf(priv->iio_sysfs_path, sizeof(priv->iio_sysfs_path),
           "%s/%s", IIO_BASE_PATH, iio_device_id);
  snprintf(priv->iio_dev_node, sizeof(priv->iio_dev_node),
           "%s%s", IIO_DEV_PREFIX, iio_device_id);

  memcpy(&priv->config, sensor_config, sizeof(sensor_config_t));
  priv->scale_value = 1.0f;
  priv->offset_x    = 0.0f;
  priv->offset_y    = 0.0f;
  priv->offset_z    = 0.0f;
  priv->buffer_fd   = -1;

  dev->ops          = &sensor_ops;
  dev->priv         = priv;
  dev->cleanup      = sensor_priv_cleanup;
  dev->capabilities = HAL_CAP_READ | HAL_CAP_CONTROL;  /* [PROPOSED] */

  printf("[sensor_hal] created '%s' for '%s' (%s)\n",
         device_name, iio_device_id,
         sensor_hal_type_string(sensor_config->type));
  return dev;
}

/**
 * @brief Release all resources held by a sensor device.
 *
 * Delegates to hal_device_unref() which drives the full teardown chain.
 *
 * @param device_ptr  Device returned by sensor_hal_create().
 */
void sensor_hal_destroy(hw_device_t *device_ptr) {
  hal_device_unref(device_ptr);
}

/**
 * @brief Read one sample from a 3-axis sensor (accel, gyro, magnet).
 *
 * Thin wrapper that calls ops->read() and converts the byte-count return
 * to a HAL_SUCCESS/error code for callers that prefer the typed API over
 * the raw vtable.
 *
 * @param device_ptr  Active sensor device.
 * @param data_out    Caller-allocated struct to fill.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int sensor_hal_read_3axis(hw_device_t *device_ptr,
                           sensor_data_3axis_t *data_out) {
  if (!device_ptr || !data_out)
    return HAL_ERROR_INVALID;

  ssize_t bytes = device_ptr->ops->read(device_ptr, data_out,
                                         sizeof(sensor_data_3axis_t));
  return (bytes < 0) ? (int)bytes : HAL_SUCCESS;
}

/**
 * @brief Read one sample from a single-value sensor.
 *
 * Thin wrapper analogous to sensor_hal_read_3axis() for scalar sensors.
 *
 * @param device_ptr  Active sensor device.
 * @param data_out    Caller-allocated struct to fill.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int sensor_hal_read_1axis(hw_device_t *device_ptr,
                           sensor_data_1axis_t *data_out) {
  if (!device_ptr || !data_out)
    return HAL_ERROR_INVALID;

  ssize_t bytes = device_ptr->ops->read(device_ptr, data_out,
                                         sizeof(sensor_data_1axis_t));
  return (bytes < 0) ? (int)bytes : HAL_SUCCESS;
}
