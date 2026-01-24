#include "process_manager.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <cstring>
#include <ctime>
#include <algorithm>

// Global signal handler for child processes
ProcessManager* g_process_manager = nullptr;

void globalChildSignalHandler(int sig) {
    if (sig == SIGCHLD) {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            // Child terminated
        }
    }
}

// ============================================================================
// Singleton Implementation
// ============================================================================
ProcessManager& ProcessManager::getInstance() {
    static ProcessManager instance;
    return instance;
}

// ============================================================================
// Constructor aur Destructor
// ============================================================================
ProcessManager::ProcessManager() 
    : monitoring_active(false), monitoring_thread(nullptr) {
    g_process_manager = this;
    signal(SIGCHLD, globalChildSignalHandler);
}

ProcessManager::~ProcessManager() {
    stopMonitoring();
    stopAll();
}

// ============================================================================
// Process Control - Start, Stop, Restart processes
// ============================================================================

// Spawn a new process from AppConfig
bool ProcessManager::spawnProcess(const AppConfig& config) {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    // Check if already exists
    if (processes.find(config.app_id) != processes.end()) {
        std::cout << "ERROR: Process " << config.app_id << " already exists\n";
        return false;
    }
    
    // Fork and execute
    int pid = forkAndExec(config);
    if (pid < 0) {
        std::cout << "ERROR: Failed to spawn process " << config.app_id << "\n";
        return false;
    }
    
    // Create ProcessInfo entry
    ProcessInfo proc_info;
    proc_info.name = config.app_name;
    proc_info.path = config.executable;
    proc_info.pid = pid;
    proc_info.status = ProcessStatus::RUNNING;
    proc_info.start_time = time(nullptr);
    proc_info.last_restart = proc_info.start_time;
    proc_info.priority = config.priority;
    proc_info.auto_restart = config.max_restarts > 0;
    proc_info.max_restart_attempts = config.max_restarts;
    
    // Add to process map
    processes[config.app_id] = proc_info;
    
    std::cout << "INFO: Process " << config.app_id << " spawned (PID: " << pid << ")\n";
    return true;
}

// Kill/Stop a process
bool ProcessManager::killProcess(const std::string& app_id, bool graceful) {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    auto it = processes.find(app_id);
    if (it == processes.end()) {
        std::cout << "ERROR: Process " << app_id << " not found\n";
        return false;
    }
    
    ProcessInfo& proc = it->second;
    if (proc.pid <= 0) {
        std::cout << "ERROR: Invalid PID for " << app_id << "\n";
        return false;
    }
    
    proc.status = ProcessStatus::STOPPING;
    
    if (graceful) {
        // Send SIGTERM first (polite shutdown)
        kill(proc.pid, SIGTERM);
        std::cout << "INFO: Sent SIGTERM to " << app_id << " (PID: " << proc.pid << ")\n";
    } else {
        // Send SIGKILL (force kill)
        kill(proc.pid, SIGKILL);
        std::cout << "INFO: Force killed " << app_id << " (PID: " << proc.pid << ")\n";
    }
    
    return true;
}

// Restart a process
bool ProcessManager::restartProcess(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    auto it = processes.find(app_id);
    if (it == processes.end()) {
        std::cout << "ERROR: Process " << app_id << " not found\n";
        return false;
    }
    
    ProcessInfo& proc = it->second;
    proc.status = ProcessStatus::RESTARTING;
    proc.last_restart = time(nullptr);
    proc.current_restart_count++;
    
    // Kill old process
    if (proc.pid > 0) {
        kill(proc.pid, SIGKILL);
    }
    
    std::cout << "INFO: Restarting " << app_id << " (attempt " 
              << proc.current_restart_count << ")\n";
    
    return true;
}

// ============================================================================
// Process Information Functions
// ============================================================================

ProcessInfo* ProcessManager::getProcessInfo(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    auto it = processes.find(app_id);
    if (it != processes.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<ProcessInfo> ProcessManager::getAllProcesses() {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    std::vector<ProcessInfo> result;
    for (const auto& pair : processes) {
        result.push_back(pair.second);
    }
    return result;
}

int ProcessManager::getProcessCount() {
    std::lock_guard<std::mutex> lock(process_mutex);
    return processes.size();
}

ProcessInfo* ProcessManager::getProcessByPID(int pid) {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    for (auto& pair : processes) {
        if (pair.second.pid == pid) {
            return &pair.second;
        }
    }
    return nullptr;
}

// ============================================================================
// Monitoring Functions
// ============================================================================

void ProcessManager::startMonitoring() {
    if (monitoring_active) {
        std::cout << "WARNING: Monitoring already active\n";
        return;
    }
    
    monitoring_active = true;
    monitoring_thread = new std::thread(&ProcessManager::monitoringLoop, this);
    std::cout << "INFO: Process monitoring started\n";
}

void ProcessManager::stopMonitoring() {
    if (!monitoring_active) {
        return;
    }
    
    monitoring_active = false;
    if (monitoring_thread) {
        monitoring_thread->join();
        delete monitoring_thread;
        monitoring_thread = nullptr;
    }
    std::cout << "INFO: Process monitoring stopped\n";
}

void ProcessManager::monitoringLoop() {
    while (monitoring_active) {
        {
            std::lock_guard<std::mutex> lock(process_mutex);
            
            for (auto& pair : processes) {
                ProcessInfo& proc = pair.second;
                std::string app_id = pair.first;
                
                // Check if process is still running
                int status;
                pid_t result = waitpid(proc.pid, &status, WNOHANG);
                
                if (result == proc.pid) {
                    // Process terminated
                    if (proc.status != ProcessStatus::STOPPING) {
                        proc.status = ProcessStatus::CRASHED;
                        std::cout << "WARNING: Process " << app_id << " crashed\n";
                        
                        // Auto-restart if enabled
                        if (proc.auto_restart && 
                            proc.current_restart_count < proc.max_restart_attempts) {
                            proc.current_restart_count++;
                            proc.status = ProcessStatus::RESTARTING;
                            std::cout << "INFO: Auto-restarting " << app_id << "\n";
                        }
                    } else {
                        proc.status = ProcessStatus::STOPPED;
                    }
                    proc.pid = -1;
                }
            }
        }
        
        sleep(1); // Check every second
    }
}

void ProcessManager::monitorProcess(const std::string& app_id) {
    // Monitor single process (called from monitoring loop)
    auto proc = getProcessInfo(app_id);
    if (proc) {
        // Perform health checks, restart logic, etc
    }
}

// ============================================================================
// Lifecycle Management
// ============================================================================

bool ProcessManager::startAll() {
    std::cout << "INFO: Starting all processes...\n";
    
    auto sequence = getStartupSequence();
    for (const auto& app_id : sequence) {
        std::cout << "INFO: Starting " << app_id << "\n";
        // Load config and spawn (implementation needs app registry)
    }
    
    return true;
}

bool ProcessManager::stopAll() {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    std::cout << "INFO: Stopping all processes...\n";
    
    // Stop in reverse order
    std::vector<std::pair<std::string, ProcessInfo>> procs(
        processes.begin(), processes.end());
    
    for (auto it = procs.rbegin(); it != procs.rend(); ++it) {
        if (it->second.pid > 0) {
            std::cout << "INFO: Stopping " << it->first << "\n";
            kill(it->second.pid, SIGTERM);
        }
    }
    
    return true;
}

std::vector<std::string> ProcessManager::getStartupSequence() {
    // Return processes in dependency order
    std::vector<std::string> sequence;
    
    std::lock_guard<std::mutex> lock(process_mutex);
    for (const auto& pair : processes) {
        sequence.push_back(pair.first);
    }
    
    return sequence;
}

// ============================================================================
// Status and Metrics Functions
// ============================================================================

std::string ProcessManager::getStatusString(ProcessStatus status) {
    switch (status) {
        case ProcessStatus::STOPPED:
            return "STOPPED";
        case ProcessStatus::STARTING:
            return "STARTING";
        case ProcessStatus::RUNNING:
            return "RUNNING";
        case ProcessStatus::STOPPING:
            return "STOPPING";
        case ProcessStatus::CRASHED:
            return "CRASHED";
        case ProcessStatus::RESTARTING:
            return "RESTARTING";
        default:
            return "UNKNOWN";
    }
}

ProcessMetrics ProcessManager::getProcessMetrics(const std::string& app_id) {
    ProcessMetrics metrics;
    
    auto proc = getProcessInfo(app_id);
    if (proc) {
        metrics.uptime_seconds = time(nullptr) - proc->start_time;
        metrics.total_memory = proc->memory_usage;
        metrics.average_cpu = proc->cpu_usage;
        metrics.total_restarts = proc->current_restart_count;
    }
    
    return metrics;
}

ProcessManager::SystemMetrics ProcessManager::getSystemMetrics() {
    std::lock_guard<std::mutex> lock(process_mutex);
    
    SystemMetrics metrics{0, 0, 0};
    
    for (const auto& pair : processes) {
        if (pair.second.status == ProcessStatus::RUNNING) {
            metrics.total_memory += pair.second.memory_usage;
            metrics.total_cpu += pair.second.cpu_usage;
            metrics.total_processes++;
        }
    }
    
    return metrics;
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

int ProcessManager::forkAndExec(const AppConfig& config) {
    pid_t pid = fork();
    
    if (pid < 0) {
        std::cout << "ERROR: fork() failed\n";
        return -1;
    }
    
    if (pid == 0) {
        // Child process - execute the application
        
        // Change working directory
        if (!config.working_dir.empty()) {
            chdir(config.working_dir.c_str());
        }
        
        // Prepare argv array
        std::vector<char*> argv;
        argv.push_back((char*)config.executable.c_str());
        
        for (const auto& arg : config.args) {
            argv.push_back((char*)arg.c_str());
        }
        argv.push_back(nullptr);
        
        // Set environment variables
        for (const auto& env : config.env_vars) {
            setenv(env.first.c_str(), env.second.c_str(), 1);
        }
        
        // Execute
        execvp(argv[0], argv.data());
        
        // If execvp returns, something went wrong
        std::cerr << "ERROR: execvp failed for " << config.executable << "\n";
        exit(1);
    }
    
    // Parent process
    return pid;
}
