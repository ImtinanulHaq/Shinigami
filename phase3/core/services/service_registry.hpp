#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>

namespace phase3::core::services {

// Forward declaration - full include is in service_registry.cpp
class MicroService;

/// Service registry for discovering and managing microservices
class ServiceRegistry {
public:
    /// Get singleton instance
    static ServiceRegistry& getInstance();
    
    /// Register a microservice by name
    void registerService(const std::string& service_name, 
                        std::shared_ptr<MicroService> service);
    
    /// Unregister a microservice by name
    void unregisterService(const std::string& service_name);
    
    /// Get a service by name
    std::shared_ptr<MicroService> getService(const std::string& service_name) const;
    
    /// Check if service exists
    bool hasService(const std::string& service_name) const;
    
    /// Get all registered service names
    std::vector<std::string> getServiceNames() const;
    
    /// Get count of registered services
    size_t getServiceCount() const;
    
    /// Clear all registered services
    void clear();
    
    /// Service discovery by interface/capability
    std::vector<std::shared_ptr<MicroService>> 
    findServicesByCapability(const std::string& capability) const;
    
    /// Service health check - returns list of unhealthy services
    std::vector<std::string> getUnhealthyServices() const;

private:
    ServiceRegistry() = default;
    
    /// Map of service name to service instance
    std::map<std::string, std::shared_ptr<MicroService>> services;
    
    /// Prevent copying
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;
};

}  // namespace phase3::core::services
