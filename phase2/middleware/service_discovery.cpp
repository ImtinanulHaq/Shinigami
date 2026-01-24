#include "service_discovery.hpp"
#include <iostream>
#include <algorithm>
#include <ctime>

ServiceDiscovery& ServiceDiscovery::getInstance() {
    static ServiceDiscovery instance;
    return instance;
}

ServiceDiscovery::ServiceDiscovery() {
}

ServiceDiscovery::~ServiceDiscovery() {
}

// ============================================================================
// Service Registration
// ============================================================================

bool ServiceDiscovery::registerService(const ServiceEntry& entry) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    if (services.find(entry.service_id) != services.end()) {
        std::cout << "ERROR: Service " << entry.service_id << " already registered\n";
        return false;
    }
    
    ServiceEntry new_entry = entry;
    new_entry.registered_time = time(nullptr);
    
    services[entry.service_id] = new_entry;
    std::cout << "INFO: Service registered: " << entry.service_id 
              << " at " << entry.host << ":" << entry.port << "\n";
    return true;
}

bool ServiceDiscovery::unregisterService(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it == services.end()) {
        std::cout << "ERROR: Service not found: " << service_id << "\n";
        return false;
    }
    
    services.erase(it);
    allocations.erase(service_id);
    dependencies.erase(service_id);
    
    std::cout << "INFO: Service unregistered: " << service_id << "\n";
    return true;
}

bool ServiceDiscovery::updateService(const ServiceEntry& entry) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(entry.service_id);
    if (it == services.end()) {
        std::cout << "ERROR: Service not found: " << entry.service_id << "\n";
        return false;
    }
    
    services[entry.service_id] = entry;
    services[entry.service_id].registered_time = 
        (it->second.registered_time > 0) ? it->second.registered_time : time(nullptr);
    
    return true;
}

bool ServiceDiscovery::setServiceHealth(const std::string& service_id, bool healthy) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it == services.end()) {
        return false;
    }
    
    it->second.healthy = healthy;
    std::cout << "INFO: Service " << service_id << " health set to " 
              << (healthy ? "HEALTHY" : "UNHEALTHY") << "\n";
    return true;
}

// ============================================================================
// Service Discovery
// ============================================================================

ServiceEntry* ServiceDiscovery::findServiceByID(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it != services.end()) {
        return &it->second;
    }
    return nullptr;
}

ServiceEntry* ServiceDiscovery::findServiceByName(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    for (auto& pair : services) {
        if (pair.second.service_name == service_name) {
            return &pair.second;
        }
    }
    return nullptr;
}

ServiceEntry* ServiceDiscovery::findServiceByPort(uint16_t port) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    for (auto& pair : services) {
        if (pair.second.port == port) {
            return &pair.second;
        }
    }
    return nullptr;
}

std::vector<ServiceEntry> ServiceDiscovery::getAllServices() {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    std::vector<ServiceEntry> result;
    for (const auto& pair : services) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<ServiceEntry> ServiceDiscovery::getHealthyServices() {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    std::vector<ServiceEntry> result;
    for (const auto& pair : services) {
        if (pair.second.healthy) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<ServiceEntry> ServiceDiscovery::getServicesByProtocol(const std::string& protocol) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    std::vector<ServiceEntry> result;
    for (const auto& pair : services) {
        if (pair.second.protocol == protocol) {
            result.push_back(pair.second);
        }
    }
    return result;
}

// ============================================================================
// Connection Information
// ============================================================================

std::string ServiceDiscovery::getConnectionString(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it == services.end()) {
        return "";
    }
    
    return it->second.host + ":" + std::to_string(it->second.port);
}

uint16_t ServiceDiscovery::getServicePort(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it != services.end()) {
        return it->second.port;
    }
    return 0;
}

std::string ServiceDiscovery::getServiceHost(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it != services.end()) {
        return it->second.host;
    }
    return "";
}

// ============================================================================
// Resource Allocation
// ============================================================================

bool ServiceDiscovery::allocateResources(const std::string& service_id, 
                                        const ResourceAllocation& resources) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    if (services.find(service_id) == services.end()) {
        std::cout << "ERROR: Service not found: " << service_id << "\n";
        return false;
    }
    
    allocations[service_id] = resources;
    std::cout << "INFO: Resources allocated to " << service_id 
              << " (Memory: " << resources.memory_mb << "MB, CPU: " 
              << resources.cpu_percent << "%)\n";
    return true;
}

ServiceDiscovery::ResourceAllocation* ServiceDiscovery::getAllocatedResources(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = allocations.find(service_id);
    if (it != allocations.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ServiceDiscovery::isPortAvailable(uint16_t port) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    for (const auto& pair : services) {
        if (pair.second.port == port) {
            return false;
        }
    }
    return true;
}

uint16_t ServiceDiscovery::getAvailablePort(uint16_t start, uint16_t end) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    for (uint16_t port = start; port <= end; port++) {
        bool available = true;
        
        for (const auto& pair : services) {
            if (pair.second.port == port) {
                available = false;
                break;
            }
        }
        
        if (available) {
            return port;
        }
    }
    
    return 0; // No available port
}

// ============================================================================
// Service Dependencies
// ============================================================================

bool ServiceDiscovery::addServiceDependency(const std::string& service_id, 
                                           const std::string& depends_on) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    if (services.find(service_id) == services.end() ||
        services.find(depends_on) == services.end()) {
        std::cout << "ERROR: One or both services not found\n";
        return false;
    }
    
    auto& deps = dependencies[service_id];
    if (std::find(deps.begin(), deps.end(), depends_on) == deps.end()) {
        deps.push_back(depends_on);
    }
    
    return true;
}

std::vector<std::string> ServiceDiscovery::getServiceDependencies(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = dependencies.find(service_id);
    if (it != dependencies.end()) {
        return it->second;
    }
    return std::vector<std::string>();
}

bool ServiceDiscovery::areDependenciesAvailable(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = dependencies.find(service_id);
    if (it == dependencies.end()) {
        return true;
    }
    
    for (const auto& dep : it->second) {
        auto dep_it = services.find(dep);
        if (dep_it == services.end() || !dep_it->second.healthy) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Service Monitoring
// ============================================================================

int ServiceDiscovery::getServiceCount() {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    return services.size();
}

int ServiceDiscovery::getHealthyServiceCount() {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    int count = 0;
    for (const auto& pair : services) {
        if (pair.second.healthy) {
            count++;
        }
    }
    return count;
}

bool ServiceDiscovery::isServiceAvailable(const std::string& service_id) {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    auto it = services.find(service_id);
    if (it == services.end()) {
        return false;
    }
    
    return it->second.healthy;
}

// ============================================================================
// Statistics
// ============================================================================

ServiceDiscovery::DiscoveryStats ServiceDiscovery::getStatistics() {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    
    DiscoveryStats stats{0, 0, 0, 0, 0};
    
    for (const auto& pair : services) {
        stats.total_services++;
        if (pair.second.healthy) {
            stats.healthy_services++;
        } else {
            stats.unhealthy_services++;
        }
    }
    
    for (const auto& pair : allocations) {
        stats.total_allocated_memory += pair.second.memory_mb;
        stats.total_allocated_cpu += pair.second.cpu_percent;
    }
    
    return stats;
}
