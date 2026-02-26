/**
 * @file audio_hal.h
 * @brief Audio HAL — ALSA PCM playback and capture interface.
 *
 * Wraps the ALSA (Advanced Linux Sound Architecture) snd_pcm API behind
 * the standard @ref hw_device_t contract.  One @ref hw_device_t represents
 * either a playback stream (speaker) or a capture stream (microphone).
 */

#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include "hal_interface.h"

/* ── audio direction ──────────────────────────────────────────────────── */

/**
 * @brief Whether the PCM stream flows to or from the hardware.
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
 * of the x86 and ARM cores this HAL targets.
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
 * @p period_size controls interrupt latency: smaller values reduce
 * latency but increase CPU wakeup frequency.  @p buffer_size must be an
 * integer multiple of @p period_size and should be at least 2×.
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
 * Each command is associated with a required argument type:
 *   AUDIO_CMD_SET_VOLUME  — arg: const uint32_t *  (0–100)
 *   AUDIO_CMD_GET_VOLUME  — arg: uint32_t *
 *   AUDIO_CMD_SET_MUTE    — arg: const int *        (0=unmute, 1=mute)
 *   AUDIO_CMD_GET_MUTE    — arg: int *
 *   AUDIO_CMD_GET_CONFIG  — arg: audio_config_t *
 */
typedef enum {
  AUDIO_CMD_SET_VOLUME = 0x1000,
  AUDIO_CMD_GET_VOLUME = 0x1001,
  AUDIO_CMD_SET_MUTE = 0x1002,
  AUDIO_CMD_GET_MUTE = 0x1003,
  AUDIO_CMD_GET_CONFIG = 0x1005,
} audio_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise an audio HAL device.
 *
 * Does not open the ALSA PCM device; call dev->ops->open() to acquire
 * the hardware handle.
 *
 * @param device_name     Human-readable name used for registry lookup.
 * @param alsa_device_id  ALSA device string, e.g. "default", "hw:0,0".
 * @param audio_config    Stream parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
hw_device_t *audio_hal_create(const char *device_name,
                              const char *alsa_device_id,
                              const audio_config_t *audio_config);

/**
 * @brief Release all resources held by an audio device.
 *
 * Equivalent to calling @ref hal_device_unref.  The device struct is
 * freed when the reference count reaches zero.
 *
 * @param device_ptr  Device returned by @ref audio_hal_create.
 */
void audio_hal_destroy(hw_device_t *device_ptr);

/**
 * @brief Return the default audio configuration for the given direction.
 *
 * Produces CD-quality stereo (44 100 Hz, S16_LE, 2 ch) with ~23 ms
 * period and ~93 ms total buffer.
 *
 * @param direction  Playback or capture.
 * @return Populated audio_config_t; no heap allocation.
 */
audio_config_t audio_hal_default_config(audio_direction_t direction);

/**
 * @brief Calculate the number of bytes in one interleaved audio frame.
 *
 * One frame contains one sample from every channel.
 *
 * @param format    Sample encoding.
 * @param channels  Channel count (e.g. 2 for stereo).
 * @return Bytes per frame.
 */
uint32_t audio_hal_bytes_per_frame(audio_format_t format, uint32_t channels);

/**
 * @brief Return a short string identifying an audio sample format.
 * @param format  Sample encoding.
 * @return Static string; never NULL.
 */
const char *audio_hal_format_string(audio_format_t format);

#endif /* AUDIO_HAL_H */
