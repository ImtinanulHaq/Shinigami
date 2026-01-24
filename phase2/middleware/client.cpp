#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>

const char* SOCKET_PATH = "/var/run/middleware.sock";

// ============================================================================
// Send command and receive response
// ============================================================================
std::string sendCommand(const std::string& cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return "ERROR: Cannot create socket";
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return "ERROR: Cannot connect to daemon";
    }
    
    // Send command
    write(sock, cmd.c_str(), cmd.length());
    write(sock, "\n", 1);
    
    // Receive response
    char buffer[4096] = {0};
    int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    
    close(sock);
    
    if (bytes_read > 0) {
        return std::string(buffer);
    }
    return "ERROR: No response from daemon";
}

// ============================================================================
// Main CLI
// ============================================================================
int main() {
    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║   MicroOS Phase 2 - Middleware Client             ║\n";
    std::cout << "║   Process Manager Interface                       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n\n";
    
    std::string input;
    
    while (true) {
        std::cout << "middleware> ";
        std::getline(std::cin, input);
        
        // Check for exit commands
        if (input == "exit" || input == "quit" || input == "SHUTDOWN") {
            std::cout << "Exiting...\n";
            break;
        }
        
        // Check for local help command
        if (input == "help" || input == "HELP") {
            std::cout << "\n================================\n";
            std::cout << "MicroOS Phase 2 Commands\n";
            std::cout << "================================\n\n";
            std::cout << "Process Management:\n";
            std::cout << "  SPAWN_PROCESS <app_id>      - Start an application\n";
            std::cout << "  KILL_PROCESS <app_id>       - Stop an application\n";
            std::cout << "  LIST_PROCESSES              - Show all processes\n";
            std::cout << "  GET_PROCESS_STATUS <id>     - Get process details\n\n";
            std::cout << "App Management:\n";
            std::cout << "  LIST_APPS                   - Show registered apps\n";
            std::cout << "  REGISTER_APP <config>       - Register new app\n";
            std::cout << "  UNREGISTER_APP <app_id>     - Remove app\n\n";
            std::cout << "Lifecycle Control:\n";
            std::cout << "  START_ALL                   - Start all apps\n";
            std::cout << "  STOP_ALL                    - Stop all apps\n";
            std::cout << "  SYSTEM_STATUS               - Show system status\n";
            std::cout << "  SYSTEM_HEALTH               - Check system health\n\n";
            std::cout << "Service Discovery:\n";
            std::cout << "  LIST_SERVICES               - Show all services\n";
            std::cout << "  SERVICE_INFO <svc_id>       - Service details\n\n";
            std::cout << "Other:\n";
            std::cout << "  PING                        - Health check\n";
            std::cout << "  STATUS                      - Daemon status\n";
            std::cout << "  exit/quit                   - Exit client\n\n";
            continue;
        }
        
        // Skip empty commands
        if (input.empty()) {
            continue;
        }
        
        // Send command to daemon
        std::string response = sendCommand(input);
        std::cout << response << "\n";
    }
    
    return 0;
}
