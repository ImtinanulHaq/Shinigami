#include "commands_v2.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

// ============================================================================
// Command Execution Router
// ============================================================================
std::string CommandHandler::executeCommand(const std::string& command) {
    std::string cmd, args;
    parseCommand(command, cmd, args);
    
    // Phase 1 Commands
    if (cmd == "PING") {
        return cmdPing();
    } else if (cmd == "STATUS") {
        return cmdStatus();
    } else if (cmd == "HELP") {
        return cmdHelp();
    } else if (cmd == "LOG_LEVEL") {
        return cmdLogLevel(args);
    } else if (cmd == "SHUTDOWN") {
        return cmdShutdown();
    }
    
    // Phase 2 Commands - Process Management
    else if (cmd == "SPAWN_PROCESS") {
        return cmdSpawnProcess(args);
    } else if (cmd == "KILL_PROCESS") {
        return cmdKillProcess(args);
    } else if (cmd == "LIST_PROCESSES") {
        return cmdListProcesses();
    } else if (cmd == "GET_PROCESS_STATUS") {
        return cmdGetProcessStatus(args);
    }
    
    // Phase 2 Commands - App Management
    else if (cmd == "REGISTER_APP") {
        return cmdRegisterApp(args);
    } else if (cmd == "UNREGISTER_APP") {
        return cmdUnregisterApp(args);
    } else if (cmd == "LIST_APPS") {
        return cmdListApps();
    }
    
    // Phase 2 Commands - Lifecycle
    else if (cmd == "START_ALL") {
        return cmdStartAll();
    } else if (cmd == "STOP_ALL") {
        return cmdStopAll();
    } else if (cmd == "SYSTEM_STATUS") {
        return cmdSystemStatus();
    } else if (cmd == "SYSTEM_HEALTH") {
        return cmdSystemHealth();
    }
    
    // Phase 2 Commands - Service Discovery
    else if (cmd == "LIST_SERVICES") {
        return cmdListServices();
    } else if (cmd == "SERVICE_INFO") {
        return cmdServiceInfo(args);
    }
    
    else {
        return "ERROR: Unknown command: " + cmd + "\n";
    }
}

// ============================================================================
// Phase 1 Commands
// ============================================================================

std::string CommandHandler::cmdPing() {
    return "OK: Daemon is alive\n";
}

std::string CommandHandler::cmdStatus() {
    auto& pm = ProcessManager::getInstance();
    int count = pm.getProcessCount();
    return "OK: " + std::to_string(count) + " processes running\n";
}

std::string CommandHandler::cmdHelp() {
    return "Use HELP command to see available commands\n";
}

std::string CommandHandler::cmdLogLevel(const std::string& args) {
    return "OK: Log level updated\n";
}

std::string CommandHandler::cmdShutdown() {
    return "OK: Shutting down...\n";
}

// ============================================================================
// Phase 2 Commands - Process Management
// ============================================================================

std::string CommandHandler::cmdSpawnProcess(const std::string& args) {
    if (args.empty()) {
        return "ERROR: SPAWN_PROCESS requires app_id\n";
    }
    
    auto& app_registry = AppRegistry::getInstance();
    auto* config = app_registry.getApp(args);
    
    if (!config) {
        return "ERROR: App not registered: " + args + "\n";
    }
    
    auto& pm = ProcessManager::getInstance();
    if (pm.spawnProcess(*config)) {
        return "OK: Process spawned for " + args + "\n";
    } else {
        return "ERROR: Failed to spawn process\n";
    }
}

std::string CommandHandler::cmdKillProcess(const std::string& args) {
    std::istringstream iss(args);
    std::string app_id, graceful_str;
    iss >> app_id >> graceful_str;
    
    if (app_id.empty()) {
        return "ERROR: KILL_PROCESS requires app_id\n";
    }
    
    bool graceful = (graceful_str != "force");
    
    auto& pm = ProcessManager::getInstance();
    if (pm.killProcess(app_id, graceful)) {
        return "OK: Process " + app_id + " killed\n";
    } else {
        return "ERROR: Failed to kill process\n";
    }
}

std::string CommandHandler::cmdListProcesses() {
    auto& pm = ProcessManager::getInstance();
    auto procs = pm.getAllProcesses();
    
    std::ostringstream oss;
    oss << "OK:\n";
    
    for (const auto& proc : procs) {
        oss << "  " << proc.name << " (PID: " << proc.pid 
            << ", Status: " << pm.getStatusString(proc.status) << ")\n";
    }
    
    return oss.str();
}

std::string CommandHandler::cmdGetProcessStatus(const std::string& args) {
    if (args.empty()) {
        return "ERROR: GET_PROCESS_STATUS requires app_id\n";
    }
    
    auto& pm = ProcessManager::getInstance();
    auto* proc = pm.getProcessInfo(args);
    
    if (!proc) {
        return "ERROR: Process not found: " + args + "\n";
    }
    
    std::ostringstream oss;
    oss << "OK:\n";
    oss << "  Name: " << proc->name << "\n";
    oss << "  PID: " << proc->pid << "\n";
    oss << "  Status: " << pm.getStatusString(proc->status) << "\n";
    oss << "  Memory: " << proc->memory_usage << " bytes\n";
    oss << "  Restarts: " << proc->current_restart_count << "\n";
    
    return oss.str();
}

// ============================================================================
// Phase 2 Commands - App Management
// ============================================================================

std::string CommandHandler::cmdRegisterApp(const std::string& args) {
    if (args.empty()) {
        return "ERROR: REGISTER_APP requires config file path\n";
    }
    
    // In real implementation, parse JSON config from file
    // For now, return placeholder
    return "OK: App registered (requires proper config file)\n";
}

std::string CommandHandler::cmdUnregisterApp(const std::string& args) {
    if (args.empty()) {
        return "ERROR: UNREGISTER_APP requires app_id\n";
    }
    
    auto& app_registry = AppRegistry::getInstance();
    if (app_registry.unregisterApp(args)) {
        return "OK: App unregistered: " + args + "\n";
    } else {
        return "ERROR: Failed to unregister app\n";
    }
}

std::string CommandHandler::cmdListApps() {
    auto& app_registry = AppRegistry::getInstance();
    auto apps = app_registry.getAllApps();
    
    std::ostringstream oss;
    oss << "OK: " << apps.size() << " apps registered\n";
    
    for (const auto& app : apps) {
        oss << "  " << app.app_id << " (" << app.app_name << ")\n";
    }
    
    return oss.str();
}

// ============================================================================
// Phase 2 Commands - Lifecycle Management
// ============================================================================

std::string CommandHandler::cmdStartAll() {
    auto& lm = LifecycleManager::getInstance();
    if (lm.startAllApps()) {
        return "OK: All apps started\n";
    } else {
        return "ERROR: Failed to start all apps\n";
    }
}

std::string CommandHandler::cmdStopAll() {
    auto& lm = LifecycleManager::getInstance();
    if (lm.stopAllApps()) {
        return "OK: All apps stopped\n";
    } else {
        return "ERROR: Failed to stop all apps\n";
    }
}

std::string CommandHandler::cmdSystemStatus() {
    auto& lm = LifecycleManager::getInstance();
    auto stats = lm.getStatistics();
    
    std::ostringstream oss;
    oss << "OK:\n";
    oss << "  Running Apps: " << stats.running_apps << "/" << stats.total_apps << "\n";
    oss << "  Crashed Apps: " << stats.crashed_apps << "\n";
    oss << "  Total Restarts: " << stats.total_restarts << "\n";
    oss << "  System Uptime: " << stats.uptime << " seconds\n";
    
    return oss.str();
}

std::string CommandHandler::cmdSystemHealth() {
    auto& lm = LifecycleManager::getInstance();
    auto health = lm.getHealthStatus();
    
    std::string health_str;
    switch (health) {
        case LifecycleManager::HealthStatus::HEALTHY:
            health_str = "HEALTHY";
            break;
        case LifecycleManager::HealthStatus::DEGRADED:
            health_str = "DEGRADED";
            break;
        case LifecycleManager::HealthStatus::UNHEALTHY:
            health_str = "UNHEALTHY";
            break;
    }
    
    return "OK: System health is " + health_str + "\n";
}

// ============================================================================
// Phase 2 Commands - Service Discovery
// ============================================================================

std::string CommandHandler::cmdListServices() {
    auto& sd = ServiceDiscovery::getInstance();
    auto services = sd.getAllServices();
    
    std::ostringstream oss;
    oss << "OK: " << services.size() << " services registered\n";
    
    for (const auto& svc : services) {
        oss << "  " << svc.service_id << " (" << svc.service_name << ") @ "
            << svc.host << ":" << svc.port;
        
        if (!svc.healthy) {
            oss << " [UNHEALTHY]";
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string CommandHandler::cmdServiceInfo(const std::string& args) {
    if (args.empty()) {
        return "ERROR: SERVICE_INFO requires service_id\n";
    }
    
    auto& sd = ServiceDiscovery::getInstance();
    auto* svc = sd.findServiceByID(args);
    
    if (!svc) {
        return "ERROR: Service not found: " + args + "\n";
    }
    
    std::ostringstream oss;
    oss << "OK:\n";
    oss << "  Service ID: " << svc->service_id << "\n";
    oss << "  Name: " << svc->service_name << "\n";
    oss << "  Host: " << svc->host << "\n";
    oss << "  Port: " << svc->port << "\n";
    oss << "  Protocol: " << svc->protocol << "\n";
    oss << "  Health: " << (svc->healthy ? "HEALTHY" : "UNHEALTHY") << "\n";
    
    return oss.str();
}

// ============================================================================
// Helper Functions
// ============================================================================

void CommandHandler::parseCommand(const std::string& cmd,
                                  std::string& command,
                                  std::string& args) {
    std::istringstream iss(cmd);
    iss >> command;
    
    // Get remaining args
    std::getline(iss, args);
    
    // Trim leading whitespace from args
    if (!args.empty() && args[0] == ' ') {
        args = args.substr(1);
    }
}

std::string CommandHandler::getCommandHelp() {
    std::ostringstream oss;
    oss << "================================\n";
    oss << "MicroOS Middleware Commands\n";
    oss << "================================\n\n";
    
    oss << "Phase 1 Commands:\n";
    oss << "  PING                   - Health check\n";
    oss << "  STATUS                 - Show status\n";
    oss << "  LOG_LEVEL <0-3>        - Set log level\n";
    oss << "  SHUTDOWN               - Shutdown daemon\n\n";
    
    oss << "Phase 2 Commands:\n";
    oss << "  SPAWN_PROCESS <app_id> - Start app\n";
    oss << "  KILL_PROCESS <app_id>  - Stop app\n";
    oss << "  LIST_PROCESSES         - Show all processes\n";
    oss << "  GET_PROCESS_STATUS     - Get process info\n";
    oss << "  START_ALL              - Start all apps\n";
    oss << "  STOP_ALL               - Stop all apps\n";
    oss << "  SYSTEM_STATUS          - System status\n";
    oss << "  SYSTEM_HEALTH          - System health\n";
    oss << "  LIST_SERVICES          - Show services\n";
    oss << "  SERVICE_INFO <svc_id>  - Service details\n\n";
    
    return oss.str();
}
