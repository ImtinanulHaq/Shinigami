#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include "hal_interface.h"

// ══════════════════════════════════════════════════════════════════════════════
// AUDIO HAL - ALSA (Advanced Linux Sound Architecture) Implementation
// 
// Purpose: Provides audio playback and recording using ALSA
// Features:
// - PCM playback (speaker output)
// - PCM capture (microphone input)
// - Volume control
// - Sample rate/format configuration
// ══════════════════════════════════════════════════════════════════════════════

// Audio direction
typedef enum {
    AUDIO_DIRECTION_PLAYBACK = 0,  // Output (speakers)
    AUDIO_DIRECTION_CAPTURE  = 1,  // Input (microphone)
} audio_direction_t;

// Audio sample format
typedef enum {
    AUDIO_FORMAT_S16_LE = 0,  // Signed 16-bit little-endian (most common)
    AUDIO_FORMAT_S24_LE = 1,  // Signed 24-bit little-endian
    AUDIO_FORMAT_S32_LE = 2,  // Signed 32-bit little-endian
    AUDIO_FORMAT_FLOAT  = 3,  // 32-bit float
} audio_format_t;

// Audio configuration
typedef struct {
    audio_direction_t direction;   // Playback or capture
    uint32_t         sample_rate;  // Sample rate (Hz): 44100, 48000, etc.
    uint32_t         channels;     // Number of channels: 1 (mono), 2 (stereo)
    audio_format_t   format;       // Sample format
    uint32_t         period_size;  // Period size in frames (latency control)
    uint32_t         buffer_size;  // Buffer size in frames
} audio_config_t;

// Audio device info
typedef struct {
    char device_name[HAL_MAX_NAME_LEN];  // ALSA device name (e.g., "hw:0,0")
    audio_config_t config;               // Current configuration
    uint32_t bytes_per_frame;            // Bytes per frame (calculated)
    uint32_t current_volume;             // Current volume (0-100)
    int is_muted;                        // Mute state
} audio_info_t;

// Audio control commands (for control() function)
typedef enum {
    AUDIO_CMD_SET_VOLUME    = 0x1000,  // arg: uint32_t* (0-100)
    AUDIO_CMD_GET_VOLUME    = 0x1001,  // arg: uint32_t*
    AUDIO_CMD_SET_MUTE      = 0x1002,  // arg: int* (0=unmute, 1=mute)
    AUDIO_CMD_GET_MUTE      = 0x1003,  // arg: int*
    AUDIO_CMD_SET_CONFIG    = 0x1004,  // arg: audio_config_t*
    AUDIO_CMD_GET_CONFIG    = 0x1005,  // arg: audio_config_t*
} audio_cmd_t;

// ══════════════════════════════════════════════════════════════════════════════
// AUDIO HAL FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Create audio device
// name: device name (user-friendly, like "audio0")
// alsa_device: ALSA device identifier (like "default", "hw:0,0", "plughw:0,0")
// config: audio configuration
// Returns: device pointer on success, NULL on failure
hw_device_t* audio_hal_create(const char* name, 
                              const char* alsa_device,
                              const audio_config_t* config);

// Destroy audio device (release resources)
void audio_hal_destroy(hw_device_t* dev);

// Helper: Get default audio configuration
// direction: playback or capture
audio_config_t audio_hal_default_config(audio_direction_t direction);

// Helper: Calculate bytes per frame based on format and channels
uint32_t audio_hal_bytes_per_frame(audio_format_t format, uint32_t channels);

// Helper: Get format name as string
const char* audio_hal_format_string(audio_format_t format);

#endif // AUDIO_HAL_H