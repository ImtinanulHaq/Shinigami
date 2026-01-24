#ifndef PROCESS_MANAGER_HPP
#define PROCESS_MANAGER_HPP

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include "process_info.hpp"
#include "app_config.hpp"

// ============================================================================
// ProcessManager Class - Processes ko manage karna (start, stop, monitor)
// ============================================================================
class ProcessManager {
public:
    // Singleton pattern - sirf ek instance
    static ProcessManager& getInstance();
    
    // Delete copy constructor aur assignment
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    
    // ========================================================================
    // Process Control Functions - Processes ko control karna
    // ========================================================================
    
    // Start a process from AppConfig
    // Return: true if successful, false otherwise
    bool spawnProcess(const AppConfig& config);
    
    // Stop a process by app_id
    // graceful: send SIGTERM pehle, phir SIGKILL (default: true)
    bool killProcess(const std::string& app_id, bool graceful = true);
    
    // Restart a process
    bool restartProcess(const std::string& app_id);
    
    // ========================================================================
    // Process Information Functions - Process ki information nikalna
    // ========================================================================
    
    // Get process info by app_id
    ProcessInfo* getProcessInfo(const std::string& app_id);
    
    // Get all running processes
    std::vector<ProcessInfo> getAllProcesses();
    
    // Get process count (kitne processes chal rahe hain)
    int getProcessCount();
    
    // Get process by PID
    ProcessInfo* getProcessByPID(int pid);
    
    // ========================================================================
    // Process Monitoring - Background monitoring thread
    // ========================================================================
    
    // Start monitoring daemon (background thread)
    void startMonitoring();
    
    // Stop monitoring daemon
    void stopMonitoring();
    
    // Monitor single process - health check, restart on crash
    void monitorProcess(const std::string& app_id);
    
    // ========================================================================
    // Lifecycle Management - Startup order aur shutdown
    // ========================================================================
    
    // Start all processes in dependency order
    bool startAll();
    
    // Stop all processes gracefully
    bool stopAll();
    
    // Get startup sequence based on dependencies
    std::vector<std::string> getStartupSequence();
    
    // ========================================================================
    // Process Status & Metrics
    // ========================================================================
    
    // Get process status string (RUNNING, CRASHED, etc)
    std::string getStatusString(ProcessStatus status);
    
    // Get process metrics (uptime, memory, etc)
    ProcessMetrics getProcessMetrics(const std::string& app_id);
    
    // Get total system resource usage
    struct SystemMetrics {
        uint64_t total_memory;
        uint32_t total_cpu;
        int total_processes;
    };
    SystemMetrics getSystemMetrics();
    
    // ========================================================================
    // Internal Helper Functions
    // ========================================================================
    
private:
    // Constructor (private for singleton)
    ProcessManager();
    
    // Destructor
    ~ProcessManager();
    
    // Internal process spawning with fork/exec
    int forkAndExec(const AppConfig& config);
    
    // Monitor thread function
    void monitoringLoop();
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    // Process map: app_id -> ProcessInfo
    std::map<std::string, ProcessInfo> processes;
    
    // Thread safety
    std::mutex process_mutex;
    
    // Monitoring state
    bool monitoring_active;
    std::thread* monitoring_thread;
    
    // Auto-restart tracking
    std::map<std::string, time_t> restart_timers;
};

#endif // PROCESS_MANAGER_HPP
