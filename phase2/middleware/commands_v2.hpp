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
    
    // ========================================================================
    // Phase 2 Commands - App Management
    // ========================================================================
    
    // REGISTER_APP <config_file> - Register new app
    static std::string cmdRegisterApp(const std::string& args);
    
    // UNREGISTER_APP <app_id> - Remove app
    static std::string cmdUnregisterApp(const std::string& args);
    
    // LIST_APPS - Show all registered apps
    static std::string cmdListApps();
    
    // ========================================================================
    // Phase 2 Commands - Lifecycle Management
    // ========================================================================
    
    // START_ALL - Start all apps
    static std::string cmdStartAll();
    
    // STOP_ALL - Stop all apps
    static std::string cmdStopAll();
    
    // SYSTEM_STATUS - Get overall system status
    static std::string cmdSystemStatus();
    
    // SYSTEM_HEALTH - Get system health
    static std::string cmdSystemHealth();
    
    // ========================================================================
    // Phase 2 Commands - Service Discovery
    // ========================================================================
    
    // LIST_SERVICES - Show all services
    static std::string cmdListServices();
    
    // SERVICE_INFO <service_id> - Get service details
    static std::string cmdServiceInfo(const std::string& args);
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
    // Parse command and arguments
    static void parseCommand(const std::string& cmd, 
                            std::string& command, 
                            std::string& args);
};

#endif // COMMANDS_V2_HPP
