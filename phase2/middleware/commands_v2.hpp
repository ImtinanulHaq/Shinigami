#ifndef COMMANDS_V2_HPP
#define COMMANDS_V2_HPP

#include <string>
#include <map>
#include <functional>
#include <vector>
#include "process_manager.hpp"
#include "app_registry.hpp"
#include "lifecycle_manager.hpp"
#include "service_discovery.hpp"

// ============================================================================
// CommandHandler Class - Extended with Phase 2 commands
// ============================================================================
class CommandHandler {
public:
    // Parse and execute command
    static std::string executeCommand(const std::string& command);
    
    // Get command help
    static std::string getCommandHelp();
    
private:
    // ========================================================================
    // Phase 1 Commands (Original)
    // ========================================================================
    
    static std::string cmdPing();
    static std::string cmdStatus();
    static std::string cmdHelp();
    static std::string cmdLogLevel(const std::string& args);
    static std::string cmdShutdown();
    
    // ========================================================================
    // Phase 2 Commands - Process Management
    // ========================================================================
    
    // SPAWN_PROCESS <app_id> - Start a process
    static std::string cmdSpawnProcess(const std::string& args);
    
    // KILL_PROCESS <app_id> [graceful] - Stop a process
    static std::string cmdKillProcess(const std::string& args);
    
    // LIST_PROCESSES - Show all processes
    static std::string cmdListProcesses();
    
    // GET_PROCESS_STATUS <app_id> - Get specific process status
    static std::string cmdGetProcessStatus(const std::string& args);
    
    // PROCESS_STATS <app_id> - Get detailed process statistics
    static std::string cmdProcessStats(const std::string& args);
    
    // PROCESS_RESTART <app_id> - Restart a process
    static std::string cmdProcessRestart(const std::string& args);
    
    // MONITOR_PROCESS <app_id> - Monitor process in real-time
    static std::string cmdMonitorProcess(const std::string& args);
    
    // STOP_MONITORING <app_id> - Stop monitoring process
    static std::string cmdStopMonitoring(const std::string& args);
    
    // ========================================================================
    // Phase 2 Commands - App Management
    // ========================================================================
    
    // REGISTER_APP <name> <cmd> - Register new app (no file needed)
    static std::string cmdRegisterApp(const std::string& args);
    
    // UNREGISTER_APP <app_id> - Remove app
    static std::string cmdUnregisterApp(const std::string& args);
    
    // LIST_APPS - Show all registered apps
    static std::string cmdListApps();
    
    // APP_INFO <app_id> - Show detailed app info
    static std::string cmdAppInfo(const std::string& args);
    
    // ENABLE_APP <app_id> - Enable app auto-start
    static std::string cmdEnableApp(const std::string& args);
    
    // DISABLE_APP <app_id> - Disable app auto-start
    static std::string cmdDisableApp(const std::string& args);
    
    // UPDATE_APP <app_id> <config> - Update app configuration
    static std::string cmdUpdateApp(const std::string& args);
    
    // ========================================================================
    // Phase 2 Commands - Lifecycle Management
    // ========================================================================
    
    // START_ALL - Start all apps
    static std::string cmdStartAll();
    
    // STOP_ALL - Stop all apps
    static std::string cmdStopAll();
    
    // RESTART_ALL - Restart all apps
    static std::string cmdRestartAll();
    
    // SYSTEM_STATUS - Get overall system status
    static std::string cmdSystemStatus();
    
    // SYSTEM_HEALTH - Get system health
    static std::string cmdSystemHealth();
    
    // LIFECYCLE_REPORT - Get detailed lifecycle statistics
    static std::string cmdLifecycleReport();
    
    // ========================================================================
    // Phase 2 Commands - Service Discovery
    // ========================================================================
    
    // LIST_SERVICES - Show all services
    static std::string cmdListServices();
    
    // SERVICE_INFO <service_id> - Get service details
    static std::string cmdServiceInfo(const std::string& args);
    
    // SERVICE_HEALTH <service_id> - Check service health
    static std::string cmdServiceHealth(const std::string& args);
    
    // DISCOVER_SERVICES - Perform service discovery
    static std::string cmdDiscoverServices();
    
    // REGISTER_SERVICE <config> - Register new service
    static std::string cmdRegisterService(const std::string& args);
    
    // UNREGISTER_SERVICE <svc_id> - Remove service
    static std::string cmdUnregisterService(const std::string& args);
    
    // ========================================================================
    // Phase 2 Commands - Monitoring & Diagnostics
    // ========================================================================
    
    // SYSTEM_METRICS - Show system resource metrics
    static std::string cmdSystemMetrics();
    
    // EVENT_LOG - Show recent system events
    static std::string cmdEventLog();
    
    // ERROR_LOG - Show recent errors
    static std::string cmdErrorLog();
    
    // ========================================================================
    // Phase 2 Commands - Configuration
    // ========================================================================
    
    // SAVE_CONFIG - Save current configuration
    static std::string cmdSaveConfig();
    
    // LOAD_CONFIG <file> - Load configuration from file
    static std::string cmdLoadConfig(const std::string& args);
    
    // SHOW_CONFIG - Display current configuration
    static std::string cmdShowConfig();
    
    // RESET_CONFIG - Reset to default configuration
    static std::string cmdResetConfig();
    static void parseCommand(const std::string& cmd, 
                            std::string& command, 
                            std::string& args);
};

#endif // COMMANDS_V2_HPP
