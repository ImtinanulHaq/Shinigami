#ifndef CAMERA_HAL_H
#define CAMERA_HAL_H

#include "hal_interface.h"

// ══════════════════════════════════════════════════════════════════════════════
// CAMERA HAL - V4L2 (Video for Linux 2) Implementation
// 
// Purpose: Provides camera capture using V4L2 API
// Features:
// - Video capture (streaming)
// - Multiple pixel formats (YUYV, MJPEG, RGB, etc.)
// - Resolution configuration
// - Frame rate control
// - Exposure and white balance control
// ══════════════════════════════════════════════════════════════════════════════

// Pixel formats
typedef enum {
    CAMERA_FORMAT_YUYV   = 0x01,  // YUV 4:2:2 (most common)
    CAMERA_FORMAT_MJPEG  = 0x02,  // Motion JPEG
    CAMERA_FORMAT_RGB24  = 0x03,  // 24-bit RGB
    CAMERA_FORMAT_NV12   = 0x04,  // YUV 4:2:0
    CAMERA_FORMAT_H264   = 0x05,  // H.264 compressed
} camera_format_t;

// Camera resolution presets
typedef enum {
    CAMERA_RES_QVGA  = 0,  // 320x240
    CAMERA_RES_VGA   = 1,  // 640x480
    CAMERA_RES_HD    = 2,  // 1280x720
    CAMERA_RES_FHD   = 3,  // 1920x1080
    CAMERA_RES_4K    = 4,  // 3840x2160
    CAMERA_RES_CUSTOM = 99, // Custom resolution
} camera_resolution_t;

// Camera configuration
typedef struct {
    uint32_t width;              // Frame width in pixels
    uint32_t height;             // Frame height in pixels
    camera_format_t format;      // Pixel format
    uint32_t fps;                // Frames per second
    uint32_t buffer_count;       // Number of buffers (2-4 typical)
} camera_config_t;

// Frame buffer
typedef struct {
    void* data;                  // Frame data pointer
    size_t size;                 // Frame size in bytes
    uint64_t timestamp;          // Capture timestamp (microseconds)
    uint32_t sequence;           // Frame sequence number
} camera_frame_t;

// Camera device info
typedef struct {
    char v4l2_device[64];        // V4L2 device path
    camera_config_t config;      // Current configuration
    uint32_t frame_count;        // Total frames captured
    uint32_t dropped_frames;     // Dropped frame count
    int streaming;               // Streaming state
} camera_info_t;

// Camera control commands
typedef enum {
    CAMERA_CMD_SET_RESOLUTION  = 0x3000,  // arg: camera_resolution_t*
    CAMERA_CMD_SET_FPS         = 0x3001,  // arg: uint32_t*
    CAMERA_CMD_SET_EXPOSURE    = 0x3002,  // arg: int* (-4 to +4, 0=auto)
    CAMERA_CMD_SET_BRIGHTNESS  = 0x3003,  // arg: int* (0-255)
    CAMERA_CMD_SET_CONTRAST    = 0x3004,  // arg: int* (0-255)
    CAMERA_CMD_SET_SATURATION  = 0x3005,  // arg: int* (0-255)
    CAMERA_CMD_SET_WHITE_BAL   = 0x3006,  // arg: int* (0=auto, 1-N=preset)
    CAMERA_CMD_GET_FRAME       = 0x3007,  // arg: camera_frame_t*
    CAMERA_CMD_RETURN_FRAME    = 0x3008,  // arg: camera_frame_t*
    CAMERA_CMD_GET_CONFIG      = 0x3009,  // arg: camera_config_t*
} camera_cmd_t;

// ══════════════════════════════════════════════════════════════════════════════
// CAMERA HAL FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Create camera device
// name: device name (user-friendly, like "camera0")
// v4l2_device: V4L2 device path (like "/dev/video0")
// config: camera configuration
// Returns: device pointer on success, NULL on failure
hw_device_t* camera_hal_create(const char* name,
                               const char* v4l2_device,
                               const camera_config_t* config);

// Destroy camera device
void camera_hal_destroy(hw_device_t* dev);

// Helper: Get default camera configuration
camera_config_t camera_hal_default_config(void);

// Helper: Get resolution dimensions
void camera_hal_get_resolution(camera_resolution_t res, uint32_t* width, uint32_t* height);

// Helper: Get format name as string
const char* camera_hal_format_string(camera_format_t format);

// Helper: Capture a frame (blocking)
// dev: camera device
// frame: output frame structure
// timeout_ms: timeout in milliseconds (0 = infinite)
// Returns: 0 on success, negative error code on failure
int camera_hal_capture_frame(hw_device_t* dev, camera_frame_t* frame, uint32_t timeout_ms);

// Helper: Return frame to driver (must be called after processing)
// dev: camera device
// frame: frame to return
// Returns: 0 on success, negative error code on failure
int camera_hal_return_frame(hw_device_t* dev, camera_frame_t* frame);

#endif // CAMERA_HAL_H