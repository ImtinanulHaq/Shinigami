/**
 * @file audio_hal.h
 * @brief Audio HAL — ALSA PCM playback and capture interface.
 *
 * Wraps the ALSA (Advanced Linux Sound Architecture) snd_pcm API behind
 * the standard hw_device_t contract.  One hw_device_t represents either
 * a playback stream (speaker) or a capture stream (microphone).
 *
 * PROPOSED CHANGES:
 *   - Added AUDIO_CMD_DRAIN for graceful playback shutdown (wait for the
 *     ring buffer to empty before returning).
 *   - Added AUDIO_CMD_RECOVER for explicit xrun (underrun/overrun) recovery
 *     without needing a full stop/start cycle.
 */

#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include "hal_interface.h"

/* ── audio direction ──────────────────────────────────────────────────── */

/**
 * @brief Whether the PCM stream flows to or from the hardware.
 *
 * @p AUDIO_DIRECTION_PLAYBACK  Samples flow user→speaker.
 * @p AUDIO_DIRECTION_CAPTURE   Samples flow microphone→user.
 */
typedef enum {
  AUDIO_DIRECTION_PLAYBACK = 0,
  AUDIO_DIRECTION_CAPTURE = 1,
} audio_direction_t;

/* ── sample format ────────────────────────────────────────────────────── */

/**
 * @brief PCM sample encoding.
 *
 * All formats use little-endian byte order to match the native byte order
 * of the x86 and ARM cores targeted by this HAL.
 *
 * @p AUDIO_FORMAT_S16_LE  16-bit signed PCM, little-endian.
 * @p AUDIO_FORMAT_S24_LE  24-bit signed PCM, little-endian (packed in 3 B).
 * @p AUDIO_FORMAT_S32_LE  32-bit signed PCM, little-endian.
 * @p AUDIO_FORMAT_FLOAT   32-bit IEEE 754 float, little-endian.
 */
typedef enum {
  AUDIO_FORMAT_S16_LE = 0,
  AUDIO_FORMAT_S24_LE = 1,
  AUDIO_FORMAT_S32_LE = 2,
  AUDIO_FORMAT_FLOAT = 3,
} audio_format_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Full configuration for an audio PCM stream.
 *
 * period_size controls interrupt latency: smaller values reduce latency
 * but increase CPU wakeup frequency.  buffer_size must be an integer
 * multiple of period_size and should be at least 2×.  If the ALSA driver
 * cannot meet the exact sample_rate, the nearest supported value is applied
 * and stored back into the live config.
 *
 * @p direction    Whether this device is for playback or capture.
 * @p sample_rate  Requested sample rate in Hz (e.g. 44100, 48000).
 * @p channels     Number of interleaved channels (1 = mono, 2 = stereo).
 * @p format       PCM sample encoding from audio_format_t.
 * @p period_size  Hardware interrupt granularity in frames.
 * @p buffer_size  Total ring-buffer depth in frames; must be N*period_size.
 */
typedef struct {
  audio_direction_t direction;
  uint32_t sample_rate;
  uint32_t channels;
  audio_format_t format;
  uint32_t period_size;
  uint32_t buffer_size;
} audio_config_t;

/* ── device info ──────────────────────────────────────────────────────── */

/**
 * @brief Runtime snapshot of audio device state.
 *
 * @p device_name     ALSA device string used at open time (e.g. "hw:0,0").
 * @p config          Copy of the active stream configuration.
 * @p bytes_per_frame Pre-computed bytes per interleaved audio frame.
 * @p current_volume  Last volume set via AUDIO_CMD_SET_VOLUME (0–100).
 * @p is_muted        Non-zero if the stream is currently muted.
 */
typedef struct {
  char device_name[HAL_MAX_NAME_LEN];
  audio_config_t config;
  uint32_t bytes_per_frame;
  uint32_t current_volume;
  int is_muted;
} audio_info_t;

/* ── control commands ─────────────────────────────────────────────────── */

/**
 * @brief Commands accepted by the audio device control() operation.
 *
 * @p AUDIO_CMD_SET_VOLUME  arg: const uint32_t * (0–100); clamps if >100.
 * @p AUDIO_CMD_GET_VOLUME  arg: uint32_t *.
 * @p AUDIO_CMD_SET_MUTE    arg: const int * (0 = unmute, 1 = mute).
 * @p AUDIO_CMD_GET_MUTE    arg: int *.
 * @p AUDIO_CMD_GET_CONFIG  arg: audio_config_t *.
 * @p AUDIO_CMD_DRAIN       [PROPOSED] arg: NULL; blocks until the playback
 *                          ring empties (equivalent to snd_pcm_drain).
 * @p AUDIO_CMD_RECOVER     [PROPOSED] arg: NULL; calls snd_pcm_prepare()
 *                          to recover from an xrun without a full restart.
 */
typedef enum {
  AUDIO_CMD_SET_VOLUME = 0x1000,
  AUDIO_CMD_GET_VOLUME = 0x1001,
  AUDIO_CMD_SET_MUTE = 0x1002,
  AUDIO_CMD_GET_MUTE = 0x1003,
  AUDIO_CMD_GET_CONFIG = 0x1005,
  AUDIO_CMD_DRAIN = 0x1006,   /* [PROPOSED] */
  AUDIO_CMD_RECOVER = 0x1007, /* [PROPOSED] */
} audio_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/** @brief Allocate and initialise an audio HAL device. */
hw_device_t *audio_hal_create(const char *device_name,
                              const char *alsa_device_id,
                              const audio_config_t *audio_config);

/** @brief Release all resources held by an audio device. */
void audio_hal_destroy(hw_device_t *device_ptr);

/** @brief Return the default audio configuration for the given direction. */
audio_config_t audio_hal_default_config(audio_direction_t direction);

/** @brief Calculate the number of bytes in one interleaved audio frame. */
uint32_t audio_hal_bytes_per_frame(audio_format_t format, uint32_t channels);

/** @brief Return a short string identifying an audio sample format. */
const char *audio_hal_format_string(audio_format_t format);

#endif /* AUDIO_HAL_H */
