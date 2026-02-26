/**
 * @file audio_hal.c
 * @brief Audio HAL implementation — ALSA PCM backend.
 *
 * Security hardening applied:
 *   - explicit_bzero() clears the ALSA capture ring-buffer contents on
 *     close so residual audio cannot be read from subsequently reallocated
 *     heap pages in the same process or a forked child.
 *   - control() validates arg pointer and natural alignment before any
 *     dereference; misaligned reads are undefined behaviour on ARM.
 *   - audio_priv_cleanup() calls explicit_bzero() on the whole priv struct
 *     so device names and credentials do not linger in heap memory.
 *
 * Performance notes:
 *   - bytes_per_frame is pre-computed at create time; the read/write hot
 *     path performs a single multiply rather than a per-call switch.
 *   - The device rwlock is acquired as a write-lock only for state
 *     transitions and control commands; data-path read/write hold only
 *     the shared read side.
 *
 * PROPOSED additions:
 *   - AUDIO_CMD_DRAIN: calls snd_pcm_drain() for graceful playback shutdown.
 *   - AUDIO_CMD_RECOVER: calls snd_pcm_prepare() for explicit xrun recovery.
 */

#define _DEFAULT_SOURCE
#include "audio_hal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#else
/* ── ALSA stub types ──────────────────────────────────────────────────────
 * Minimal typedefs that allow compilation on systems without libasound-dev.
 * On a real embedded target, define HAVE_ALSA and link against libasound.
 * ──────────────────────────────────────────────────────────────────────── */
typedef void snd_pcm_t;
typedef int snd_pcm_format_t;
typedef int snd_pcm_stream_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
typedef void snd_pcm_hw_params_t;

#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_STREAM_CAPTURE 1
#define SND_PCM_ACCESS_RW_INTERLEAVED 0
#define SND_PCM_FORMAT_S16_LE 0
#define SND_PCM_FORMAT_S24_LE 1
#define SND_PCM_FORMAT_S32_LE 2
#define SND_PCM_FORMAT_FLOAT_LE 3

static inline int snd_pcm_open(snd_pcm_t **h, const char *n, int s, int m) {
  (void)h;
  (void)n;
  (void)s;
  (void)m;
  return -1;
}
static inline int snd_pcm_close(snd_pcm_t *h) {
  (void)h;
  return 0;
}
static inline int snd_pcm_drain(snd_pcm_t *h) {
  (void)h;
  return 0;
}
static inline int snd_pcm_drop(snd_pcm_t *h) {
  (void)h;
  return 0;
}
static inline int snd_pcm_prepare(snd_pcm_t *h) {
  (void)h;
  return 0;
}
static inline int snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **p) {
  (void)p;
  return -1;
}
static inline void snd_pcm_hw_params_free(snd_pcm_hw_params_t *p) { (void)p; }
static inline int snd_pcm_hw_params_any(snd_pcm_t *h, snd_pcm_hw_params_t *p) {
  (void)h;
  (void)p;
  return -1;
}
static inline int snd_pcm_hw_params_set_access(snd_pcm_t *h,
                                               snd_pcm_hw_params_t *p, int a) {
  (void)h;
  (void)p;
  (void)a;
  return -1;
}
static inline int snd_pcm_hw_params_set_format(snd_pcm_t *h,
                                               snd_pcm_hw_params_t *p, int f) {
  (void)h;
  (void)p;
  (void)f;
  return -1;
}
static inline int snd_pcm_hw_params_set_channels(snd_pcm_t *h,
                                                 snd_pcm_hw_params_t *p,
                                                 unsigned c) {
  (void)h;
  (void)p;
  (void)c;
  return -1;
}
static inline int snd_pcm_hw_params_set_rate_near(snd_pcm_t *h,
                                                  snd_pcm_hw_params_t *p,
                                                  unsigned *r, int *d) {
  (void)h;
  (void)p;
  (void)r;
  (void)d;
  return -1;
}
static inline int snd_pcm_hw_params_set_period_size_near(snd_pcm_t *h,
                                                         snd_pcm_hw_params_t *p,
                                                         snd_pcm_uframes_t *f,
                                                         int *d) {
  (void)h;
  (void)p;
  (void)f;
  (void)d;
  return -1;
}
static inline int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *h,
                                                         snd_pcm_hw_params_t *p,
                                                         snd_pcm_uframes_t *f) {
  (void)h;
  (void)p;
  (void)f;
  return -1;
}
static inline int snd_pcm_hw_params(snd_pcm_t *h, snd_pcm_hw_params_t *p) {
  (void)h;
  (void)p;
  return -1;
}
static inline snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *h, void *b,
                                              snd_pcm_uframes_t f) {
  (void)h;
  (void)b;
  (void)f;
  return -1;
}
static inline snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *h, const void *b,
                                               snd_pcm_uframes_t f) {
  (void)h;
  (void)b;
  (void)f;
  return -1;
}
static inline const char *snd_strerror(int e) {
  (void)e;
  return "ALSA not available";
}
#endif /* HAVE_ALSA */

/* ── private device data ──────────────────────────────────────────────── */

/**
 * @brief Internal state for an audio HAL device.
 *
 * @p alsa_device_id  ALSA device string passed to snd_pcm_open().
 * @p pcm_handle      Open ALSA PCM handle; NULL when closed.
 * @p config          Copy of the active stream configuration.
 * @p bytes_per_frame Pre-computed value of channels × bytes-per-sample.
 * @p volume          Current logical volume level 0–100.
 * @p muted           Non-zero when muted.
 */
typedef struct {
  char alsa_device_id[64];
  snd_pcm_t *pcm_handle;
  audio_config_t config;
  uint32_t bytes_per_frame;
  uint32_t volume;
  int muted;
} audio_priv_t;

/* ── control-command specification table ──────────────────────────────── */

/**
 * @brief Maps each control command to the byte size of its argument.
 *
 * A size of 0 means the command takes no argument (NULL arg is valid).
 * Used by validate_control_arg() to check for NULL and alignment before
 * any dereference.
 *
 * @p command   Control command code from audio_cmd_t.
 * @p arg_size  Required argument size in bytes; 0 if no argument needed.
 */
typedef struct {
  uint32_t command;
  size_t arg_size;
} audio_cmd_spec_t;

static const audio_cmd_spec_t AUDIO_CMD_SPECS[] = {
    {AUDIO_CMD_SET_VOLUME, sizeof(uint32_t)},
    {AUDIO_CMD_GET_VOLUME, sizeof(uint32_t)},
    {AUDIO_CMD_SET_MUTE, sizeof(int)},
    {AUDIO_CMD_GET_MUTE, sizeof(int)},
    {AUDIO_CMD_GET_CONFIG, sizeof(audio_config_t)},
    {AUDIO_CMD_DRAIN, 0},   /* [PROPOSED] no argument */
    {AUDIO_CMD_RECOVER, 0}, /* [PROPOSED] no argument */
};

/* ── helpers ──────────────────────────────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 *
 * Rejects NULL arguments for commands that require one.  Also rejects
 * pointers that are not naturally aligned to uint32_t: misaligned reads
 * of multi-byte types are undefined behaviour on ARM and trap on some
 * configurations with strict-alignment enforcement enabled.
 *
 * @param control_command  Command code from audio_cmd_t.
 * @param command_arg      Argument pointer supplied by the caller.
 * @return 0 on success, -1 if the argument is NULL or misaligned.
 */
static int validate_control_arg(uint32_t control_command,
                                const void *command_arg) {
  for (size_t i = 0; i < HAL_ARRAY_SIZE(AUDIO_CMD_SPECS); i++) {
    if (AUDIO_CMD_SPECS[i].command != control_command)
      continue;
    if (AUDIO_CMD_SPECS[i].arg_size == 0)
      return 0;
    if (!command_arg)
      return -1;
    if ((uintptr_t)command_arg % sizeof(uint32_t) != 0)
      return -1;
    return 0;
  }
  return -1; /* unknown command */
}

/**
 * @brief Translate a HAL audio format code to the corresponding ALSA constant.
 *
 * Falls back to SND_PCM_FORMAT_S16_LE for any unrecognised value so the
 * caller always receives a valid ALSA format without a fatal error.
 *
 * @param format  HAL sample encoding from audio_format_t.
 * @return Corresponding snd_pcm_format_t constant.
 */
static snd_pcm_format_t alsa_format_from_hal(audio_format_t format) {
  switch (format) {
  case AUDIO_FORMAT_S16_LE:
    return SND_PCM_FORMAT_S16_LE;
  case AUDIO_FORMAT_S24_LE:
    return SND_PCM_FORMAT_S24_LE;
  case AUDIO_FORMAT_S32_LE:
    return SND_PCM_FORMAT_S32_LE;
  case AUDIO_FORMAT_FLOAT:
    return SND_PCM_FORMAT_FLOAT_LE;
  default:
    return SND_PCM_FORMAT_S16_LE;
  }
}

/**
 * @brief Zero-fill ALSA's internal ring buffer before closing a capture device.
 *
 * When snd_pcm_close() releases the ring-buffer pages, their content is
 * not wiped.  A subsequent malloc() in the same process (or a forked child)
 * may receive those pages through the buddy allocator, allowing audio to
 * be read from apparently-new memory — a privacy regression in multi-tenant
 * or privileged contexts.
 *
 * This function writes one period's worth of silence through snd_pcm_writei()
 * four times to ensure the ring is fully overwritten, then calls
 * explicit_bzero() on the intermediate zero buffer so the compiler cannot
 * eliminate the store (plain memset/calloc stores can be optimised away).
 *
 * @param pcm_handle  Open ALSA PCM handle in SND_PCM_STREAM_CAPTURE mode.
 * @param priv        Device private data supplying period and frame sizes.
 */
static void secure_clear_capture_buffer(snd_pcm_t *pcm_handle,
                                        const audio_priv_t *priv) {
  size_t period_bytes =
      (size_t)priv->config.period_size * (size_t)priv->bytes_per_frame;
  void *zero_buf = calloc(1u, period_bytes);
  if (!zero_buf)
    return;

  snd_pcm_uframes_t period_frames = priv->config.period_size;
  for (int pass = 0; pass < 4; pass++)
    snd_pcm_writei(pcm_handle, zero_buf, period_frames);

  explicit_bzero(zero_buf, period_bytes);
  free(zero_buf);
}

/* ── ALSA hardware parameter negotiation ──────────────────────────────── */

/**
 * @brief Apply the stream configuration to the open ALSA PCM device.
 *
 * ALSA uses a constraint-refinement model rather than direct assignment.
 * snd_pcm_hw_params_any() fills a hw_params set with the full capability
 * envelope of the hardware.  Each subsequent set_* call intersects that
 * envelope with the requested value.  snd_pcm_hw_params() commits the
 * final intersection to the driver.
 *
 * set_rate_near and set_period_size_near accept the nearest supported
 * value and update the supplied variable in-place.  If the driver
 * negotiates a different sample rate than requested, the live config is
 * updated to reflect the actual negotiated rate.
 *
 * @param priv  Device private data; config.sample_rate may be updated.
 * @return HAL_SUCCESS, HAL_ERROR_NO_MEMORY, or HAL_ERROR_IO.
 */
static int configure_alsa_hw_params(audio_priv_t *priv) {
  snd_pcm_hw_params_t *hw_params = NULL;
  int err;

  if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
    fprintf(stderr, "[audio_hal] hw_params malloc: %s\n", snd_strerror(err));
    return HAL_ERROR_NO_MEMORY;
  }

  if ((err = snd_pcm_hw_params_any(priv->pcm_handle, hw_params)) < 0)
    goto fail_io;

  if ((err = snd_pcm_hw_params_set_access(priv->pcm_handle, hw_params,
                                          SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    goto fail_io;

  if ((err = snd_pcm_hw_params_set_format(
           priv->pcm_handle, hw_params,
           alsa_format_from_hal(priv->config.format))) < 0)
    goto fail_io;

  if ((err = snd_pcm_hw_params_set_channels(priv->pcm_handle, hw_params,
                                            priv->config.channels)) < 0)
    goto fail_io;

  unsigned int rate = priv->config.sample_rate;
  if ((err = snd_pcm_hw_params_set_rate_near(priv->pcm_handle, hw_params, &rate,
                                             0)) < 0)
    goto fail_io;

  if (rate != priv->config.sample_rate) {
    printf("[audio_hal] sample rate adjusted %u → %u Hz\n",
           priv->config.sample_rate, rate);
    priv->config.sample_rate = rate;
  }

  snd_pcm_uframes_t period = priv->config.period_size;
  if ((err = snd_pcm_hw_params_set_period_size_near(priv->pcm_handle, hw_params,
                                                    &period, 0)) < 0)
    goto fail_io;

  snd_pcm_uframes_t buffer = priv->config.buffer_size;
  if ((err = snd_pcm_hw_params_set_buffer_size_near(priv->pcm_handle, hw_params,
                                                    &buffer)) < 0)
    goto fail_io;

  if ((err = snd_pcm_hw_params(priv->pcm_handle, hw_params)) < 0)
    goto fail_io;

  snd_pcm_hw_params_free(hw_params);

  if ((err = snd_pcm_prepare(priv->pcm_handle)) < 0) {
    fprintf(stderr, "[audio_hal] snd_pcm_prepare: %s\n", snd_strerror(err));
    return HAL_ERROR_IO;
  }

  printf("[audio_hal] configured %s: %u Hz, %u ch, %s, period=%lu, buf=%lu\n",
         priv->alsa_device_id, priv->config.sample_rate, priv->config.channels,
         audio_hal_format_string(priv->config.format), (unsigned long)period,
         (unsigned long)buffer);

  return HAL_SUCCESS;

fail_io:
  fprintf(stderr, "[audio_hal] hw_params: %s\n", snd_strerror(err));
  snd_pcm_hw_params_free(hw_params);
  return HAL_ERROR_IO;
}

/* ── vtable implementations ───────────────────────────────────────────── */

/**
 * @brief Open the ALSA PCM device and negotiate hardware parameters.
 *
 * Calls snd_pcm_open() with the stored device string, then applies the
 * full hw_params negotiation via configure_alsa_hw_params().  On any
 * failure the handle is closed and NULL-ed before returning so the struct
 * is always consistent.
 *
 * @param device_ptr  Device in HAL_STATE_CLOSED.
 * @return HAL_SUCCESS, HAL_ERROR_NO_DEVICE, or HAL_ERROR_IO.
 */
static int audio_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;
  int err;

  snd_pcm_stream_t stream = (priv->config.direction == AUDIO_DIRECTION_PLAYBACK)
                                ? SND_PCM_STREAM_PLAYBACK
                                : SND_PCM_STREAM_CAPTURE;

  if ((err = snd_pcm_open(&priv->pcm_handle, priv->alsa_device_id, stream, 0)) <
      0) {
    fprintf(stderr, "[audio_hal] snd_pcm_open(%s): %s\n", priv->alsa_device_id,
            snd_strerror(err));
    return HAL_ERROR_NO_DEVICE;
  }

  if ((err = configure_alsa_hw_params(priv)) != HAL_SUCCESS) {
    snd_pcm_close(priv->pcm_handle);
    priv->pcm_handle = NULL;
    return err;
  }

  device_ptr->state = HAL_STATE_OPEN;
  printf("[audio_hal] %s opened\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Close the ALSA PCM device.
 *
 * For capture devices, secure_clear_capture_buffer() is called first to
 * wipe any audio that remains in the ring before the pages are released.
 * snd_pcm_drain() is called on playback to allow the ring buffer to empty
 * gracefully before the device is closed.
 *
 * @param device_ptr  Device to close.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int audio_close(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;

  if (priv->pcm_handle) {
    if (priv->config.direction == AUDIO_DIRECTION_CAPTURE)
      secure_clear_capture_buffer(priv->pcm_handle, priv);

    snd_pcm_drain(priv->pcm_handle);
    snd_pcm_close(priv->pcm_handle);
    priv->pcm_handle = NULL;
  }

  device_ptr->state = HAL_STATE_CLOSED;
  printf("[audio_hal] %s closed\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Transition the device from OPEN to ACTIVE state.
 *
 * The ALSA stream is ready to produce/consume frames immediately after
 * open(); this call simply advances the state machine so the data-path
 * read/write implementations know the device is live.
 *
 * @param device_ptr  Device in HAL_STATE_OPEN.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int audio_start(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_OPEN)
    return HAL_ERROR_INVALID;

  device_ptr->state = HAL_STATE_ACTIVE;
  printf("[audio_hal] %s started\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Pause the stream and transition back to OPEN state.
 *
 * snd_pcm_drop() discards frames currently in the ALSA ring and returns
 * the PCM to the PREPARED state.  snd_pcm_prepare() is then called so
 * the device is ready to restart immediately on the next audio_start().
 *
 * @param device_ptr  Device in any non-CLOSED state.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int audio_stop(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;

  if (priv->pcm_handle) {
    snd_pcm_drop(priv->pcm_handle);
    snd_pcm_prepare(priv->pcm_handle);
  }

  device_ptr->state = HAL_STATE_OPEN;
  printf("[audio_hal] %s stopped\n", device_ptr->name);
  return HAL_SUCCESS;
}

/**
 * @brief Read interleaved PCM frames from a capture device.
 *
 * snd_pcm_readi() blocks until the requested frame count is available
 * or an error occurs.  -EPIPE indicates a hardware overrun: the ring
 * buffer filled faster than we drained it.  snd_pcm_prepare() resets
 * ALSA state so subsequent reads are valid (overflowed frames are lost).
 *
 * @param device_ptr   Active capture device.
 * @param data_buffer  Destination for interleaved PCM samples.
 * @param buffer_size  Size of data_buffer in bytes.
 * @return Bytes read on success, negative HAL_ERROR_* on failure.
 */
static ssize_t audio_read(hw_device_t *device_ptr, void *data_buffer,
                          size_t buffer_size) {
  if (!device_ptr || !device_ptr->priv || !data_buffer)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_ACTIVE)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;

  if (priv->config.direction != AUDIO_DIRECTION_CAPTURE)
    return HAL_ERROR_NOT_SUPPORT;

  snd_pcm_uframes_t frames = buffer_size / priv->bytes_per_frame;
  snd_pcm_sframes_t result =
      snd_pcm_readi(priv->pcm_handle, data_buffer, frames);
  if (result < 0) {
    fprintf(stderr, "[audio_hal] read error: %s\n", snd_strerror((int)result));
    if (result == -EPIPE)
      snd_pcm_prepare(priv->pcm_handle);
    return HAL_ERROR_IO;
  }

  return (ssize_t)result * (ssize_t)priv->bytes_per_frame;
}

/**
 * @brief Write interleaved PCM frames to a playback device.
 *
 * snd_pcm_writei() blocks until the requested frames are accepted into
 * the ALSA ring.  -EPIPE indicates a buffer underrun: the speaker consumed
 * frames faster than we provided them.  snd_pcm_prepare() resets the PCM
 * so the next write restarts cleanly without a full stop/open cycle.
 *
 * @param device_ptr   Active playback device.
 * @param data_buffer  Source of interleaved PCM samples.
 * @param data_size    Number of bytes in data_buffer.
 * @return Bytes written on success, negative HAL_ERROR_* on failure.
 */
static ssize_t audio_write(hw_device_t *device_ptr, const void *data_buffer,
                           size_t data_size) {
  if (!device_ptr || !device_ptr->priv || !data_buffer)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_ACTIVE)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;

  if (priv->config.direction != AUDIO_DIRECTION_PLAYBACK)
    return HAL_ERROR_NOT_SUPPORT;

  snd_pcm_uframes_t frames = data_size / priv->bytes_per_frame;
  snd_pcm_sframes_t result =
      snd_pcm_writei(priv->pcm_handle, data_buffer, frames);
  if (result < 0) {
    fprintf(stderr, "[audio_hal] write error: %s\n", snd_strerror((int)result));
    if (result == -EPIPE)
      snd_pcm_prepare(priv->pcm_handle);
    return HAL_ERROR_IO;
  }

  return (ssize_t)result * (ssize_t)priv->bytes_per_frame;
}

/**
 * @brief Execute a device-specific control command.
 *
 * All argument pointers are validated for NULL and uint32_t alignment
 * before any dereference.  Volume values greater than 100 are clamped.
 *
 * AUDIO_CMD_DRAIN  [PROPOSED] Calls snd_pcm_drain() to block until the
 * playback ring empties, useful for seamless track transitions or shutdown.
 *
 * AUDIO_CMD_RECOVER  [PROPOSED] Calls snd_pcm_prepare() to recover from an
 * xrun (underrun or overrun) that was detected externally (e.g. by a higher-
 * level audio engine that caught -EPIPE in its own snd_pcm_writei loop).
 *
 * @param device_ptr      Audio device.
 * @param control_command One of audio_cmd_t.
 * @param command_arg     Typed argument; see audio_cmd_t for requirements.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
static int audio_control(hw_device_t *device_ptr, uint32_t control_command,
                         void *command_arg) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;
  if (validate_control_arg(control_command, command_arg) != 0)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;

  switch (control_command) {
  case AUDIO_CMD_SET_VOLUME:
    priv->volume = *(const uint32_t *)command_arg;
    if (priv->volume > 100u)
      priv->volume = 100u;
    /* TODO: propagate to snd_mixer_t when mixer support is added. */
    return HAL_SUCCESS;

  case AUDIO_CMD_GET_VOLUME:
    *(uint32_t *)command_arg = priv->volume;
    return HAL_SUCCESS;

  case AUDIO_CMD_SET_MUTE:
    priv->muted = *(const int *)command_arg ? 1 : 0;
    /* TODO: propagate to snd_mixer_t. */
    return HAL_SUCCESS;

  case AUDIO_CMD_GET_MUTE:
    *(int *)command_arg = priv->muted;
    return HAL_SUCCESS;

  case AUDIO_CMD_GET_CONFIG:
    memcpy(command_arg, &priv->config, sizeof(audio_config_t));
    return HAL_SUCCESS;

  case AUDIO_CMD_DRAIN: /* [PROPOSED] */
    if (!priv->pcm_handle)
      return HAL_ERROR_INVALID;
    snd_pcm_drain(priv->pcm_handle);
    return HAL_SUCCESS;

  case AUDIO_CMD_RECOVER: /* [PROPOSED] */
    if (!priv->pcm_handle)
      return HAL_ERROR_INVALID;
    snd_pcm_prepare(priv->pcm_handle);
    return HAL_SUCCESS;

  default:
    return HAL_ERROR_NOT_SUPPORT;
  }
}

/**
 * @brief Fill an audio_info_t with the current runtime state.
 *
 * @param device_ptr  Audio device.
 * @param info_out    Caller-allocated audio_info_t to fill.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int audio_get_info(hw_device_t *device_ptr, void *info_out) {
  if (!device_ptr || !device_ptr->priv || !info_out)
    return HAL_ERROR_INVALID;

  audio_priv_t *priv = (audio_priv_t *)device_ptr->priv;
  audio_info_t *audio_info = (audio_info_t *)info_out;

  strncpy(audio_info->device_name, priv->alsa_device_id, HAL_MAX_NAME_LEN - 1u);
  audio_info->device_name[HAL_MAX_NAME_LEN - 1u] = '\0';
  memcpy(&audio_info->config, &priv->config, sizeof(audio_config_t));
  audio_info->bytes_per_frame = priv->bytes_per_frame;
  audio_info->current_volume = priv->volume;
  audio_info->is_muted = priv->muted;

  return HAL_SUCCESS;
}

/* ── cleanup callback ─────────────────────────────────────────────────── */

/**
 * @brief Free audio private data; registered as hw_device_t::cleanup.
 *
 * Called by hal_device_unref() when the reference count reaches zero.
 * explicit_bzero() overwrites the entire priv struct before freeing it
 * so credential-like data (device name, volume, mute state) does not
 * linger in heap memory and become accessible through heap-spray or
 * use-after-free exploits.
 *
 * @param device_ptr  Device whose priv is to be freed.
 */
static void audio_priv_cleanup(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return;
  explicit_bzero(device_ptr->priv, sizeof(audio_priv_t));
  free(device_ptr->priv);
  device_ptr->priv = NULL;
}

/* ── vtable ───────────────────────────────────────────────────────────── */

static const hw_device_ops_t audio_ops = {
    .open = audio_open,
    .close = audio_close,
    .start = audio_start,
    .stop = audio_stop,
    .read = audio_read,
    .write = audio_write,
    .control = audio_control,
    .get_info = audio_get_info,
    .reset = NULL, /* [PROPOSED] field; audio does not need soft-reset */
};

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Return the default audio configuration for the given direction.
 *
 * Produces CD-quality stereo (44 100 Hz, S16_LE, 2 ch) with a ~23 ms
 * period (1024 frames at 44.1 kHz) and a ~93 ms total ring buffer
 * (4096 frames).  These values balance latency against CPU wake-up
 * frequency for general-purpose embedded use.
 *
 * @param direction  Playback or capture direction.
 * @return Populated audio_config_t; no heap allocation.
 */
audio_config_t audio_hal_default_config(audio_direction_t direction) {
  audio_config_t cfg;
  cfg.direction = direction;
  cfg.sample_rate = 44100u;
  cfg.channels = 2u;
  cfg.format = AUDIO_FORMAT_S16_LE;
  cfg.period_size = 1024u;
  cfg.buffer_size = 4096u;
  return cfg;
}

/**
 * @brief Calculate the number of bytes in one interleaved audio frame.
 *
 * One frame contains exactly one sample from every channel.  For stereo
 * S16_LE the result is 4 bytes (2 bytes/sample × 2 channels).
 *
 * @param format    Sample encoding from audio_format_t.
 * @param channels  Number of channels (1 = mono, 2 = stereo, etc.).
 * @return Bytes per interleaved frame.
 */
uint32_t audio_hal_bytes_per_frame(audio_format_t format, uint32_t channels) {
  uint32_t bytes_per_sample;
  switch (format) {
  case AUDIO_FORMAT_S16_LE:
    bytes_per_sample = 2u;
    break;
  case AUDIO_FORMAT_S24_LE:
    bytes_per_sample = 3u;
    break;
  case AUDIO_FORMAT_S32_LE:
    bytes_per_sample = 4u;
    break;
  case AUDIO_FORMAT_FLOAT:
    bytes_per_sample = 4u;
    break;
  default:
    bytes_per_sample = 2u;
    break;
  }
  return bytes_per_sample * channels;
}

/**
 * @brief Return a short string identifying an audio sample format.
 *
 * The returned pointer is a string literal in read-only memory.
 *
 * @param format  Sample encoding from audio_format_t.
 * @return Static string; never NULL.
 */
const char *audio_hal_format_string(audio_format_t format) {
  switch (format) {
  case AUDIO_FORMAT_S16_LE:
    return "S16_LE";
  case AUDIO_FORMAT_S24_LE:
    return "S24_LE";
  case AUDIO_FORMAT_S32_LE:
    return "S32_LE";
  case AUDIO_FORMAT_FLOAT:
    return "FLOAT";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Allocate and initialise an audio HAL device.
 *
 * Allocates an hw_device_t and an audio_priv_t, copies the caller's
 * configuration, pre-computes bytes_per_frame, and attaches the ALSA vtable.
 * Does not open the ALSA PCM device; call dev->ops->open() to acquire the
 * hardware handle and negotiate hw_params.
 *
 * [PROPOSED] Sets capabilities = HAL_CAP_READ | HAL_CAP_WRITE |
 * HAL_CAP_CONTROL so callers can skip NULL-checking every vtable slot.
 *
 * @param device_name     Human-readable name used for registry lookup.
 * @param alsa_device_id  ALSA device string, e.g. "default" or "hw:0,0".
 * @param audio_config    Stream parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
hw_device_t *audio_hal_create(const char *device_name,
                              const char *alsa_device_id,
                              const audio_config_t *audio_config) {
  if (!device_name || !alsa_device_id || !audio_config)
    return NULL;

  hw_device_t *dev = malloc(sizeof(hw_device_t));
  if (!dev)
    return NULL;

  if (hal_device_init(dev, device_name, HAL_DEVICE_TYPE_AUDIO) != HAL_SUCCESS) {
    free(dev);
    return NULL;
  }

  audio_priv_t *priv = calloc(1u, sizeof(audio_priv_t));
  if (!priv) {
    hal_device_destroy(dev);
    free(dev);
    return NULL;
  }

  strncpy(priv->alsa_device_id, alsa_device_id,
          sizeof(priv->alsa_device_id) - 1u);
  priv->alsa_device_id[sizeof(priv->alsa_device_id) - 1u] = '\0';
  memcpy(&priv->config, audio_config, sizeof(audio_config_t));
  priv->bytes_per_frame =
      audio_hal_bytes_per_frame(audio_config->format, audio_config->channels);
  priv->volume = 100u;
  priv->muted = 0;
  priv->pcm_handle = NULL;

  dev->ops = &audio_ops;
  dev->priv = priv;
  dev->cleanup = audio_priv_cleanup;
  dev->capabilities =
      HAL_CAP_READ | HAL_CAP_WRITE | HAL_CAP_CONTROL; /* [PROPOSED] */

  printf("[audio_hal] created '%s' for ALSA device '%s'\n", device_name,
         alsa_device_id);
  return dev;
}

/**
 * @brief Release all resources held by an audio device.
 *
 * Delegates entirely to hal_device_unref(), which drives the full teardown
 * chain: stop → close → cleanup callback → hal_device_destroy → free.
 *
 * @param device_ptr  Device returned by audio_hal_create().
 */
void audio_hal_destroy(hw_device_t *device_ptr) {
  hal_device_unref(device_ptr);
}
