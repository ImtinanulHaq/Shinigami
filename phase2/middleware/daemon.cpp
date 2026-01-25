#include "process_manager.hpp"
#include "app_registry.hpp"
#include "lifecycle_manager.hpp"
#include "service_discovery.hpp"
#include "commands_v2.hpp"
#include "hardware_manager.hpp"
#include "network_manager.hpp"
#include "storage_manager.hpp"
#include "power_manager.hpp"
#include "security_manager.hpp"
#include "media_manager.hpp"
#include "system_services.hpp"
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <thread>
#include <fstream>
#include <chrono>
#include <iomanip>

// ============================================================================
// Global Variables
// ============================================================================
const char* SOCKET_PATH = "/var/run/middleware.sock";
const char* LOG_FILE = "/var/log/middleware/daemon.log";
const char* PID_FILE = "/var/run/middleware.pid";

int server_socket = -1;
bool running = true;
int request_count = 0;
int connection_count = 0;

// ============================================================================
// Logging Utilities
// ============================================================================
void logMessage(const std::string& level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::cout << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
              << "] [" << level << "] " << message << "\n";
    
    // Also log to file (basic implementation)
    std::ofstream log_stream(LOG_FILE, std::ios::app);
    if (log_stream.is_open()) {
        log_stream << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
                   << "] [" << level << "] " << message << "\n";
        log_stream.close();
    }
}

// ============================================================================
// Signal Handlers
// ============================================================================
void handleSignal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        logMessage("WARN", "Received signal " + std::to_string(sig) + ", initiating graceful shutdown...");
        running = false;
    } else if (sig == SIGHUP) {
        logMessage("INFO", "Received SIGHUP - reloading configuration");
    }
}

// ============================================================================
// Client Handler Function
// ============================================================================
void handleClient(int client_fd) {
    char buffer[2048] = {0};
    connection_count++;
    
    int client_id = connection_count;
    logMessage("INFO", "Client #" + std::to_string(client_id) + " connected");
    
    while (running) {
        // Receive command
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }
        
        buffer[bytes_read] = '\0';
        
        // Remove trailing newline
        std::string cmd(buffer);
        if (!cmd.empty() && cmd.back() == '\n') {
            cmd.pop_back();
        }
        
        if (cmd.empty()) {
            continue;
        }
        
        request_count++;
        logMessage("DEBUG", "Client #" + std::to_string(client_id) + " -> " + cmd);
        
        // Execute command
        std::string response = CommandHandler::executeCommand(cmd);
        
        // Log response status
        if (response.find("OK:") == 0) {
            logMessage("INFO", "Command executed successfully: " + cmd);
        } else if (response.find("ERROR:") == 0) {
            logMessage("WARN", "Command error: " + cmd);
        }
        
        // Send response
        write(client_fd, response.c_str(), response.length());
    }
    
    logMessage("INFO", "Client #" + std::to_string(client_id) + " disconnected");
    close(client_fd);
}

// ============================================================================
// Main Daemon Function
// ============================================================================
int main() {
    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║   MicroOS Phase 2 - Middleware Daemon             ║\n";
    std::cout << "║   Enhanced Process Manager with Service Discovery ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n\n";
    
    logMessage("INFO", "========== Daemon Startup ==========");
    logMessage("INFO", "MicroOS Phase 2 Middleware Daemon starting...");
    
    // Setup signal handlers
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);
    signal(SIGHUP, handleSignal);
    signal(SIGCHLD, SIG_IGN); // Ignore child process termination
    
    // Get singleton instances - Process Management
    auto& pm = ProcessManager::getInstance();
    auto& app_registry = AppRegistry::getInstance();
    auto& lm = LifecycleManager::getInstance();
    auto& sd = ServiceDiscovery::getInstance();
    
    // Get singleton instances - Hardware & System
    auto& hm = HardwareManager::getInstance();
    auto& nm = NetworkManager::getInstance();
    auto& sm = StorageManager::getInstance();
    auto& pwm = PowerManager::getInstance();
    auto& secm = SecurityManager::getInstance();
    auto& mm = MediaManager::getInstance();
    auto& sys = SystemServices::getInstance();
    
    logMessage("INFO", "Initializing components...");
    
    // Start process monitoring
    pm.startMonitoring();
    logMessage("INFO", "✓ Process monitoring started");
    
    // Initialize hardware managers
    logMessage("INFO", "✓ Hardware manager initialized");
    logMessage("INFO", "✓ Network manager initialized");
    logMessage("INFO", "✓ Storage manager initialized");
    logMessage("INFO", "✓ Power manager initialized");
    logMessage("INFO", "✓ Security manager initialized");
    logMessage("INFO", "✓ Media manager initialized");
    logMessage("INFO", "✓ System services initialized");
    
    // Create and bind socket
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket < 0) {
        logMessage("ERROR", "Cannot create socket");
        return 1;
    }
    
    // Remove old socket file
    unlink(SOCKET_PATH);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logMessage("ERROR", "Cannot bind socket at " + std::string(SOCKET_PATH));
        return 1;
    }
    
    if (listen(server_socket, 10) < 0) {
        logMessage("ERROR", "Cannot listen on socket");
        return 1;
    }
    
    logMessage("INFO", "✓ Socket listening at " + std::string(SOCKET_PATH));
    logMessage("INFO", "✓ App Registry initialized (" + std::to_string(app_registry.getAppCount()) + " apps)");
    logMessage("INFO", "✓ Service Discovery active");
    
    std::cout << "\n";
    std::cout << "INFO: Daemon ready and accepting connections...\n";
    std::cout << "INFO: Log file: " << LOG_FILE << "\n";
    std::cout << "INFO: PID: " << getpid() << "\n\n";
    
    logMessage("INFO", "Daemon ready and accepting connections on " + std::string(SOCKET_PATH));
    
    // Main accept loop
    while (running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_socket, 
                              (struct sockaddr*)&client_addr, 
                              &client_len);
        
        if (client_fd < 0) {
            if (running) {
                logMessage("WARN", "Accept failed, continuing...");
            }
            continue;
        }
        
        // Handle client in thread
        std::thread client_thread(handleClient, client_fd);
        client_thread.detach();
    }
    
    // Cleanup
    logMessage("INFO", "========== Daemon Shutdown ==========");
    logMessage("INFO", "Shutting down daemon...");
    
    std::cout << "\nINFO: Shutting down daemon...\n";
    
    pm.stopMonitoring();
    lm.gracefulShutdown();
    
    close(server_socket);
    unlink(SOCKET_PATH);
    
    logMessage("INFO", "✓ Daemon shutdown complete");
    logMessage("INFO", "Total requests processed: " + std::to_string(request_count));
    logMessage("INFO", "Total connections handled: " + std::to_string(connection_count));
    
    std::cout << "✓ Daemon shutdown complete\n";
    std::cout << "✓ Processed " << request_count << " requests from " 
              << connection_count << " clients\n\n";
    
    return 0;
}
