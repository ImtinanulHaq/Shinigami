#ifndef APP_REGISTRY_HPP
#define APP_REGISTRY_HPP

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include "app_config.hpp"

// ============================================================================
// AppRegistry Class - Applications ko register aur manage karna
// ============================================================================
class AppRegistry {
public:
    // Singleton pattern - sirf ek instance
    static AppRegistry& getInstance();
    
    // Delete copy constructor aur assignment
    AppRegistry(const AppRegistry&) = delete;
    AppRegistry& operator=(const AppRegistry&) = delete;
    
    // ========================================================================
    // Registration Functions - Apps ko register karna
    // ========================================================================
    
    // Register a new application
    bool registerApp(const AppConfig& config);
    
    // Unregister an application
    bool unregisterApp(const std::string& app_id);
    
    // Update existing app configuration
    bool updateApp(const AppConfig& config);
    
    // ========================================================================
    // Lookup Functions - Apps ko dhundna
    // ========================================================================
    
    // Get app config by ID
    AppConfig* getApp(const std::string& app_id);
    
    // Get all registered apps
    std::vector<AppConfig> getAllApps();
    
    // Get app by name
    AppConfig* getAppByName(const std::string& name);
    
    // Get app by port
    AppConfig* getAppByPort(uint16_t port);
    
    // ========================================================================
    // Dependency Management - Dependencies handle karna
    // ========================================================================
    
    // Add dependency: app_id depends on dependency_id
    bool addDependency(const std::string& app_id, const std::string& dependency_id);
    
    // Remove dependency
    bool removeDependency(const std::string& app_id, const std::string& dependency_id);
    
    // Get all dependencies of an app
    std::vector<std::string> getDependencies(const std::string& app_id);
    
    // Get all apps that depend on this app
    std::vector<std::string> getDependents(const std::string& app_id);
    
    // Resolve dependency graph - startup order nikalna
    std::vector<std::string> resolveDependencyGraph();
    
    // Check for circular dependencies
    bool hasCircularDependencies();
    
    // ========================================================================
    // App Count and Status
    // ========================================================================
    
    // Get total registered apps
    int getAppCount();
    
    // Check if app exists
    bool hasApp(const std::string& app_id);
    
    // Get all auto-start apps
    std::vector<AppConfig> getAutoStartApps();
    
    // Get all critical apps
    std::vector<AppConfig> getCriticalApps();
    
    // ========================================================================
    // Persistence Functions - Registry ko save/load karna
    // ========================================================================
    
    // Save registry to file (JSON format)
    bool saveToFile(const std::string& filepath);
    
    // Load registry from file (JSON format)
    bool loadFromFile(const std::string& filepath);
    
    // ========================================================================
    // Search and Filter Functions
    // ========================================================================
    
    // Get apps by priority
    std::vector<AppConfig> getAppsByPriority(int priority);
    
    // Get apps matching pattern
    std::vector<AppConfig> searchApps(const std::string& pattern);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct RegistryStats {
        int total_apps;
        int auto_start_apps;
        int critical_apps;
        uint32_t total_memory_limit;
        uint32_t total_cpu_limit;
    };
    
    RegistryStats getStatistics();
    
    // ========================================================================
    // Internal Helper Functions
    // ========================================================================
    
private:
    // Constructor (private for singleton)
    AppRegistry();
    
    // Destructor
    ~AppRegistry();
    
    // DFS for circular dependency detection
    bool hasCycleDFS(const std::string& app_id, 
                     std::map<std::string, bool>& visited,
                     std::map<std::string, bool>& recursion_stack);
    
    // Topological sort for dependency resolution
    void topologicalSort(const std::string& app_id,
                        std::map<std::string, bool>& visited,
                        std::vector<std::string>& result);
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    // App registry: app_id -> AppConfig
    std::map<std::string, AppConfig> apps;
    
    // Dependency graph: app_id -> list of dependencies
    std::map<std::string, std::vector<std::string>> dependencies;
    
    // Thread safety
    std::mutex registry_mutex;
};

#endif // APP_REGISTRY_HPP
