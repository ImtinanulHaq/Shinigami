#include "camera_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/videodev2.h>

// Buffer for V4L2 mmap
typedef struct {
    void* start;
    size_t length;
} camera_buffer_t;

// Private camera device data
typedef struct {
    char v4l2_path[64];             // V4L2 device path
    int v4l2_fd;                    // V4L2 file descriptor
    camera_config_t config;         // Configuration
    camera_buffer_t* buffers;       // mmap buffers
    uint32_t buffer_count;          // Number of buffers
    uint32_t frame_count;           // Total frames captured
    uint32_t dropped_frames;        // Dropped frames
    int streaming;                  // Streaming state
} camera_priv_t;

// ══════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

static uint32_t camera_format_to_v4l2(camera_format_t format)
{
    switch (format) {
        case CAMERA_FORMAT_YUYV:  return V4L2_PIX_FMT_YUYV;
        case CAMERA_FORMAT_MJPEG: return V4L2_PIX_FMT_MJPEG;
        case CAMERA_FORMAT_RGB24: return V4L2_PIX_FMT_RGB24;
        case CAMERA_FORMAT_NV12:  return V4L2_PIX_FMT_NV12;
        case CAMERA_FORMAT_H264:  return V4L2_PIX_FMT_H264;
        default:                  return V4L2_PIX_FMT_YUYV;
    }
}

const char* camera_hal_format_string(camera_format_t format)
{
    switch (format) {
        case CAMERA_FORMAT_YUYV:  return "YUYV";
        case CAMERA_FORMAT_MJPEG: return "MJPEG";
        case CAMERA_FORMAT_RGB24: return "RGB24";
        case CAMERA_FORMAT_NV12:  return "NV12";
        case CAMERA_FORMAT_H264:  return "H264";
        default:                  return "UNKNOWN";
    }
}

void camera_hal_get_resolution(camera_resolution_t res, uint32_t* width, uint32_t* height)
{
    switch (res) {
        case CAMERA_RES_QVGA:
            *width = 320; *height = 240; break;
        case CAMERA_RES_VGA:
            *width = 640; *height = 480; break;
        case CAMERA_RES_HD:
            *width = 1280; *height = 720; break;
        case CAMERA_RES_FHD:
            *width = 1920; *height = 1080; break;
        case CAMERA_RES_4K:
            *width = 3840; *height = 2160; break;
        default:
            *width = 640; *height = 480; break;
    }
}

camera_config_t camera_hal_default_config(void)
{
    camera_config_t config;
    config.width = 640;
    config.height = 480;
    config.format = CAMERA_FORMAT_YUYV;
    config.fps = 30;
    config.buffer_count = 4;  // 4 buffers for smooth streaming
    return config;
}

// ══════════════════════════════════════════════════════════════════════════════
// V4L2 CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

static int configure_v4l2(camera_priv_t* priv)
{
    struct v4l2_format fmt;
    struct v4l2_streamparm parm;
    
    // Set format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = priv->config.width;
    fmt.fmt.pix.height = priv->config.height;
    fmt.fmt.pix.pixelformat = camera_format_to_v4l2(priv->config.format);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    
    if (ioctl(priv->v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "[camera_hal] VIDIOC_S_FMT failed: %s\n", strerror(errno));
        return HAL_ERROR_IO;
    }
    
    // Verify format was set
    if (fmt.fmt.pix.width != priv->config.width ||
        fmt.fmt.pix.height != priv->config.height) {
        printf("[camera_hal] requested %ux%u, got %ux%u\n",
               priv->config.width, priv->config.height,
               fmt.fmt.pix.width, fmt.fmt.pix.height);
        priv->config.width = fmt.fmt.pix.width;
        priv->config.height = fmt.fmt.pix.height;
    }
    
    // Set frame rate
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = priv->config.fps;
    
    if (ioctl(priv->v4l2_fd, VIDIOC_S_PARM, &parm) < 0) {
        // Not critical, many cameras don't support this
        printf("[camera_hal] VIDIOC_S_PARM failed (non-critical)\n");
    }
    
    printf("[camera_hal] configured: %ux%u @ %u fps, format=%s\n",
           priv->config.width, priv->config.height, priv->config.fps,
           camera_hal_format_string(priv->config.format));
    
    return HAL_SUCCESS;
}

static int init_mmap_buffers(camera_priv_t* priv)
{
    struct v4l2_requestbuffers req;
    
    // Request buffers
    memset(&req, 0, sizeof(req));
    req.count = priv->config.buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(priv->v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "[camera_hal] VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        return HAL_ERROR_IO;
    }
    
    if (req.count < 2) {
        fprintf(stderr, "[camera_hal] insufficient buffer memory\n");
        return HAL_ERROR_NO_MEMORY;
    }
    
    priv->buffer_count = req.count;
    priv->buffers = calloc(req.count, sizeof(camera_buffer_t));
    if (!priv->buffers) {
        return HAL_ERROR_NO_MEMORY;
    }
    
    // Map buffers
    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(priv->v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "[camera_hal] VIDIOC_QUERYBUF failed\n");
            return HAL_ERROR_IO;
        }
        
        priv->buffers[i].length = buf.length;
        priv->buffers[i].start = mmap(NULL, buf.length,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED,
                                     priv->v4l2_fd, buf.m.offset);
        
        if (priv->buffers[i].start == MAP_FAILED) {
            fprintf(stderr, "[camera_hal] mmap failed\n");
            return HAL_ERROR_IO;
        }
    }
    
    printf("[camera_hal] initialized %u mmap buffers\n", req.count);
    
    return HAL_SUCCESS;
}

static void cleanup_mmap_buffers(camera_priv_t* priv)
{
    if (!priv->buffers) return;
    
    for (uint32_t i = 0; i < priv->buffer_count; i++) {
        if (priv->buffers[i].start && priv->buffers[i].start != MAP_FAILED) {
            munmap(priv->buffers[i].start, priv->buffers[i].length);
        }
    }
    
    free(priv->buffers);
    priv->buffers = NULL;
    priv->buffer_count = 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE OPERATIONS
// ══════════════════════════════════════════════════════════════════════════════

static int camera_open(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    // Open V4L2 device
    priv->v4l2_fd = open(priv->v4l2_path, O_RDWR | O_NONBLOCK);
    if (priv->v4l2_fd < 0) {
        fprintf(stderr, "[camera_hal] cannot open %s: %s\n",
                priv->v4l2_path, strerror(errno));
        return HAL_ERROR_NO_DEVICE;
    }
    
    // Verify it's a video capture device
    struct v4l2_capability cap;
    if (ioctl(priv->v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "[camera_hal] VIDIOC_QUERYCAP failed\n");
        close(priv->v4l2_fd);
        return HAL_ERROR_NO_DEVICE;
    }
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[camera_hal] device is not a video capture device\n");
        close(priv->v4l2_fd);
        return HAL_ERROR_NO_DEVICE;
    }
    
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "[camera_hal] device does not support streaming\n");
        close(priv->v4l2_fd);
        return HAL_ERROR_NOT_SUPPORT;
    }
    
    // Configure V4L2
    if (configure_v4l2(priv) != HAL_SUCCESS) {
        close(priv->v4l2_fd);
        return HAL_ERROR_IO;
    }
    
    // Initialize buffers
    if (init_mmap_buffers(priv) != HAL_SUCCESS) {
        close(priv->v4l2_fd);
        return HAL_ERROR_IO;
    }
    
    dev->fd = priv->v4l2_fd;
    dev->state = HAL_STATE_OPEN;
    printf("[camera_hal] device %s opened (%s)\n", dev->name,
           (char*)cap.card);
    
    return HAL_SUCCESS;
}

static int camera_close(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    // Stop streaming if active
    if (priv->streaming) {
        camera_stop(dev);
    }
    
    // Cleanup buffers
    cleanup_mmap_buffers(priv);
    
    // Close device
    if (priv->v4l2_fd >= 0) {
        close(priv->v4l2_fd);
        priv->v4l2_fd = -1;
    }
    
    dev->fd = -1;
    dev->state = HAL_STATE_CLOSED;
    printf("[camera_hal] device %s closed\n", dev->name);
    
    return HAL_SUCCESS;
}

static int camera_start(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_OPEN) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    // Queue all buffers
    for (uint32_t i = 0; i < priv->buffer_count; i++) {
        struct v4l2_buffer buf;
        
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(priv->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[camera_hal] VIDIOC_QBUF failed\n");
            return HAL_ERROR_IO;
        }
    }
    
    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(priv->v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[camera_hal] VIDIOC_STREAMON failed: %s\n", strerror(errno));
        return HAL_ERROR_IO;
    }
    
    priv->streaming = 1;
    dev->state = HAL_STATE_ACTIVE;
    printf("[camera_hal] device %s streaming started\n", dev->name);
    
    return HAL_SUCCESS;
}

static int camera_stop(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    if (priv->streaming) {
        // Stop streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(priv->v4l2_fd, VIDIOC_STREAMOFF, &type) < 0) {
            fprintf(stderr, "[camera_hal] VIDIOC_STREAMOFF failed\n");
            return HAL_ERROR_IO;
        }
        
        priv->streaming = 0;
    }
    
    dev->state = HAL_STATE_OPEN;
    printf("[camera_hal] device %s streaming stopped\n", dev->name);
    
    return HAL_SUCCESS;
}

static ssize_t camera_read(hw_device_t* dev, void* buf, size_t size)
{
    // Camera uses capture_frame instead of read
    (void)dev; (void)buf; (void)size;
    return HAL_ERROR_NOT_SUPPORT;
}

static ssize_t camera_write(hw_device_t* dev, const void* buf, size_t size)
{
    // Camera is capture-only
    (void)dev; (void)buf; (void)size;
    return HAL_ERROR_NOT_SUPPORT;
}

static int camera_control(hw_device_t* dev, uint32_t cmd, void* arg)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    switch (cmd) {
        case CAMERA_CMD_SET_RESOLUTION: {
            if (!arg) return HAL_ERROR_INVALID;
            camera_resolution_t res = *(camera_resolution_t*)arg;
            camera_hal_get_resolution(res, &priv->config.width, &priv->config.height);
            // Would need to reopen device to apply
            return HAL_SUCCESS;
        }
        
        case CAMERA_CMD_SET_FPS:
            if (!arg) return HAL_ERROR_INVALID;
            priv->config.fps = *(uint32_t*)arg;
            return HAL_SUCCESS;
            
        case CAMERA_CMD_GET_CONFIG:
            if (!arg) return HAL_ERROR_INVALID;
            memcpy(arg, &priv->config, sizeof(camera_config_t));
            return HAL_SUCCESS;
            
        case CAMERA_CMD_GET_FRAME:
            if (!arg) return HAL_ERROR_INVALID;
            return camera_hal_capture_frame(dev, (camera_frame_t*)arg, 0);
            
        case CAMERA_CMD_RETURN_FRAME:
            if (!arg) return HAL_ERROR_INVALID;
            return camera_hal_return_frame(dev, (camera_frame_t*)arg);
            
        default:
            return HAL_ERROR_NOT_SUPPORT;
    }
}

static int camera_get_info(hw_device_t* dev, void* info)
{
    if (!dev || !dev->priv || !info) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    camera_info_t* cam_info = (camera_info_t*)info;
    
    strncpy(cam_info->v4l2_device, priv->v4l2_path, sizeof(cam_info->v4l2_device) - 1);
    memcpy(&cam_info->config, &priv->config, sizeof(camera_config_t));
    cam_info->frame_count = priv->frame_count;
    cam_info->dropped_frames = priv->dropped_frames;
    cam_info->streaming = priv->streaming;
    
    return HAL_SUCCESS;
}

// Device operations
static const hw_device_ops_t camera_ops = {
    .open     = camera_open,
    .close    = camera_close,
    .start    = camera_start,
    .stop     = camera_stop,
    .read     = camera_read,
    .write    = camera_write,
    .control  = camera_control,
    .get_info = camera_get_info,
};

// ══════════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

hw_device_t* camera_hal_create(const char* name,
                               const char* v4l2_device,
                               const camera_config_t* config)
{
    if (!name || !v4l2_device || !config) return NULL;
    
    // Allocate device
    hw_device_t* dev = malloc(sizeof(hw_device_t));
    if (!dev) return NULL;
    
    // Initialize device
    if (hal_device_init(dev, name, HAL_DEVICE_TYPE_CAMERA) != HAL_SUCCESS) {
        free(dev);
        return NULL;
    }
    
    // Allocate private data
    camera_priv_t* priv = calloc(1, sizeof(camera_priv_t));
    if (!priv) {
        hal_device_destroy(dev);
        free(dev);
        return NULL;
    }
    
    // Initialize private data
    strncpy(priv->v4l2_path, v4l2_device, sizeof(priv->v4l2_path) - 1);
    memcpy(&priv->config, config, sizeof(camera_config_t));
    priv->v4l2_fd = -1;
    priv->buffers = NULL;
    priv->buffer_count = 0;
    priv->frame_count = 0;
    priv->dropped_frames = 0;
    priv->streaming = 0;
    
    // Set device operations
    dev->ops = &camera_ops;
    dev->priv = priv;
    
    printf("[camera_hal] created device %s for %s\n", name, v4l2_device);
    
    return dev;
}

void camera_hal_destroy(hw_device_t* dev)
{
    if (!dev) return;
    
    if (dev->state != HAL_STATE_CLOSED && dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }
    
    if (dev->priv) {
        free(dev->priv);
        dev->priv = NULL;
    }
    
    hal_device_destroy(dev);
    free(dev);
}

int camera_hal_capture_frame(hw_device_t* dev, camera_frame_t* frame, uint32_t timeout_ms)
{
    if (!dev || !dev->priv || !frame) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_ACTIVE) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    // Dequeue buffer
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(priv->v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) {
            return HAL_ERROR_TIMEOUT;
        }
        fprintf(stderr, "[camera_hal] VIDIOC_DQBUF failed: %s\n", strerror(errno));
        return HAL_ERROR_IO;
    }
    
    // Fill frame structure
    frame->data = priv->buffers[buf.index].start;
    frame->size = buf.bytesused;
    frame->timestamp = buf.timestamp.tv_sec * 1000000ULL + buf.timestamp.tv_usec;
    frame->sequence = buf.sequence;
    
    priv->frame_count++;
    
    return HAL_SUCCESS;
}

int camera_hal_return_frame(hw_device_t* dev, camera_frame_t* frame)
{
    if (!dev || !dev->priv || !frame) return HAL_ERROR_INVALID;
    
    camera_priv_t* priv = (camera_priv_t*)dev->priv;
    
    // Find buffer index
    uint32_t index = 0;
    for (index = 0; index < priv->buffer_count; index++) {
        if (priv->buffers[index].start == frame->data) {
            break;
        }
    }
    
    if (index >= priv->buffer_count) {
        return HAL_ERROR_INVALID;
    }
    
    // Queue buffer back to driver
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    
    if (ioctl(priv->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "[camera_hal] VIDIOC_QBUF failed\n");
        return HAL_ERROR_IO;
    }
    
    return HAL_SUCCESS;
}