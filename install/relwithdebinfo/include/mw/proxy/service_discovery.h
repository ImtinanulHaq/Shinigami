/**
 * @file    service_discovery.h
 * @brief   SM registry query interface — resolves service socket paths.
 *
 * ServiceDiscovery connects to the Service Manager (SM) Unix socket, sends
 * SM_MSG_LOOKUP, and returns the socket path and ring buffer name for any
 * registered service.  It also provides waitForService() to block until a
 * service registers (used during system startup ordering).
 *
 * @thread_safety
 *   findService() / waitForService() are synchronous and NOT thread-safe.
 *   Call only from the io thread or from a setup context.
 */

#pragma once

#include "../base/proxy_result.h"
#include "../base/proxy_config.h"
#include <string>
#include <chrono>

namespace middleware {

/**
 * @brief Result type returned by ServiceDiscovery::findService().
 */
struct ServiceInfo {
    std::string socket_path;   ///< Absolute Unix socket path of the service
    std::string ring_name;     ///< POSIX SHM ring path (may be empty)
    int         service_pid;   ///< PID of the service daemon
};

/**
 * @brief Queries the Service Manager registry for service socket information.
 */
class ServiceDiscovery {
public:
    /**
     * @brief Construct discovery helper.
     * @param config  Provides sm_socket_path and connect/request timeouts.
     */
    explicit ServiceDiscovery(const ProxyConfig& config);

    /**
     * @brief Look up a service by name.
     *
     * Opens a short-lived connection to the SM, sends SM_MSG_LOOKUP, waits
     * for the lookup reply, then closes the connection.
     *
     * @param service_name  Registered service name (e.g. "audio_service").
     * @return              ServiceInfo on success, or ProxyError::ServiceNotFound
     *                      if SM returns SM_ERR_NOT_FOUND.
     */
    Result<ServiceInfo> findService(const std::string& service_name);

    /**
     * @brief Block until the named service appears in SM or timeout expires.
     *
     * Polls findService() in a loop with 200 ms intervals.
     *
     * @param service_name  Registered service name.
     * @param timeout       Maximum time to wait.
     * @return              ServiceInfo once the service is found, or
     *                      ProxyError::Timeout / ProxyError::ServiceNotFound.
     */
    Result<ServiceInfo> waitForService(
        const std::string&            service_name,
        std::chrono::milliseconds     timeout = std::chrono::seconds(30));

private:
    const ProxyConfig& config_;
};

} // namespace middleware
