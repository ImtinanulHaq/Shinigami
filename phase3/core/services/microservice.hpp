#ifndef MICROSERVICE_HPP
#define MICROSERVICE_HPP

#include "../events/event_bus.hpp"
#include "../capabilities/capability.hpp"
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>

// ============================================================================
// Microservice Framework - Event-driven service model
// ============================================================================

enum class ServiceState : uint8_t {
    CREATED = 0,
    INITIALIZING = 1,
    READY = 2,
    RUNNING = 3,
    PAUSED = 4,
    STOPPING = 5,
    STOPPED = 6,
    ERROR = 7
};

// Service method signature
using ServiceMethod = std::function<std::string(const std::map<std::string, std::string>&)>;

// ============================================================================
// MicroService Base Class
// ============================================================================

class MicroService {
public:
    MicroService(const std::string& service_id, const std::string& display_name = "")
        : service_id(service_id), 
          display_name(display_name.empty() ? service_id : display_name),
          current_state(ServiceState::CREATED) {}
    
    virtual ~MicroService() = default;
    
    // Delete copy operations
    MicroService(const MicroService&) = delete;
    MicroService& operator=(const MicroService&) = delete;
    
    // ========================================================================
    // Lifecycle Management
    // ========================================================================
    
    // Initialize service (called once at startup)
    virtual bool initialize() {
        current_state = ServiceState::INITIALIZING;
        return true;
    }
    
    // Start service (called when service should begin operating)
    virtual bool start() {
        current_state = ServiceState::RUNNING;
        publishEvent(EventType::SERVICE_REGISTERED, "Service started");
        return true;
    }
    
    // Stop service (graceful shutdown)
    virtual bool stop() {
        current_state = ServiceState::STOPPING;
        publishEvent(EventType::SERVICE_UNREGISTERED, "Service stopped");
        current_state = ServiceState::STOPPED;
        return true;
    }
    
    // Pause service (temporary halt)
    virtual bool pause() {
        current_state = ServiceState::PAUSED;
        return true;
    }
    
    // Resume service
    virtual bool resume() {
        current_state = ServiceState::RUNNING;
        return true;
    }
    
    // Check service health
    virtual bool healthCheck() {
        return current_state == ServiceState::RUNNING || current_state == ServiceState::READY;
    }
    
    // ========================================================================
    // Service Identity
    // ========================================================================
    
    std::string getServiceId() const { return service_id; }
    std::string getDisplayName() const { return display_name; }
    ServiceState getState() const { return current_state; }
    
    std::string getStateString() const;
    
    // ========================================================================
    // Event Handling
    // ========================================================================
    
    // Subscribe to event
    uint64_t onEvent(EventType event_type, EventHandler handler);
    
    // Unsubscribe from event
    bool offEvent(uint64_t subscription_id);
    
    // Publish event from this service
    void publishEvent(EventType event_type, const std::string& description = "");
    
    void publishEvent(const Event& event);
    
    // ========================================================================
    // Method Registration & Calling
    // ========================================================================
    
    // Register a callable method
    void registerMethod(const std::string& method_name, ServiceMethod method);
    
    // Call a method on this service
    std::string callMethod(const std::string& method_name, 
                          const std::map<std::string, std::string>& params = {});
    
    // Get list of available methods
    std::vector<std::string> getAvailableMethods() const;
    
    // ========================================================================
    // Capability Management
    // ========================================================================
    
    // Request capability
    bool requestCapability(CapabilityType capability);
    
    // Check if we have capability
    bool hasCapability(CapabilityType capability) const;
    
    // Get all granted capabilities
    std::vector<CapabilityType> getCapabilities() const;
    
    // ========================================================================
    // Service Configuration
    // ========================================================================
    
    // Get/set service configuration
    void setConfig(const std::string& key, const std::string& value);
    std::string getConfig(const std::string& key, const std::string& default_value = "") const;
    
    // ========================================================================
    // Messaging & IPC
    // ========================================================================
    
    // Send message to another service
    bool sendMessageToService(const std::string& target_service_id, 
                             const std::string& message);
    
    // Receive message handler
    virtual void onMessageReceived(const std::string& /*from_service*/, 
                                   const std::string& /*message*/) {}
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct ServiceMetrics {
        uint64_t total_events_published;
        uint64_t total_methods_called;
        uint64_t total_messages_sent;
        std::chrono::steady_clock::time_point start_time;
    };
    
    ServiceMetrics getMetrics() const;

protected:
    // Service methods can use these for child classes
    std::string service_id;
    std::string display_name;
    ServiceState current_state;
    std::map<std::string, std::string> config;
    std::vector<uint64_t> subscriptions;
    
    // Registered methods
    std::map<std::string, ServiceMethod> methods;
    
    // Metrics tracking
    uint64_t events_published = 0;
    uint64_t methods_called = 0;
    uint64_t messages_sent = 0;
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    
    // Granted capabilities
    std::vector<CapabilityType> granted_capabilities;
};

#endif // MICROSERVICE_HPP
