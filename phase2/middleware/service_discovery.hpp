#ifndef SERVICE_DISCOVERY_HPP
#define SERVICE_DISCOVERY_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include "app_config.hpp"
#include "process_info.hpp"

// ============================================================================
// ServiceEntry Structure - Service ka entry (hostname, port, address)
// ============================================================================
struct ServiceEntry {
    std::string service_id;        // Service ka unique ID
    std::string service_name;      // Service ka name
    std::string host;              // Hostname/IP address (localhost, 127.0.0.1)
    uint16_t port;                 // Service port
    std::string protocol;          // Protocol (TCP, UDP)
    std::map<std::string, std::string> metadata; // Extra metadata
    bool healthy;                  // Service healthy hai?
    time_t registered_time;        // Jab register hua
    
    ServiceEntry() 
        : service_id(""), service_name(""), host("127.0.0.1"),
          port(0), protocol("TCP"), healthy(true), registered_time(0) {}
};

// ============================================================================
// ServiceDiscovery Class - Services ko discover aur manage karna
// ============================================================================
class ServiceDiscovery {
public:
    // Singleton pattern
    static ServiceDiscovery& getInstance();
    
    ServiceDiscovery(const ServiceDiscovery&) = delete;
    ServiceDiscovery& operator=(const ServiceDiscovery&) = delete;
    
    // ========================================================================
    // Service Registration
    // ========================================================================
    
    // Register a service (app start hone par automatically register)
    bool registerService(const ServiceEntry& entry);
    
    // Unregister a service
    bool unregisterService(const std::string& service_id);
    
    // Update service information
    bool updateService(const ServiceEntry& entry);
    
    // Mark service as healthy/unhealthy
    bool setServiceHealth(const std::string& service_id, bool healthy);
    
    // ========================================================================
    // Service Discovery - Services dhundna
    // ========================================================================
    
    // Find service by ID
    ServiceEntry* findServiceByID(const std::string& service_id);
    
    // Find service by name
    ServiceEntry* findServiceByName(const std::string& service_name);
    
    // Find service by port
    ServiceEntry* findServiceByPort(uint16_t port);
    
    // Get all registered services
    std::vector<ServiceEntry> getAllServices();
    
    // Get all healthy services
    std::vector<ServiceEntry> getHealthyServices();
    
    // Get services by protocol
    std::vector<ServiceEntry> getServicesByProtocol(const std::string& protocol);
    
    // ========================================================================
    // Connection Information
    // ========================================================================
    
    // Get connection string (host:port)
    std::string getConnectionString(const std::string& service_id);
    
    // Get service port
    uint16_t getServicePort(const std::string& service_id);
    
    // Get service host
    std::string getServiceHost(const std::string& service_id);
    
    // ========================================================================
    // Resource Allocation - Resources allocate karna
    // ========================================================================
    
    struct ResourceAllocation {
        uint64_t memory_mb;        // Memory MB
        uint32_t cpu_percent;      // CPU percentage
        uint32_t disk_gb;          // Disk space
        uint16_t port_range_start; // Port range start
        uint16_t port_range_end;   // Port range end
    };
    
    // Allocate resources for a service
    bool allocateResources(const std::string& service_id, 
                          const ResourceAllocation& resources);
    
    // Get allocated resources
    ResourceAllocation* getAllocatedResources(const std::string& service_id);
    
    // Check if port is available
    bool isPortAvailable(uint16_t port);
    
    // Get next available port in range
    uint16_t getAvailablePort(uint16_t start, uint16_t end);
    
    // ========================================================================
    // Service Dependencies
    // ========================================================================
    
    // Register service dependency
    bool addServiceDependency(const std::string& service_id, 
                             const std::string& depends_on);
    
    // Get service dependencies
    std::vector<std::string> getServiceDependencies(const std::string& service_id);
    
    // Check if all dependencies are available
    bool areDependenciesAvailable(const std::string& service_id);
    
    // ========================================================================
    // Service Monitoring
    // ========================================================================
    
    // Get total service count
    int getServiceCount();
    
    // Get healthy service count
    int getHealthyServiceCount();
    
    // Check service availability
    bool isServiceAvailable(const std::string& service_id);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct DiscoveryStats {
        int total_services;
        int healthy_services;
        int unhealthy_services;
        uint32_t total_allocated_memory;
        uint32_t total_allocated_cpu;
    };
    
    DiscoveryStats getStatistics();
    
private:
    // Constructor
    ServiceDiscovery();
    
    // Destructor
    ~ServiceDiscovery();
    
    // Member variables
    
    // Service registry: service_id -> ServiceEntry
    std::map<std::string, ServiceEntry> services;
    
    // Resource allocations: service_id -> ResourceAllocation
    std::map<std::string, ResourceAllocation> allocations;
    
    // Service dependencies: service_id -> list of dependencies
    std::map<std::string, std::vector<std::string>> dependencies;
    
    // Thread safety
    std::mutex discovery_mutex;
};

#endif // SERVICE_DISCOVERY_HPP
