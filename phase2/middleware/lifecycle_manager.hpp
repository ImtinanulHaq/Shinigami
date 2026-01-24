#ifndef LIFECYCLE_MANAGER_HPP
#define LIFECYCLE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "app_registry.hpp"
#include "process_manager.hpp"
#include "app_config.hpp"

// ============================================================================
// LifecycleManager Class - Process lifecycle manage karna
// ============================================================================
class LifecycleManager {
public:
    // Singleton pattern
    static LifecycleManager& getInstance();
    
    LifecycleManager(const LifecycleManager&) = delete;
    LifecycleManager& operator=(const LifecycleManager&) = delete;
    
    // ========================================================================
    // Lifecycle Control Functions
    // ========================================================================
    
    // Start all apps in dependency order
    bool startAllApps();
    
    // Start specific app with dependencies
    bool startApp(const std::string& app_id);
    
    // Stop all apps in reverse order
    bool stopAllApps();
    
    // Stop specific app with dependents
    bool stopApp(const std::string& app_id);
    
    // Graceful shutdown - stop everything properly
    bool gracefulShutdown();
    
    // Get current startup state
    enum class SystemState {
        IDLE = 0,           // Kuch chal nahi raha
        BOOTING = 1,        // Startup ho raha hai
        READY = 2,          // Sab ready hai
        SHUTTING_DOWN = 3,  // Shutdown ho raha hai
        ERROR = 4           // Kuch error hua
    };
    
    SystemState getSystemState();
    
    // ========================================================================
    // Dependency Validation
    // ========================================================================
    
    // Check if dependencies are satisfied
    bool validateDependencies(const std::string& app_id);
    
    // Check for circular dependencies before starting
    bool checkCircularDependencies();
    
    // Get missing dependencies for an app
    std::vector<std::string> getMissingDependencies(const std::string& app_id);
    
    // ========================================================================
    // Health Monitoring
    // ========================================================================
    
    // Check health of all running apps
    bool checkAllHealth();
    
    // Check health of specific app
    bool checkAppHealth(const std::string& app_id);
    
    // Get overall system health status
    enum class HealthStatus {
        HEALTHY = 0,
        DEGRADED = 1,
        UNHEALTHY = 2
    };
    
    HealthStatus getHealthStatus();
    
    // ========================================================================
    // Restart and Recovery
    // ========================================================================
    
    // Restart crashed app automatically
    bool handleCrash(const std::string& app_id);
    
    // Perform recovery from error state
    bool recoverFromError();
    
    // ========================================================================
    // Statistics and Monitoring
    // ========================================================================
    
    struct LifecycleStats {
        SystemState current_state;
        HealthStatus health;
        int total_apps;
        int running_apps;
        int crashed_apps;
        int total_restarts;
        time_t uptime;
    };
    
    LifecycleStats getStatistics();
    
private:
    // Constructor
    LifecycleManager();
    
    // Destructor
    ~LifecycleManager();
    
    // Internal helper functions
    
    // Resolve startup order with dependency checking
    std::vector<std::string> resolveStartupOrder();
    
    // Start app with all dependencies
    bool startAppWithDependencies(const std::string& app_id, 
                                  std::vector<bool>& started);
    
    // Stop app with all dependents
    bool stopAppWithDependents(const std::string& app_id,
                              std::vector<bool>& stopped);
    
    // Wait for app to become ready
    bool waitForAppReady(const std::string& app_id, int timeout_seconds);
    
    // Member variables
    SystemState current_state;
    time_t boot_time;
    std::mutex state_mutex;
};

#endif // LIFECYCLE_MANAGER_HPP
