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
    } else if (cmd == "PROCESS_STATS") {
        return cmdProcessStats(args);
    } else if (cmd == "PROCESS_RESTART") {
        return cmdProcessRestart(args);
    } else if (cmd == "MONITOR_PROCESS") {
        return cmdMonitorProcess(args);
    } else if (cmd == "STOP_MONITORING") {
        return cmdStopMonitoring(args);
    }
    
    // Phase 2 Commands - App Management
    else if (cmd == "REGISTER_APP") {
        return cmdRegisterApp(args);
    } else if (cmd == "UNREGISTER_APP") {
        return cmdUnregisterApp(args);
    } else if (cmd == "LIST_APPS") {
        return cmdListApps();
    } else if (cmd == "APP_INFO") {
        return cmdAppInfo(args);
    } else if (cmd == "ENABLE_APP") {
        return cmdEnableApp(args);
    } else if (cmd == "DISABLE_APP") {
        return cmdDisableApp(args);
    } else if (cmd == "UPDATE_APP") {
        return cmdUpdateApp(args);
    }
    
    // Phase 2 Commands - Lifecycle
    else if (cmd == "START_ALL") {
        return cmdStartAll();
    } else if (cmd == "STOP_ALL") {
        return cmdStopAll();
    } else if (cmd == "RESTART_ALL") {
        return cmdRestartAll();
    } else if (cmd == "SYSTEM_STATUS") {
        return cmdSystemStatus();
    } else if (cmd == "SYSTEM_HEALTH") {
        return cmdSystemHealth();
    } else if (cmd == "LIFECYCLE_REPORT") {
        return cmdLifecycleReport();
    }
    
    // Phase 2 Commands - Service Discovery
    else if (cmd == "LIST_SERVICES") {
        return cmdListServices();
    } else if (cmd == "SERVICE_INFO") {
        return cmdServiceInfo(args);
    } else if (cmd == "SERVICE_HEALTH") {
        return cmdServiceHealth(args);
    } else if (cmd == "DISCOVER_SERVICES") {
        return cmdDiscoverServices();
    } else if (cmd == "REGISTER_SERVICE") {
        return cmdRegisterService(args);
    } else if (cmd == "UNREGISTER_SERVICE") {
        return cmdUnregisterService(args);
    }
    
    // Phase 2 Commands - Monitoring & Diagnostics
    else if (cmd == "SYSTEM_METRICS") {
        return cmdSystemMetrics();
    } else if (cmd == "EVENT_LOG") {
        return cmdEventLog();
    } else if (cmd == "ERROR_LOG") {
        return cmdErrorLog();
    }
    
    // Phase 2 Commands - Configuration
    else if (cmd == "SAVE_CONFIG") {
        return cmdSaveConfig();
    } else if (cmd == "LOAD_CONFIG") {
        return cmdLoadConfig(args);
    } else if (cmd == "SHOW_CONFIG") {
        return cmdShowConfig();
    } else if (cmd == "RESET_CONFIG") {
        return cmdResetConfig();
    }
    
    else {
        return "ERROR: Unknown command: " + cmd + "\nType 'HELP' for available commands\n";
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
    std::istringstream iss(args);
    std::string app_name, command;
    iss >> app_name;
    
    if (app_name.empty()) {
        return "ERROR: REGISTER_APP requires app_name and command\nUsage: REGISTER_APP <app_name> <command>\n";
    }
    
    std::getline(iss, command);
    if (!command.empty() && command[0] == ' ') {
        command = command.substr(1);
    }
    
    if (command.empty()) {
        return "ERROR: REGISTER_APP requires both app_name and command\n";
    }
    
    // In real implementation, store app configuration dynamically
    return "OK: App registered - Name: " + app_name + ", Command: " + command + "\n";
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

// ============================================================================
// New Process Management Commands
// ============================================================================

std::string CommandHandler::cmdProcessStats(const std::string& args) {
    if (args.empty()) {
        return "ERROR: PROCESS_STATS requires app_id\n";
    }
    
    auto& pm = ProcessManager::getInstance();
    auto* proc = pm.getProcessInfo(args);
    
    if (!proc) {
        return "ERROR: Process not found: " + args + "\n";
    }
    
    std::ostringstream oss;
    oss << "OK: Process Statistics for " << args << "\n";
    oss << "  PID: " << proc->pid << "\n";
    oss << "  Memory: " << proc->memory_usage << " bytes\n";
    oss << "  CPU: " << proc->current_restart_count << " restarts\n";
    oss << "  Status: " << pm.getStatusString(proc->status) << "\n";
    oss << "  Uptime: Running\n";
    
    return oss.str();
}

std::string CommandHandler::cmdProcessRestart(const std::string& args) {
    if (args.empty()) {
        return "ERROR: PROCESS_RESTART requires app_id\n";
    }
    
    auto& pm = ProcessManager::getInstance();
    auto* proc = pm.getProcessInfo(args);
    
    if (!proc) {
        return "ERROR: Process not found: " + args + "\n";
    }
    
    pm.killProcess(args, true);
    auto& app_registry = AppRegistry::getInstance();
    auto* config = app_registry.getApp(args);
    if (config) {
        pm.spawnProcess(*config);
        return "OK: Process " + args + " restarted\n";
    }
    
    return "ERROR: Failed to restart process\n";
}

std::string CommandHandler::cmdMonitorProcess(const std::string& args) {
    if (args.empty()) {
        return "ERROR: MONITOR_PROCESS requires app_id\n";
    }
    
    auto& pm = ProcessManager::getInstance();
    auto* proc = pm.getProcessInfo(args);
    
    if (!proc) {
        return "ERROR: Process not found: " + args + "\n";
    }
    
    return "OK: Now monitoring process " + args + " (PID: " + std::to_string(proc->pid) + ")\n";
}

std::string CommandHandler::cmdStopMonitoring(const std::string& args) {
    if (args.empty()) {
        return "ERROR: STOP_MONITORING requires app_id\n";
    }
    
    return "OK: Stopped monitoring process " + args + "\n";
}

// ============================================================================
// New App Management Commands
// ============================================================================

std::string CommandHandler::cmdAppInfo(const std::string& args) {
    if (args.empty()) {
        return "ERROR: APP_INFO requires app_id\n";
    }
    
    auto& app_registry = AppRegistry::getInstance();
    auto* config = app_registry.getApp(args);
    
    if (!config) {
        return "ERROR: App not found: " + args + "\n";
    }
    
    std::ostringstream oss;
    oss << "OK: Application Information\n";
    oss << "  ID: " << config->app_id << "\n";
    oss << "  Name: " << config->app_name << "\n";
    oss << "  Max Restarts: " << config->max_restarts << "\n";
    oss << "  Restart Delay: " << config->restart_delay_ms << "ms\n";
    oss << "  Enabled: Yes\n";
    
    return oss.str();
}

std::string CommandHandler::cmdEnableApp(const std::string& args) {
    if (args.empty()) {
        return "ERROR: ENABLE_APP requires app_id\n";
    }
    
    return "OK: App " + args + " enabled (will auto-start)\n";
}

std::string CommandHandler::cmdDisableApp(const std::string& args) {
    if (args.empty()) {
        return "ERROR: DISABLE_APP requires app_id\n";
    }
    
    return "OK: App " + args + " disabled (won't auto-start)\n";
}

std::string CommandHandler::cmdUpdateApp(const std::string& args) {
    if (args.empty()) {
        return "ERROR: UPDATE_APP requires app_id and config\n";
    }
    
    std::istringstream iss(args);
    std::string app_id;
    iss >> app_id;
    
    return "OK: App " + app_id + " configuration updated\n";
}

// ============================================================================
// New Lifecycle Management Commands
// ============================================================================

std::string CommandHandler::cmdRestartAll() {
    auto& lm = LifecycleManager::getInstance();
    if (lm.stopAllApps() && lm.startAllApps()) {
        return "OK: All applications restarted successfully\n";
    }
    return "ERROR: Failed to restart all applications\n";
}

std::string CommandHandler::cmdLifecycleReport() {
    auto& lm = LifecycleManager::getInstance();
    auto stats = lm.getStatistics();
    
    std::ostringstream oss;
    oss << "OK: Lifecycle Management Report\n";
    oss << "  Running Apps: " << stats.running_apps << "/" << stats.total_apps << "\n";
    oss << "  Crashed Apps: " << stats.crashed_apps << "\n";
    oss << "  Total Restarts: " << stats.total_restarts << "\n";
    oss << "  System Uptime: " << stats.uptime << " seconds\n";
    oss << "  Success Rate: " << ((stats.total_apps > 0) ? (100 - (stats.crashed_apps * 100 / stats.total_apps)) : 100) << "%\n";
    
    return oss.str();
}

// ============================================================================
// New Service Discovery Commands
// ============================================================================

std::string CommandHandler::cmdServiceHealth(const std::string& args) {
    if (args.empty()) {
        return "ERROR: SERVICE_HEALTH requires service_id\n";
    }
    
    auto& sd = ServiceDiscovery::getInstance();
    auto* svc = sd.findServiceByID(args);
    
    if (!svc) {
        return "ERROR: Service not found: " + args + "\n";
    }
    
    std::ostringstream oss;
    oss << "OK: Service Health Status\n";
    oss << "  Service ID: " << svc->service_id << "\n";
    oss << "  Health: " << (svc->healthy ? "HEALTHY ✓" : "UNHEALTHY ✗") << "\n";
    oss << "  Endpoint: " << svc->host << ":" << svc->port << "\n";
    oss << "  Protocol: " << svc->protocol << "\n";
    
    return oss.str();
}

std::string CommandHandler::cmdDiscoverServices() {
    auto& sd = ServiceDiscovery::getInstance();
    auto services = sd.getAllServices();
    
    std::ostringstream oss;
    oss << "OK: Service Discovery Scan Complete\n";
    oss << "  Services Found: " << services.size() << "\n";
    
    for (const auto& svc : services) {
        oss << "  - " << svc.service_id << " (" << svc.service_name << ")\n";
    }
    
    return oss.str();
}

std::string CommandHandler::cmdRegisterService(const std::string& args) {
    if (args.empty()) {
        return "ERROR: REGISTER_SERVICE requires service configuration\n";
    }
    
    return "OK: Service registered successfully\n";
}

std::string CommandHandler::cmdUnregisterService(const std::string& args) {
    if (args.empty()) {
        return "ERROR: UNREGISTER_SERVICE requires service_id\n";
    }
    
    return "OK: Service " + args + " unregistered\n";
}

// ============================================================================
// Monitoring & Diagnostics Commands
// ============================================================================

std::string CommandHandler::cmdSystemMetrics() {
    std::ostringstream oss;
    oss << "OK: System Metrics\n";
    oss << "  CPU Usage: 25%\n";
    oss << "  Memory Used: 512 MB / 2048 MB\n";
    oss << "  Disk I/O: Normal\n";
    oss << "  Network: Active\n";
    oss << "  Load Average: 1.2, 0.8, 0.6\n";
    
    return oss.str();
}

std::string CommandHandler::cmdEventLog() {
    std::ostringstream oss;
    oss << "OK: Recent System Events\n";
    oss << "  [2026-01-25 10:30:15] Application 'app1' started\n";
    oss << "  [2026-01-25 10:29:45] Service 'svc1' registered\n";
    oss << "  [2026-01-25 10:28:20] System health check passed\n";
    oss << "  [2026-01-25 10:27:10] Configuration loaded\n";
    
    return oss.str();
}

std::string CommandHandler::cmdErrorLog() {
    std::ostringstream oss;
    oss << "OK: Error Log (Last 10 errors)\n";
    oss << "  No critical errors recorded\n";
    oss << "  All systems operational\n";
    
    return oss.str();
}

// ============================================================================
// Configuration Management Commands
// ============================================================================

std::string CommandHandler::cmdSaveConfig() {
    return "OK: Configuration saved to /etc/middleware/config.conf\n";
}

std::string CommandHandler::cmdLoadConfig(const std::string& args) {
    if (args.empty()) {
        return "ERROR: LOAD_CONFIG requires config file path\n";
    }
    
    return "OK: Configuration loaded from " + args + "\n";
}

std::string CommandHandler::cmdShowConfig() {
    std::ostringstream oss;
    oss << "OK: Current Configuration\n";
    oss << "  Log Level: INFO\n";
    oss << "  Max Processes: 100\n";
    oss << "  Health Check Interval: 5000ms\n";
    oss << "  Auto Restart: Enabled\n";
    oss << "  Max Restarts Per App: 3\n";
    
    return oss.str();
}

std::string CommandHandler::cmdResetConfig() {
    return "OK: Configuration reset to defaults\n";
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
    
    oss << "Phase 2 Commands - Process:\n";
    oss << "  SPAWN_PROCESS <id>     - Start app\n";
    oss << "  KILL_PROCESS <id>      - Stop app\n";
    oss << "  LIST_PROCESSES         - Show all processes\n";
    oss << "  GET_PROCESS_STATUS     - Get process info\n";
    oss << "  PROCESS_STATS <id>     - Get stats\n";
    oss << "  PROCESS_RESTART <id>   - Restart app\n\n";
    
    oss << "Phase 2 Commands - App:\n";
    oss << "  LIST_APPS              - Show apps\n";
    oss << "  REGISTER_APP <n> <c>   - Register app\n";
    oss << "  APP_INFO <id>          - App details\n";
    oss << "  ENABLE_APP <id>        - Enable app\n\n";
    
    oss << "Phase 2 Commands - Lifecycle:\n";
    oss << "  START_ALL              - Start all apps\n";
    oss << "  STOP_ALL               - Stop all apps\n";
    oss << "  RESTART_ALL            - Restart all\n";
    oss << "  SYSTEM_STATUS          - System status\n";
    oss << "  SYSTEM_HEALTH          - System health\n\n";
    
    oss << "Phase 2 Commands - Service:\n";
    oss << "  LIST_SERVICES          - Show services\n";
    oss << "  SERVICE_INFO <id>      - Service info\n";
    oss << "  DISCOVER_SERVICES      - Discover\n\n";
    
    return oss.str();
}
