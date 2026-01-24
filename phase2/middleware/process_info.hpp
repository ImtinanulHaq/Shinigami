#ifndef PROCESS_INFO_HPP
#define PROCESS_INFO_HPP

#include <string>
#include <ctime>
#include <cstdint>

// ============================================================================
// ProcessStatus Enum - Process ka state (kaunsa stage mein hai process)
// ============================================================================
enum class ProcessStatus {
    STOPPED = 0,        // Process band hai
    STARTING = 1,       // Process start ho raha hai
    RUNNING = 2,        // Process normal chal raha hai
    STOPPING = 3,       // Process stop ho raha hai
    CRASHED = 4,        // Process crash ho gaya
    RESTARTING = 5      // Process restart ho raha hai
};

// ============================================================================
// ProcessInfo Structure - Har process ki information
// ============================================================================
struct ProcessInfo {
    // Process identification
    std::string name;              // Process ka naam (e.g., "storage-service")
    std::string path;              // Executable path (e.g., "/usr/bin/storage")
    int pid;                       // Process ID (kernel se assign kiya hua)
    
    // Process state
    ProcessStatus status;          // Current status (RUNNING, CRASHED, etc)
    
    // Timing information
    time_t start_time;             // Jab process start hua
    time_t last_restart;           // Akhri restart ka time
    
    // Resource management
    int priority;                  // Process priority (0-255, low to high)
    uint64_t memory_usage;         // Memory bytes mein
    uint32_t cpu_usage;            // CPU percentage (0-100)
    
    // Auto-restart configuration
    bool auto_restart;             // Crash par automatically restart kara?
    int max_restart_attempts;      // Max kitni baar restart try kro
    int current_restart_count;     // Ab tak kitni baar restart hua
    uint32_t restart_delay_ms;     // Restart ke beech mein delay (milliseconds)
    
    // Health checking
    int health_check_interval;     // Health check kitne seconds par check ho
    uint32_t consecutive_failures; // Kitni baar fail hua consecutive
    
    // Constructor - initialize sab values ko
    ProcessInfo() 
        : name(""), path(""), pid(-1), status(ProcessStatus::STOPPED),
          start_time(0), last_restart(0), priority(128), 
          memory_usage(0), cpu_usage(0), auto_restart(false),
          max_restart_attempts(3), current_restart_count(0),
          restart_delay_ms(1000), health_check_interval(5),
          consecutive_failures(0) {}
};

// ============================================================================
// ProcessMetrics Structure - Process ke performance metrics
// ============================================================================
struct ProcessMetrics {
    int uptime_seconds;            // Process kitne seconds se chala
    uint64_t total_memory;         // Total memory usage
    uint32_t average_cpu;          // Average CPU usage
    int total_restarts;            // Kaul restart hue abhi tak
    
    ProcessMetrics() 
        : uptime_seconds(0), total_memory(0), average_cpu(0), 
          total_restarts(0) {}
};

#endif // PROCESS_INFO_HPP
