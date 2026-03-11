/**
 * @file    camera_shm_reader.h
 * @brief   Reads video frames from the camera shared memory ring buffer.
 */

#pragma once

#include "camera_frame.h"
#include "../base/proxy_result.h"
#include "../base/proxy_shared_memory.h"
#include <functional>
#include <string>

namespace middleware {

class CameraShmReader {
public:
    CameraShmReader();
    ~CameraShmReader();

    Result<void> open(const std::string& shm_name);
    void         close() noexcept;

    [[nodiscard]] bool pollReady()  const noexcept;
    [[nodiscard]] bool isOpen()     const noexcept;
    [[nodiscard]] const CameraShmHeader* header() const noexcept;

    Result<void> readFrame(const std::function<void(const CameraFrame&)>& cb);

private:
    ProxyShmRegion   shm_;
    CameraShmHeader* hdr_{nullptr};
    uint32_t         cached_ri_{0};
};

} // namespace middleware
