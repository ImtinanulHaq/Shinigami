#include "audio_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>

// Private audio device data
typedef struct {
    char alsa_device[64];           // ALSA device name
    snd_pcm_t* pcm_handle;          // ALSA PCM handle
    audio_config_t config;          // Current configuration
    uint32_t bytes_per_frame;       // Calculated bytes per frame
    uint32_t volume;                // Volume (0-100)
    int muted;                      // Mute state
} audio_priv_t;

// ══════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

static snd_pcm_format_t audio_format_to_alsa(audio_format_t format)
{
    switch (format) {
        case AUDIO_FORMAT_S16_LE: return SND_PCM_FORMAT_S16_LE;
        case AUDIO_FORMAT_S24_LE: return SND_PCM_FORMAT_S24_LE;
        case AUDIO_FORMAT_S32_LE: return SND_PCM_FORMAT_S32_LE;
        case AUDIO_FORMAT_FLOAT:  return SND_PCM_FORMAT_FLOAT_LE;
        default:                  return SND_PCM_FORMAT_S16_LE;
    }
}

uint32_t audio_hal_bytes_per_frame(audio_format_t format, uint32_t channels)
{
    uint32_t bytes_per_sample;
    
    switch (format) {
        case AUDIO_FORMAT_S16_LE: bytes_per_sample = 2; break;
        case AUDIO_FORMAT_S24_LE: bytes_per_sample = 3; break;
        case AUDIO_FORMAT_S32_LE: bytes_per_sample = 4; break;
        case AUDIO_FORMAT_FLOAT:  bytes_per_sample = 4; break;
        default:                  bytes_per_sample = 2; break;
    }
    
    return bytes_per_sample * channels;
}

const char* audio_hal_format_string(audio_format_t format)
{
    switch (format) {
        case AUDIO_FORMAT_S16_LE: return "S16_LE";
        case AUDIO_FORMAT_S24_LE: return "S24_LE";
        case AUDIO_FORMAT_S32_LE: return "S32_LE";
        case AUDIO_FORMAT_FLOAT:  return "FLOAT";
        default:                  return "UNKNOWN";
    }
}

audio_config_t audio_hal_default_config(audio_direction_t direction)
{
    audio_config_t config;
    config.direction = direction;
    config.sample_rate = 44100;     // CD quality
    config.channels = 2;            // Stereo
    config.format = AUDIO_FORMAT_S16_LE;
    config.period_size = 1024;      // ~23ms at 44.1kHz
    config.buffer_size = 4096;      // ~93ms at 44.1kHz
    return config;
}

// ══════════════════════════════════════════════════════════════════════════════
// ALSA CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

static int configure_alsa(audio_priv_t* priv)
{
    int err;
    snd_pcm_hw_params_t* hw_params = NULL;
    snd_pcm_sw_params_t* sw_params = NULL;
    (void)sw_params;
    
    // Allocate hardware parameters
    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "[audio_hal] hw_params malloc failed: %s\n", snd_strerror(err));
        return HAL_ERROR_NO_MEMORY;
    }
    
    // Initialize hardware parameters
    if ((err = snd_pcm_hw_params_any(priv->pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "[audio_hal] hw_params_any failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    // Set access type (interleaved)
    if ((err = snd_pcm_hw_params_set_access(priv->pcm_handle, hw_params,
                                            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "[audio_hal] set_access failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    // Set sample format
    snd_pcm_format_t alsa_format = audio_format_to_alsa(priv->config.format);
    if ((err = snd_pcm_hw_params_set_format(priv->pcm_handle, hw_params, alsa_format)) < 0) {
        fprintf(stderr, "[audio_hal] set_format failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    // Set channel count
    if ((err = snd_pcm_hw_params_set_channels(priv->pcm_handle, hw_params,
                                              priv->config.channels)) < 0) {
        fprintf(stderr, "[audio_hal] set_channels failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    // Set sample rate
    unsigned int rate = priv->config.sample_rate;
    if ((err = snd_pcm_hw_params_set_rate_near(priv->pcm_handle, hw_params, &rate, 0)) < 0) {
        fprintf(stderr, "[audio_hal] set_rate failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    if (rate != priv->config.sample_rate) {
        printf("[audio_hal] requested rate %u, got %u\n", priv->config.sample_rate, rate);
        priv->config.sample_rate = rate;
    }
    
    // Set period size
    snd_pcm_uframes_t period_size = priv->config.period_size;
    if ((err = snd_pcm_hw_params_set_period_size_near(priv->pcm_handle, hw_params,
                                                       &period_size, 0)) < 0) {
        fprintf(stderr, "[audio_hal] set_period_size failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    // Set buffer size
    snd_pcm_uframes_t buffer_size = priv->config.buffer_size;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(priv->pcm_handle, hw_params,
                                                       &buffer_size)) < 0) {
        fprintf(stderr, "[audio_hal] set_buffer_size failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    // Apply hardware parameters
    if ((err = snd_pcm_hw_params(priv->pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "[audio_hal] hw_params failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        return HAL_ERROR_IO;
    }
    
    snd_pcm_hw_params_free(hw_params);
    
    // Prepare device
    if ((err = snd_pcm_prepare(priv->pcm_handle)) < 0) {
        fprintf(stderr, "[audio_hal] prepare failed: %s\n", snd_strerror(err));
        return HAL_ERROR_IO;
    }
    
    printf("[audio_hal] configured: %s, %uHz, %uch, %s\n",
           priv->alsa_device,
           priv->config.sample_rate,
           priv->config.channels,
           audio_hal_format_string(priv->config.format));
    
    return HAL_SUCCESS;
}

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE OPERATIONS
// ══════════════════════════════════════════════════════════════════════════════

static int audio_open(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    int err;
    
    // Determine stream type
    snd_pcm_stream_t stream = (priv->config.direction == AUDIO_DIRECTION_PLAYBACK)
                             ? SND_PCM_STREAM_PLAYBACK
                             : SND_PCM_STREAM_CAPTURE;
    
    // Open PCM device
    if ((err = snd_pcm_open(&priv->pcm_handle, priv->alsa_device, stream, 0)) < 0) {
        fprintf(stderr, "[audio_hal] cannot open device %s: %s\n",
                priv->alsa_device, snd_strerror(err));
        return HAL_ERROR_NO_DEVICE;
    }
    
    // Configure ALSA
    if ((err = configure_alsa(priv)) < 0) {
        snd_pcm_close(priv->pcm_handle);
        priv->pcm_handle = NULL;
        return err;
    }
    
    dev->state = HAL_STATE_OPEN;
    printf("[audio_hal] device %s opened\n", dev->name);
    
    return HAL_SUCCESS;
}

static int audio_close(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    
    if (priv->pcm_handle) {
        snd_pcm_drain(priv->pcm_handle);
        snd_pcm_close(priv->pcm_handle);
        priv->pcm_handle = NULL;
    }
    
    dev->state = HAL_STATE_CLOSED;
    printf("[audio_hal] device %s closed\n", dev->name);
    
    return HAL_SUCCESS;
}

static int audio_start(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_OPEN) return HAL_ERROR_INVALID;
    
    dev->state = HAL_STATE_ACTIVE;
    printf("[audio_hal] device %s started\n", dev->name);
    
    return HAL_SUCCESS;
}

static int audio_stop(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    
    if (priv->pcm_handle) {
        snd_pcm_drop(priv->pcm_handle);
        snd_pcm_prepare(priv->pcm_handle);
    }
    
    dev->state = HAL_STATE_OPEN;
    printf("[audio_hal] device %s stopped\n", dev->name);
    
    return HAL_SUCCESS;
}

static ssize_t audio_read(hw_device_t* dev, void* buf, size_t size)
{
    if (!dev || !dev->priv || !buf) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_ACTIVE) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    
    if (priv->config.direction != AUDIO_DIRECTION_CAPTURE) {
        return HAL_ERROR_NOT_SUPPORT;  // Cannot read from playback device
    }
    
    // Calculate frames
    snd_pcm_uframes_t frames = size / priv->bytes_per_frame;
    
    // Read audio data
    snd_pcm_sframes_t err = snd_pcm_readi(priv->pcm_handle, buf, frames);
    
    if (err < 0) {
        fprintf(stderr, "[audio_hal] read error: %s\n", snd_strerror(err));
        
        // Try to recover from errors
        if (err == -EPIPE) {
            // Buffer overrun
            snd_pcm_prepare(priv->pcm_handle);
        }
        
        return HAL_ERROR_IO;
    }
    
    return err * priv->bytes_per_frame;
}

static ssize_t audio_write(hw_device_t* dev, const void* buf, size_t size)
{
    if (!dev || !dev->priv || !buf) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_ACTIVE) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    
    if (priv->config.direction != AUDIO_DIRECTION_PLAYBACK) {
        return HAL_ERROR_NOT_SUPPORT;  // Cannot write to capture device
    }
    
    // Calculate frames
    snd_pcm_uframes_t frames = size / priv->bytes_per_frame;
    
    // Write audio data
    snd_pcm_sframes_t err = snd_pcm_writei(priv->pcm_handle, buf, frames);
    
    if (err < 0) {
        fprintf(stderr, "[audio_hal] write error: %s\n", snd_strerror(err));
        
        // Try to recover from errors
        if (err == -EPIPE) {
            // Buffer underrun
            snd_pcm_prepare(priv->pcm_handle);
        }
        
        return HAL_ERROR_IO;
    }
    
    return err * priv->bytes_per_frame;
}

static int audio_control(hw_device_t* dev, uint32_t cmd, void* arg)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    
    switch (cmd) {
        case AUDIO_CMD_SET_VOLUME:
            if (!arg) return HAL_ERROR_INVALID;
            priv->volume = *(uint32_t*)arg;
            if (priv->volume > 100) priv->volume = 100;
            // TODO: Actually set ALSA volume
            return HAL_SUCCESS;
            
        case AUDIO_CMD_GET_VOLUME:
            if (!arg) return HAL_ERROR_INVALID;
            *(uint32_t*)arg = priv->volume;
            return HAL_SUCCESS;
            
        case AUDIO_CMD_SET_MUTE:
            if (!arg) return HAL_ERROR_INVALID;
            priv->muted = *(int*)arg;
            // TODO: Actually set ALSA mute
            return HAL_SUCCESS;
            
        case AUDIO_CMD_GET_MUTE:
            if (!arg) return HAL_ERROR_INVALID;
            *(int*)arg = priv->muted;
            return HAL_SUCCESS;
            
        case AUDIO_CMD_GET_CONFIG:
            if (!arg) return HAL_ERROR_INVALID;
            memcpy(arg, &priv->config, sizeof(audio_config_t));
            return HAL_SUCCESS;
            
        default:
            return HAL_ERROR_NOT_SUPPORT;
    }
}

static int audio_get_info(hw_device_t* dev, void* info)
{
    if (!dev || !dev->priv || !info) return HAL_ERROR_INVALID;
    
    audio_priv_t* priv = (audio_priv_t*)dev->priv;
    audio_info_t* audio_info = (audio_info_t*)info;
    
    snprintf(audio_info->device_name, HAL_MAX_NAME_LEN, "%s", priv->alsa_device);
    memcpy(&audio_info->config, &priv->config, sizeof(audio_config_t));
    audio_info->bytes_per_frame = priv->bytes_per_frame;
    audio_info->current_volume = priv->volume;
    audio_info->is_muted = priv->muted;
    
    return HAL_SUCCESS;
}

// Device operations
static const hw_device_ops_t audio_ops = {
    .open     = audio_open,
    .close    = audio_close,
    .start    = audio_start,
    .stop     = audio_stop,
    .read     = audio_read,
    .write    = audio_write,
    .control  = audio_control,
    .get_info = audio_get_info,
};

// ══════════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

hw_device_t* audio_hal_create(const char* name,
                              const char* alsa_device,
                              const audio_config_t* config)
{
    if (!name || !alsa_device || !config) return NULL;
    
    // Allocate device
    hw_device_t* dev = malloc(sizeof(hw_device_t));
    if (!dev) return NULL;
    
    // Initialize device
    if (hal_device_init(dev, name, HAL_DEVICE_TYPE_AUDIO) != HAL_SUCCESS) {
        free(dev);
        return NULL;
    }
    
    // Allocate private data
    audio_priv_t* priv = calloc(1, sizeof(audio_priv_t));
    if (!priv) {
        hal_device_destroy(dev);
        free(dev);
        return NULL;
    }
    
    // Initialize private data
    strncpy(priv->alsa_device, alsa_device, sizeof(priv->alsa_device) - 1);
    memcpy(&priv->config, config, sizeof(audio_config_t));
    priv->bytes_per_frame = audio_hal_bytes_per_frame(config->format, config->channels);
    priv->volume = 100;  // Max volume
    priv->muted = 0;
    priv->pcm_handle = NULL;
    
    // Set device operations
    dev->ops = &audio_ops;
    dev->priv = priv;
    
    printf("[audio_hal] created device %s for %s\n", name, alsa_device);
    
    return dev;
}

void audio_hal_destroy(hw_device_t* dev)
{
    if (!dev) return;
    
    // Close if still open
    if (dev->state != HAL_STATE_CLOSED && dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }
    
    // Free private data
    if (dev->priv) {
        free(dev->priv);
        dev->priv = NULL;
    }
    
    hal_device_destroy(dev);
    free(dev);
}