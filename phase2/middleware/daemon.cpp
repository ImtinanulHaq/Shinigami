#include "process_manager.hpp"
#include "app_registry.hpp"
#include "lifecycle_manager.hpp"
#include "service_discovery.hpp"
#include "commands_v2.hpp"
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <thread>

// ============================================================================
// Global Variables
// ============================================================================
const char* SOCKET_PATH = "/var/run/middleware.sock";
int server_socket = -1;
bool running = true;

// ============================================================================
// Signal Handlers
// ============================================================================
void handleSignal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        std::cout << "INFO: Received signal " << sig << ", shutting down...\n";
        running = false;
    }
}

// ============================================================================
// Client Handler Function
// ============================================================================
void handleClient(int client_fd) {
    char buffer[1024] = {0};
    
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
        
        std::cout << "INFO: Received command: " << cmd << "\n";
        
        // Execute command
        std::string response = CommandHandler::executeCommand(cmd);
        
        // Send response
        write(client_fd, response.c_str(), response.length());
    }
    
    close(client_fd);
}

// ============================================================================
// Main Daemon Function
// ============================================================================
int main() {
    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║   MicroOS Phase 2 - Middleware Daemon             ║\n";
    std::cout << "║   Process Manager with Service Discovery          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n\n";
    
    // Setup signal handlers
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);
    signal(SIGCHLD, SIG_IGN); // Ignore child process termination
    
    // Get singleton instances
    auto& pm = ProcessManager::getInstance();
    auto& app_registry = AppRegistry::getInstance();
    auto& lm = LifecycleManager::getInstance();
    auto& sd = ServiceDiscovery::getInstance();
    
    std::cout << "INFO: Initializing components...\n";
    
    // Start process monitoring
    pm.startMonitoring();
    std::cout << "✓ Process monitoring started\n";
    
    // Create and bind socket
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "ERROR: Cannot create socket\n";
        return 1;
    }
    
    // Remove old socket file
    unlink(SOCKET_PATH);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "ERROR: Cannot bind socket\n";
        return 1;
    }
    
    if (listen(server_socket, 5) < 0) {
        std::cerr << "ERROR: Cannot listen on socket\n";
        return 1;
    }
    
    std::cout << "✓ Socket listening at " << SOCKET_PATH << "\n";
    std::cout << "✓ App Registry initialized (" << app_registry.getAppCount() << " apps)\n";
    std::cout << "✓ Service Discovery active\n";
    std::cout << "\nINFO: Middleware daemon ready, accepting connections...\n\n";
    
    // Main accept loop
    while (running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_socket, 
                              (struct sockaddr*)&client_addr, 
                              &client_len);
        
        if (client_fd < 0) {
            if (running) {
                std::cerr << "ERROR: Accept failed\n";
            }
            continue;
        }
        
        std::cout << "INFO: Client connected\n";
        
        // Handle client in thread
        std::thread client_thread(handleClient, client_fd);
        client_thread.detach();
    }
    
    // Cleanup
    std::cout << "\nINFO: Shutting down daemon...\n";
    
    pm.stopMonitoring();
    lm.gracefulShutdown();
    
    close(server_socket);
    unlink(SOCKET_PATH);
    
    std::cout << "✓ Daemon shutdown complete\n";
    
    return 0;
}
