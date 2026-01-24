#include "lifecycle_manager.hpp"
#include <iostream>
#include <algorithm>
#include <ctime>
#include <unistd.h>

LifecycleManager& LifecycleManager::getInstance() {
    static LifecycleManager instance;
    return instance;
}

LifecycleManager::LifecycleManager() 
    : current_state(SystemState::IDLE), boot_time(time(nullptr)) {
}

LifecycleManager::~LifecycleManager() {
    if (current_state != SystemState::SHUTTING_DOWN) {
        gracefulShutdown();
    }
}

// ============================================================================
// Lifecycle Control Functions
// ============================================================================

bool LifecycleManager::startAllApps() {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    if (current_state == SystemState::BOOTING || current_state == SystemState::SHUTTING_DOWN) {
        std::cout << "ERROR: System not in valid state for startup\n";
        return false;
    }
    
    current_state = SystemState::BOOTING;
    std::cout << "INFO: Starting all applications...\n";
    
    auto& app_registry = AppRegistry::getInstance();
    auto startup_order = app_registry.resolveDependencyGraph();
    
    int started_count = 0;
    for (const auto& app_id : startup_order) {
        auto* config = app_registry.getApp(app_id);
        if (config && config->auto_start) {
            auto& pm = ProcessManager::getInstance();
            if (pm.spawnProcess(*config)) {
                started_count++;
            }
        }
    }
    
    current_state = SystemState::READY;
    std::cout << "INFO: Started " << started_count << " applications\n";
    return true;
}

bool LifecycleManager::startApp(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    auto& app_registry = AppRegistry::getInstance();
    auto& pm = ProcessManager::getInstance();
    
    auto* config = app_registry.getApp(app_id);
    if (!config) {
        std::cout << "ERROR: App not found: " << app_id << "\n";
        return false;
    }
    
    // Check dependencies
    auto deps = app_registry.getDependencies(app_id);
    for (const auto& dep : deps) {
        auto* dep_proc = pm.getProcessInfo(dep);
        if (!dep_proc || dep_proc->status != ProcessStatus::RUNNING) {
            std::cout << "WARNING: Dependency " << dep << " not running\n";
        }
    }
    
    std::cout << "INFO: Starting app: " << app_id << "\n";
    return pm.spawnProcess(*config);
}

bool LifecycleManager::stopAllApps() {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    current_state = SystemState::SHUTTING_DOWN;
    std::cout << "INFO: Stopping all applications...\n";
    
    auto& pm = ProcessManager::getInstance();
    auto procs = pm.getAllProcesses();
    
    // Stop in reverse order of startup
    int stopped_count = 0;
    for (auto it = procs.rbegin(); it != procs.rend(); ++it) {
        if (it->status == ProcessStatus::RUNNING) {
            pm.killProcess(it->name, true);
            stopped_count++;
        }
    }
    
    current_state = SystemState::IDLE;
    std::cout << "INFO: Stopped " << stopped_count << " applications\n";
    return true;
}

bool LifecycleManager::stopApp(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    auto& pm = ProcessManager::getInstance();
    
    std::cout << "INFO: Stopping app: " << app_id << "\n";
    return pm.killProcess(app_id, true);
}

bool LifecycleManager::gracefulShutdown() {
    std::cout << "INFO: Initiating graceful shutdown...\n";
    return stopAllApps();
}

LifecycleManager::SystemState LifecycleManager::getSystemState() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return current_state;
}

// ============================================================================
// Dependency Validation
// ============================================================================

bool LifecycleManager::validateDependencies(const std::string& app_id) {
    auto& app_registry = AppRegistry::getInstance();
    auto& pm = ProcessManager::getInstance();
    
    auto deps = app_registry.getDependencies(app_id);
    
    for (const auto& dep : deps) {
        auto* proc = pm.getProcessInfo(dep);
        if (!proc || proc->status != ProcessStatus::RUNNING) {
            return false;
        }
    }
    
    return true;
}

bool LifecycleManager::checkCircularDependencies() {
    auto& app_registry = AppRegistry::getInstance();
    return !app_registry.hasCircularDependencies();
}

std::vector<std::string> LifecycleManager::getMissingDependencies(const std::string& app_id) {
    std::vector<std::string> missing;
    
    auto& app_registry = AppRegistry::getInstance();
    auto& pm = ProcessManager::getInstance();
    
    auto deps = app_registry.getDependencies(app_id);
    
    for (const auto& dep : deps) {
        auto* proc = pm.getProcessInfo(dep);
        if (!proc || proc->status != ProcessStatus::RUNNING) {
            missing.push_back(dep);
        }
    }
    
    return missing;
}

// ============================================================================
// Health Monitoring
// ============================================================================

bool LifecycleManager::checkAllHealth() {
    auto& pm = ProcessManager::getInstance();
    auto procs = pm.getAllProcesses();
    
    bool all_healthy = true;
    for (const auto& proc : procs) {
        if (proc.consecutive_failures > 0) {
            std::cout << "WARNING: " << proc.name << " has health issues\n";
            all_healthy = false;
        }
    }
    
    return all_healthy;
}

bool LifecycleManager::checkAppHealth(const std::string& app_id) {
    auto& pm = ProcessManager::getInstance();
    auto* proc = pm.getProcessInfo(app_id);
    
    if (!proc) {
        return false;
    }
    
    return proc->status == ProcessStatus::RUNNING && 
           proc->consecutive_failures == 0;
}

LifecycleManager::HealthStatus LifecycleManager::getHealthStatus() {
    auto& pm = ProcessManager::getInstance();
    auto procs = pm.getAllProcesses();
    
    int total = procs.size();
    int healthy = 0;
    
    for (const auto& proc : procs) {
        if (proc.status == ProcessStatus::RUNNING && 
            proc.consecutive_failures == 0) {
            healthy++;
        }
    }
    
    if (healthy == total) {
        return HealthStatus::HEALTHY;
    } else if (healthy > total / 2) {
        return HealthStatus::DEGRADED;
    } else {
        return HealthStatus::UNHEALTHY;
    }
}

// ============================================================================
// Restart and Recovery
// ============================================================================

bool LifecycleManager::handleCrash(const std::string& app_id) {
    auto& pm = ProcessManager::getInstance();
    auto* proc = pm.getProcessInfo(app_id);
    
    if (!proc) {
        return false;
    }
    
    if (proc->auto_restart && 
        proc->current_restart_count < proc->max_restart_attempts) {
        std::cout << "INFO: Auto-restarting crashed app: " << app_id << "\n";
        return pm.restartProcess(app_id);
    }
    
    return false;
}

bool LifecycleManager::recoverFromError() {
    std::cout << "INFO: Recovering from error state...\n";
    
    // Check if system can recover
    if (checkCircularDependencies()) {
        return startAllApps();
    } else {
        std::cout << "ERROR: Circular dependencies detected, cannot recover\n";
        return false;
    }
}

// ============================================================================
// Statistics
// ============================================================================

LifecycleManager::LifecycleStats LifecycleManager::getStatistics() {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    auto& pm = ProcessManager::getInstance();
    auto procs = pm.getAllProcesses();
    
    LifecycleStats stats;
    stats.current_state = current_state;
    stats.health = getHealthStatus();
    stats.total_apps = procs.size();
    stats.running_apps = 0;
    stats.crashed_apps = 0;
    stats.total_restarts = 0;
    stats.uptime = time(nullptr) - boot_time;
    
    for (const auto& proc : procs) {
        if (proc.status == ProcessStatus::RUNNING) {
            stats.running_apps++;
        } else if (proc.status == ProcessStatus::CRASHED) {
            stats.crashed_apps++;
        }
        stats.total_restarts += proc.current_restart_count;
    }
    
    return stats;
}
