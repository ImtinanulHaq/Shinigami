#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <string>
#include <vector>
#include <map>

// ============================================================================
// AppConfig Structure - Application ka configuration
// ============================================================================
struct AppConfig {
    // Basic information
    std::string app_id;            // Unique identifier (e.g., "storage-svc")
    std::string app_name;          // Human readable name
    std::string description;       // Application ka description
    std::string executable;        // Path to executable (e.g., "/usr/bin/app")
    std::string working_dir;       // Working directory jahan run hona hai
    
    // Launch configuration
    std::vector<std::string> args; // Command line arguments
    std::map<std::string, std::string> env_vars; // Environment variables
    
    // Service properties
    int priority;                  // Launch priority (0-255)
    bool auto_start;               // Automatically start on boot?
    bool critical;                 // Critical service? (system stop ho jaye?)
    
    // Resource limits
    uint64_t memory_limit_mb;      // Max memory MB mein
    uint32_t cpu_limit_percent;    // Max CPU usage percentage
    
    // Dependencies
    std::vector<std::string> dependencies; // Kaunse services zaroori hain pehle
    std::vector<std::string> dependents;   // Ye service kinhe depend karti hai
    
    // Network configuration
    uint16_t port;                 // Service port (agar network svc ho)
    std::string bind_address;      // Bind address (0.0.0.0, 127.0.0.1)
    
    // Health monitoring
    int health_check_interval;     // Check health kitne seconds par
    std::string health_check_cmd;  // Command to check health
    
    // Restart policy
    int max_restarts;              // Max restart attempts
    uint32_t restart_delay_ms;     // Delay between restarts
    
    // Constructor
    AppConfig()
        : app_id(""), app_name(""), description(""),
          executable(""), working_dir("/"),
          priority(128), auto_start(false), critical(false),
          memory_limit_mb(256), cpu_limit_percent(100),
          port(0), bind_address("127.0.0.1"),
          health_check_interval(30), max_restarts(3),
          restart_delay_ms(1000) {}
};

#endif // APP_CONFIG_HPP
