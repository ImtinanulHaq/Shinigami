/**
 * @file    camera_frame.h
 * @brief   CameraFrame data structure and camera SHM layout.
 *
 * ⚠️  The CameraFrame::data pointer is valid ONLY during the onFrameReady
 *     callback.  Camera frames can exceed 1.8MB (1280×720 YUYV) — NEVER
 *     attempt to store the pointer or use it after the callback returns.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace middleware {

/**
 * @brief A single video frame delivered to the application.
 */
struct CameraFrame {
    uint32_t    sequence;       ///< Monotonic frame counter
    uint64_t    timestamp_us;   ///< Microseconds since daemon start
    uint32_t    width;          ///< Pixels
    uint32_t    height;         ///< Pixels
    uint32_t    fps;            ///< Configured frames-per-second

    enum class Format : uint8_t {
        YUYV  = 0,
        MJPEG = 1,
        RGB24 = 2,
    } format;

    /**
     * @brief Pointer into SHM — valid ONLY during onFrameReady callback.
     * @warning  DO NOT store this pointer.  It becomes invalid on return.
     */
    const uint8_t*  data;

    /**
     * @brief Total bytes in this frame.
     *
     * YUYV: width * height * 2 bytes
     * RGB24: width * height * 3 bytes
     * MJPEG: compressed — actual size, not raw.
     */
    size_t      data_bytes;
};

// ── Camera SHM layout ─────────────────────────────────────────────────────

struct CameraShmHeader {
    uint32_t  magic;            ///< CAMERA_SHM_MAGIC
    uint32_t  version;          ///< CAMERA_SHM_VERSION
    uint32_t  slot_count;       ///< Ring slots
    uint32_t  slot_size_bytes;  ///< Bytes per slot (≥ max frame size)
    uint32_t  width;
    uint32_t  height;
    uint32_t  fps;
    uint8_t   format;           ///< CameraFrame::Format numeric value

    volatile uint32_t write_index;
    volatile uint32_t read_index;

    uint8_t  _pad[36]; // Pad to 80 bytes
};

static constexpr uint32_t CAMERA_SHM_MAGIC   = 0xCA10F1A0u;
static constexpr uint32_t CAMERA_SHM_VERSION = 1u;

/**
 * @brief Device information returned by CameraProxy::getDeviceInfo().
 */
struct CameraDeviceInfo {
    char     device_path[64];  ///< e.g. "/dev/video0"
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_fps;
    uint8_t  supported_formats; ///< Bitmask: bit0=YUYV, bit1=MJPEG, bit2=RGB24
};

} // namespace middleware
