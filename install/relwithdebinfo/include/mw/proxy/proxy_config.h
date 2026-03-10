/**
 * @file    proxy_config.h
 * @brief   ProxyConfig — compile-time and runtime configuration for all proxies.
 *
 * Pass a ProxyConfig to any proxy constructor to customise socket paths,
 * timeouts, shared-memory layout, security settings, and logging callbacks.
 * All fields have sane production defaults; most callers need only set
 * @p service_socket_path (or leave it empty for automatic SM discovery).
 *
 * @thread_safety  ProxyConfig objects are plain structs.  Do not mutate one
 *                 while a proxy is using it.
 */

#pragma once

#include <string>
#include <functional>
#include <chrono>

namespace middleware {

/**
 * @brief All configurable parameters for a proxy instance.
 *
 * Copy-constructible and default-constructible; owns no resources.
 */
struct ProxyConfig {

    // ── Socket / discovery ──────────────────────────────────────────────

    /**
     * @brief Unix socket path of the Service Manager daemon.
     *
     * Used by ServiceDiscovery to resolve service socket paths when
     * @p service_socket_path is empty. Must already exist when connect() is
     * called (or when the reconnect loop fires).
     */
    std::string sm_socket_path = "/tmp/servicemanager.sock";

    /**
     * @brief Direct socket path for the target service daemon.
     *
     * When non-empty, SM discovery is skipped and connect() uses this path
     * directly. Useful in testing (where a mock daemon binds a known path)
     * or on embedded systems where the path is known at compile time.
     */
    std::string service_socket_path;

    // ── Timeouts ────────────────────────────────────────────────────────

    /**
     * @brief Maximum time to wait for the initial socket connect() to complete.
     *
     * If the daemon is not yet running, connect() will block for at most
     * this duration before returning ProxyError::ConnectionTimeout.
     */
    std::chrono::milliseconds connect_timeout{3000};

    /**
     * @brief Maximum time to wait for a reply after sending a request.
     *
     * Covers: startCapture / stopCapture / setSampleRate / getDeviceInfo …
     * Returns ProxyError::Timeout when exceeded.
     */
    std::chrono::milliseconds request_timeout{1000};

    /**
     * @brief Interval between automatic health ping messages to the daemon.
     *
     * The proxy sends a lightweight heartbeat at this interval so the daemon
     * can detect stuck clients.  Set to zero to disable.
     */
    std::chrono::milliseconds health_ping_interval{5000};

    /**
     * @brief Initial back-off delay before the first reconnection attempt.
     *
     * Doubled each attempt up to @p reconnect_max (exponential back-off).
     */
    std::chrono::milliseconds reconnect_initial{500};

    /**
     * @brief Maximum back-off delay between reconnection attempts.
     */
    std::chrono::milliseconds reconnect_max{30000};

    /**
     * @brief Maximum number of reconnection attempts. -1 means infinite.
     *
     * When 0, reconnection is disabled; a disconnect permanently fails.
     * When positive N, the proxy gives up after N consecutive failures and
     * calls onConnectionLost with ProxyError::DaemonCrashed.
     */
    int reconnect_max_attempts = -1;

    // ── Shared memory ───────────────────────────────────────────────────

    /**
     * @brief Whether to use POSIX shared memory for high-throughput data.
     *
     * When true (default), AudioProxy and CameraProxy use a shared memory
     * ring buffer instead of the Unix socket for frame data.  Set to false
     * in unit tests where shm_open may not be available.
     */
    bool use_shared_memory = true;

    /**
     * @brief Prefix for POSIX shared memory object names.
     *
     * Full SHM name: @p shm_name_prefix + service_name + "_" + proxy_pid.
     * Example: "/middleware_proxy_audio_12345"
     */
    std::string shm_name_prefix = "/middleware_proxy_";

    /**
     * @brief Number of frame slots in the SHM ring buffer.
     *
     * Larger values reduce frame-drop risk at the cost of more memory.
     * Must be a power of two ≥ 2.
     */
    size_t shm_buffer_count = 4;

    // ── Security ────────────────────────────────────────────────────────

    /**
     * @brief Path to the HMAC key file used for message authentication.
     *
     * The file must contain raw binary key material (16–64 bytes).
     * An empty string disables HMAC signing — permitted only in dev/test mode.
     *
     * @warning  Never log this path or its contents.
     */
    std::string hmac_key_file;

    /**
     * @brief Whether to verify the server's HMAC token on connect.
     *
     * Set to false only in unit tests with mock daemons that do not sign
     * messages.  Always true in production.
     */
    bool verify_server_token = true;

    // ── Threading ───────────────────────────────────────────────────────

    /**
     * @brief Number of callback dispatcher threads.
     *
     * 1 (default) guarantees in-order delivery of all callbacks.
     * > 1 allows parallel dispatch across different callback types (e.g.,
     *   audio frames and GPIO events in parallel) but callbacks of the same
     *   type remain strictly ordered.
     */
    int callback_thread_count = 1;

    /**
     * @brief Maximum pending callbacks in the dispatch queue.
     *
     * If the queue fills (application callback too slow), the oldest entry
     * is dropped and onError is fired with ProxyError::BufferFull.
     */
    size_t callback_queue_depth = 256;

    // ── Logging ─────────────────────────────────────────────────────────

    /**
     * @brief Enable detailed internal tracing to the log sink.
     *
     * When true, every socket write / read and SHM operation is logged.
     * Incurs measurable overhead — never enable in production hotpaths.
     */
    bool verbose_logging = false;

    /**
     * @brief Custom log callback.
     *
     * If non-null, all proxy log messages are forwarded here instead of
     * syslog.  The callback must be thread-safe (called from the io thread
     * and the callback thread).
     *
     * @param msg  Null-terminated UTF-8 log line (no trailing newline).
     */
    std::function<void(std::string_view msg)> log_callback = nullptr;
};

} // namespace middleware
