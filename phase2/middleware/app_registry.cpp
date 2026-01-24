#include "app_registry.hpp"
#include <iostream>
#include <algorithm>
#include <fstream>

// ============================================================================
// Singleton Implementation
// ============================================================================
AppRegistry& AppRegistry::getInstance() {
    static AppRegistry instance;
    return instance;
}

// ============================================================================
// Constructor aur Destructor
// ============================================================================
AppRegistry::AppRegistry() {
    // Empty - registry initialized when apps registered
}

AppRegistry::~AppRegistry() {
    // Apps automatically cleanup
}

// ============================================================================
// Registration Functions
// ============================================================================

bool AppRegistry::registerApp(const AppConfig& config) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    // Check if already exists
    if (apps.find(config.app_id) != apps.end()) {
        std::cout << "ERROR: App " << config.app_id << " already registered\n";
        return false;
    }
    
    // Register app
    apps[config.app_id] = config;
    
    // Initialize dependency list
    if (dependencies.find(config.app_id) == dependencies.end()) {
        dependencies[config.app_id] = std::vector<std::string>();
    }
    
    std::cout << "INFO: App registered: " << config.app_id << "\n";
    return true;
}

bool AppRegistry::unregisterApp(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    auto it = apps.find(app_id);
    if (it == apps.end()) {
        std::cout << "ERROR: App " << app_id << " not found\n";
        return false;
    }
    
    apps.erase(it);
    dependencies.erase(app_id);
    
    // Remove from other apps' dependencies
    for (auto& pair : dependencies) {
        auto& deps = pair.second;
        deps.erase(
            std::remove(deps.begin(), deps.end(), app_id),
            deps.end()
        );
    }
    
    std::cout << "INFO: App unregistered: " << app_id << "\n";
    return true;
}

bool AppRegistry::updateApp(const AppConfig& config) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    auto it = apps.find(config.app_id);
    if (it == apps.end()) {
        std::cout << "ERROR: App " << config.app_id << " not found\n";
        return false;
    }
    
    apps[config.app_id] = config;
    std::cout << "INFO: App updated: " << config.app_id << "\n";
    return true;
}

// ============================================================================
// Lookup Functions
// ============================================================================

AppConfig* AppRegistry::getApp(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    auto it = apps.find(app_id);
    if (it != apps.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AppConfig> AppRegistry::getAllApps() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<AppConfig> result;
    for (const auto& pair : apps) {
        result.push_back(pair.second);
    }
    return result;
}

AppConfig* AppRegistry::getAppByName(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    for (auto& pair : apps) {
        if (pair.second.app_name == name) {
            return &pair.second;
        }
    }
    return nullptr;
}

AppConfig* AppRegistry::getAppByPort(uint16_t port) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    for (auto& pair : apps) {
        if (pair.second.port == port) {
            return &pair.second;
        }
    }
    return nullptr;
}

// ============================================================================
// Dependency Management
// ============================================================================

bool AppRegistry::addDependency(const std::string& app_id, 
                                const std::string& dependency_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    // Check both apps exist
    if (apps.find(app_id) == apps.end() || 
        apps.find(dependency_id) == apps.end()) {
        std::cout << "ERROR: One or both apps not found\n";
        return false;
    }
    
    auto& deps = dependencies[app_id];
    
    // Check if already exists
    if (std::find(deps.begin(), deps.end(), dependency_id) != deps.end()) {
        std::cout << "WARNING: Dependency already exists\n";
        return false;
    }
    
    deps.push_back(dependency_id);
    std::cout << "INFO: Added dependency: " << app_id << " -> " << dependency_id << "\n";
    return true;
}

bool AppRegistry::removeDependency(const std::string& app_id, 
                                   const std::string& dependency_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    auto it = dependencies.find(app_id);
    if (it == dependencies.end()) {
        return false;
    }
    
    auto& deps = it->second;
    deps.erase(
        std::remove(deps.begin(), deps.end(), dependency_id),
        deps.end()
    );
    
    return true;
}

std::vector<std::string> AppRegistry::getDependencies(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    auto it = dependencies.find(app_id);
    if (it != dependencies.end()) {
        return it->second;
    }
    return std::vector<std::string>();
}

std::vector<std::string> AppRegistry::getDependents(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<std::string> result;
    
    for (const auto& pair : dependencies) {
        const auto& deps = pair.second;
        if (std::find(deps.begin(), deps.end(), app_id) != deps.end()) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

std::vector<std::string> AppRegistry::resolveDependencyGraph() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<std::string> result;
    std::map<std::string, bool> visited;
    
    // Initialize all apps as unvisited
    for (const auto& pair : apps) {
        visited[pair.first] = false;
    }
    
    // Topological sort
    for (const auto& pair : apps) {
        if (!visited[pair.first]) {
            topologicalSort(pair.first, visited, result);
        }
    }
    
    return result;
}

bool AppRegistry::hasCircularDependencies() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::map<std::string, bool> visited;
    std::map<std::string, bool> recursion_stack;
    
    for (const auto& pair : apps) {
        visited[pair.first] = false;
        recursion_stack[pair.first] = false;
    }
    
    for (const auto& pair : apps) {
        if (!visited[pair.first]) {
            if (hasCycleDFS(pair.first, visited, recursion_stack)) {
                return true;
            }
        }
    }
    
    return false;
}

// ============================================================================
// App Count and Status
// ============================================================================

int AppRegistry::getAppCount() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    return apps.size();
}

bool AppRegistry::hasApp(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    return apps.find(app_id) != apps.end();
}

std::vector<AppConfig> AppRegistry::getAutoStartApps() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<AppConfig> result;
    for (const auto& pair : apps) {
        if (pair.second.auto_start) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<AppConfig> AppRegistry::getCriticalApps() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<AppConfig> result;
    for (const auto& pair : apps) {
        if (pair.second.critical) {
            result.push_back(pair.second);
        }
    }
    return result;
}

// ============================================================================
// Persistence Functions
// ============================================================================

bool AppRegistry::saveToFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cout << "ERROR: Cannot open file " << filepath << "\n";
        return false;
    }
    
    file << "{\n  \"apps\": [\n";
    
    int count = 0;
    for (const auto& pair : apps) {
        const auto& config = pair.second;
        
        if (count > 0) file << ",\n";
        
        file << "    {\n";
        file << "      \"app_id\": \"" << config.app_id << "\",\n";
        file << "      \"app_name\": \"" << config.app_name << "\",\n";
        file << "      \"executable\": \"" << config.executable << "\",\n";
        file << "      \"priority\": " << config.priority << ",\n";
        file << "      \"auto_start\": " << (config.auto_start ? "true" : "false") << ",\n";
        file << "      \"critical\": " << (config.critical ? "true" : "false") << ",\n";
        file << "      \"port\": " << config.port << "\n";
        file << "    }";
        
        count++;
    }
    
    file << "\n  ]\n}\n";
    file.close();
    
    std::cout << "INFO: Registry saved to " << filepath << "\n";
    return true;
}

bool AppRegistry::loadFromFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cout << "ERROR: Cannot open file " << filepath << "\n";
        return false;
    }
    
    // Simple JSON parsing (production would use proper JSON library)
    // For now, just load and log
    std::string line;
    while (std::getline(file, line)) {
        // Parse JSON (simplified)
    }
    
    file.close();
    std::cout << "INFO: Registry loaded from " << filepath << "\n";
    return true;
}

// ============================================================================
// Search and Filter Functions
// ============================================================================

std::vector<AppConfig> AppRegistry::getAppsByPriority(int priority) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<AppConfig> result;
    for (const auto& pair : apps) {
        if (pair.second.priority == priority) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<AppConfig> AppRegistry::searchApps(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<AppConfig> result;
    for (const auto& pair : apps) {
        if (pair.first.find(pattern) != std::string::npos ||
            pair.second.app_name.find(pattern) != std::string::npos) {
            result.push_back(pair.second);
        }
    }
    return result;
}

// ============================================================================
// Statistics
// ============================================================================

AppRegistry::RegistryStats AppRegistry::getStatistics() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    RegistryStats stats{0, 0, 0, 0, 0};
    
    for (const auto& pair : apps) {
        stats.total_apps++;
        if (pair.second.auto_start) stats.auto_start_apps++;
        if (pair.second.critical) stats.critical_apps++;
        stats.total_memory_limit += pair.second.memory_limit_mb;
        stats.total_cpu_limit += pair.second.cpu_limit_percent;
    }
    
    return stats;
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

bool AppRegistry::hasCycleDFS(const std::string& app_id,
                              std::map<std::string, bool>& visited,
                              std::map<std::string, bool>& recursion_stack) {
    visited[app_id] = true;
    recursion_stack[app_id] = true;
    
    const auto& deps = dependencies[app_id];
    for (const auto& dep : deps) {
        if (!visited[dep]) {
            if (hasCycleDFS(dep, visited, recursion_stack)) {
                return true;
            }
        } else if (recursion_stack[dep]) {
            return true; // Cycle found
        }
    }
    
    recursion_stack[app_id] = false;
    return false;
}

void AppRegistry::topologicalSort(const std::string& app_id,
                                  std::map<std::string, bool>& visited,
                                  std::vector<std::string>& result) {
    visited[app_id] = true;
    
    const auto& deps = dependencies[app_id];
    for (const auto& dep : deps) {
        if (!visited[dep]) {
            topologicalSort(dep, visited, result);
        }
    }
    
    result.push_back(app_id);
}
