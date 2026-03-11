/**
 * @file    audio_proxy.h
 * @brief   AudioProxy — high-level C++ API for the audio service daemon.
 *
 * AudioProxy provides capture, playback, volume control, and device info.
 * PCM frame data is delivered via shared memory for zero-copy throughput;
 * control messages (start/stop/configure) travel over the Unix socket.
 *
 * Example (capture):
 * @code
 *   middleware::AudioProxy audio;
 *   audio.connect();
 *   audio.onFrameReady([](const middleware::AudioFrame& f) {
 *       // f.data valid here only — copy if you need to keep it
 *       process(f.data, f.frame_count * f.channels);
 *   });
 *   audio.startCapture();
 *   // ... wait ...
 *   audio.stopCapture();
 * @endcode
 */

#pragma once

#include "../base/service_proxy.h"
#include "audio_frame.h"
#include "audio_shm_reader.h"

#include <functional>
#include <atomic>
#include <memory>
#include <cstdint>

namespace middleware {

// ── Message type extensions for audio service ─────────────────────────────
// These extend the base SM protocol with audio-specific commands.
// Range: 0x0100 – 0x01FF reserved for audio service.
static constexpr uint16_t AUDIO_MSG_START_CAPTURE = 0x0101;
static constexpr uint16_t AUDIO_MSG_STOP_CAPTURE  = 0x0102;
static constexpr uint16_t AUDIO_MSG_START_PLAYBACK= 0x0103;
static constexpr uint16_t AUDIO_MSG_STOP_PLAYBACK = 0x0104;
static constexpr uint16_t AUDIO_MSG_SET_SAMPLE_RATE=0x0105;
static constexpr uint16_t AUDIO_MSG_SET_CHANNELS  = 0x0106;
static constexpr uint16_t AUDIO_MSG_SET_BIT_DEPTH = 0x0107;
static constexpr uint16_t AUDIO_MSG_GET_DEVICE_INFO=0x0108;
static constexpr uint16_t AUDIO_MSG_SET_VOLUME    = 0x0109;
static constexpr uint16_t AUDIO_MSG_GET_VOLUME    = 0x010A;
static constexpr uint16_t AUDIO_MSG_WRITE_FRAME   = 0x010B;
static constexpr uint16_t AUDIO_MSG_FRAME_READY   = 0x010C; // push

// ── Payload structures ─────────────────────────────────────────────────────
struct AudioCmdSampleRate { uint32_t hz; };
struct AudioCmdChannels   { uint16_t count; };
struct AudioCmdBitDepth   { uint16_t bits; };
struct AudioCmdVolume     { float level; };   // 0.0 – 1.0
struct AudioCmdFrameReady { uint32_t slot_index; };
struct AudioRspVolume     { float level; int32_t status; };
struct AudioRspDeviceInfo { AudioDeviceInfo info; int32_t status; };
struct AudioRspStatus     { int32_t status; };

// ── AudioProxy ─────────────────────────────────────────────────────────────

/**
 * @brief High-level proxy for the audio service daemon.
 *
 * Inherits connection management, threading, and reconnect logic from
 * ServiceProxy.  Adds audio-specific capture / playback API.
 */
class PROXY_API AudioProxy : public ServiceProxy {
public:
    /**
     * @brief Construct an AudioProxy.
     * @param config  Optional configuration (socket paths, timeouts, etc.).
     */
    explicit AudioProxy(ProxyConfig config = {});
    ~AudioProxy() override;

    // ── Capture ────────────────────────────────────────────────────────

    /**
     * @brief Start PCM capture.
     *
     * The daemon begins filling the shared memory ring buffer.  Frames are
     * delivered via the onFrameReady callback.
     *
     * @return ok() on success, error if not connected or daemon rejected.
     *
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> startCapture();

    /**
     * @brief Stop PCM capture.
     *
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> stopCapture();

    /** @return true if capture is currently running. */
    [[nodiscard]] bool isCapturing() const noexcept;

    // ── Playback ───────────────────────────────────────────────────────

    /**
     * @brief Start playback mode (daemon opens the DAC).
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> startPlayback();

    /**
     * @brief Stop playback mode.
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> stopPlayback();

    /**
     * @brief Write one frame of PCM data for playback.
     *
     * Sends the PCM data over the socket (suitable for low-rate playback).
     * For high-rate playback, use a shared memory path (future extension).
     *
     * @param data        Interleaved PCM samples.
     * @param frame_count Frames (samples per channel).
     *
     * @thread_safety  Thread-safe (uses serialised send).
     */
    Result<void> writePlaybackFrame(const int16_t* data, size_t frame_count);

    /** @return true if playback is currently running. */
    [[nodiscard]] bool isPlaying() const noexcept;

    // ── Configuration ──────────────────────────────────────────────────

    /**
     * @brief Set the sample rate.
     * @param hz  Desired sample rate (8000 – 192000 Hz).
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> setSampleRate(uint32_t hz);

    /**
     * @brief Set the channel count.
     * @param count  1 (mono) or 2 (stereo).
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> setChannels(uint16_t count);

    /**
     * @brief Set the bit depth.
     * @param bits  16 or 32.
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> setBitDepth(uint16_t bits);

    /**
     * @brief Query device capabilities from the daemon.
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<AudioDeviceInfo> getDeviceInfo();

    // ── Volume ─────────────────────────────────────────────────────────

    /**
     * @brief Set output / input volume.
     * @param level  0.0 (mute) to 1.0 (full).
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<void> setVolume(float level);

    /**
     * @brief Get current volume level.
     * @thread_safety  NOT safe to call from the io thread.
     */
    Result<float> getVolume();

    // ── Data callback ──────────────────────────────────────────────────

    /**
     * @brief Register the frame-ready callback.
     *
     * @param cb  Invoked on the callback thread for every captured frame.
     *            @p cb MUST return quickly.  Blocking inside @p cb will
     *            cause frame drops.
     *
     * @note  The AudioFrame::data pointer is valid ONLY inside @p cb.
     *
     * @thread_safety  Set before connect() to avoid races.
     */
    void onFrameReady(std::function<void(const AudioFrame&)> cb);

protected:
    /**
     * @brief Called by base after socket connection succeeds.
     *
     * Opens the SHM reader and registers any necessary eventfds with the
     * event loop.
     */
    Result<void> onConnected()    override;

    /**
     * @brief Called by base when the connection is lost.
     *
     * Stops capture, closes SHM reader.
     */
    void         onDisconnected() override;

private:
    std::unique_ptr<AudioShmReader>            shm_reader_;
    std::function<void(const AudioFrame&)>     frame_cb_;
    std::atomic<bool>                          capturing_{false};
    std::atomic<bool>                          playing_{false};

    std::string  shm_name_;
    int          shm_eventfd_{-1};  ///< eventfd signalled by daemon per frame

    void onShmFrameReady();
};

} // namespace middleware
