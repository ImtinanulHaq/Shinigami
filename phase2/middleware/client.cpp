#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <vector>
#include <sstream>
#include <algorithm>

const char* SOCKET_PATH = "/var/run/middleware.sock";
const int TIMEOUT_SEC = 5;

// ============================================================================
// Helper function to send command and receive response
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
        return "ERROR: Cannot connect to daemon. Is the daemon running?";
    }
    
    // Send command
    write(sock, cmd.c_str(), cmd.length());
    write(sock, "\n", 1);
    
    // Receive response
    char buffer[8192] = {0};
    int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    
    close(sock);
    
    if (bytes_read > 0) {
        return std::string(buffer);
    }
    return "ERROR: No response from daemon";
}

// ============================================================================
// Command parsing utilities
// ============================================================================
std::vector<std::string> parseInput(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void printHeader() {
    std::cout << "\n╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║   MicroOS Phase 2 - Middleware Client             ║\n";
    std::cout << "║   Enhanced Process Manager Interface               ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n";
    std::cout << "Type 'help' for available commands or 'menu' for interactive menu\n\n";
}

void printDetailedHelp() {
    std::cout << "\n╔═════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║              MicroOS Phase 2 - Available Commands                ║\n";
    std::cout << "╚═════════════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "┌─ SYSTEM COMMANDS ─────────────────────────────────────────────┐\n";
    std::cout << "│ PING                      - Check daemon connectivity          │\n";
    std::cout << "│ STATUS                    - Show current daemon status         │\n";
    std::cout << "│ SYSTEM_STATUS             - Show detailed system status        │\n";
    std::cout << "│ SYSTEM_HEALTH             - Check overall system health        │\n";
    std::cout << "│ LOG_LEVEL <0-3>           - Set logging level (0=debug,3=err) │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ PROCESS MANAGEMENT ──────────────────────────────────────────┐\n";
    std::cout << "│ SPAWN_PROCESS <app_id>    - Start a registered application    │\n";
    std::cout << "│ KILL_PROCESS <app_id>     - Stop a running application        │\n";
    std::cout << "│ KILL_PROCESS <app_id> force - Force stop (no graceful)        │\n";
    std::cout << "│ LIST_PROCESSES            - Show all active processes         │\n";
    std::cout << "│ GET_PROCESS_STATUS <id>   - Show detailed process info        │\n";
    std::cout << "│ PROCESS_STATS <app_id>    - Get memory and resource stats     │\n";
    std::cout << "│ PROCESS_RESTART <app_id>  - Restart a process                 │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ APPLICATION MANAGEMENT ──────────────────────────────────────┐\n";
    std::cout << "│ LIST_APPS                 - Show all registered applications   │\n";
    std::cout << "│ REGISTER_APP <name> <cmd> - Register new application          │\n";
    std::cout << "│ UNREGISTER_APP <app_id>   - Remove application registration   │\n";
    std::cout << "│ APP_INFO <app_id>         - Show application details          │\n";
    std::cout << "│ ENABLE_APP <app_id>       - Enable auto-start for app         │\n";
    std::cout << "│ DISABLE_APP <app_id>      - Disable auto-start for app        │\n";
    std::cout << "│ UPDATE_APP <app_id> <cfg> - Update application configuration  │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ LIFECYCLE CONTROL ───────────────────────────────────────────┐\n";
    std::cout << "│ START_ALL                 - Start all registered applications  │\n";
    std::cout << "│ STOP_ALL                  - Stop all running applications      │\n";
    std::cout << "│ RESTART_ALL               - Restart all applications          │\n";
    std::cout << "│ LIFECYCLE_REPORT          - Show detailed lifecycle stats      │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ SERVICE DISCOVERY ───────────────────────────────────────────┐\n";
    std::cout << "│ LIST_SERVICES             - Show all registered services      │\n";
    std::cout << "│ SERVICE_INFO <svc_id>     - Show service details              │\n";
    std::cout << "│ SERVICE_HEALTH <svc_id>   - Check service health status       │\n";
    std::cout << "│ DISCOVER_SERVICES         - Perform service discovery scan    │\n";
    std::cout << "│ REGISTER_SERVICE <cfg>    - Register new service              │\n";
    std::cout << "│ UNREGISTER_SERVICE <id>   - Remove service                    │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ MONITORING & DIAGNOSTICS ───────────────────────────────────┐\n";
    std::cout << "│ MONITOR_PROCESS <app_id>  - Start monitoring a process        │\n";
    std::cout << "│ STOP_MONITORING <app_id>  - Stop monitoring a process         │\n";
    std::cout << "│ SYSTEM_METRICS            - Show CPU, memory, I/O metrics     │\n";
    std::cout << "│ EVENT_LOG                 - Show recent system events         │\n";
    std::cout << "│ ERROR_LOG                 - Show recent errors                │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ CONFIGURATION ──────────────────────────────────────────────┐\n";
    std::cout << "│ SAVE_CONFIG               - Save current configuration         │\n";
    std::cout << "│ LOAD_CONFIG <file>        - Load configuration from file      │\n";
    std::cout << "│ SHOW_CONFIG               - Display current configuration      │\n";
    std::cout << "│ RESET_CONFIG              - Reset to default configuration    │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "┌─ UTILITY COMMANDS ────────────────────────────────────────────┐\n";
    std::cout << "│ CLEAR                     - Clear screen                       │\n";
    std::cout << "│ HISTORY                   - Show command history               │\n";
    std::cout << "│ EXIT / QUIT               - Exit the client                    │\n";
    std::cout << "│ SHUTDOWN                  - Shutdown the daemon (careful!)    │\n";
    std::cout << "│ help / HELP               - Show this help message             │\n";
    std::cout << "│ menu                      - Show interactive menu               │\n";
    std::cout << "└───────────────────────────────────────────────────────────────┘\n\n";
}

void showInteractiveMenu() {
    while (true) {
        std::cout << "\n╔═════════════════════════════════════════════════════╗\n";
        std::cout << "║           MicroOS Middleware Management Menu       ║\n";
        std::cout << "╚═════════════════════════════════════════════════════╝\n\n";
        
        std::cout << "1. Process Management\n";
        std::cout << "2. Application Management\n";
        std::cout << "3. Lifecycle Control\n";
        std::cout << "4. Service Discovery\n";
        std::cout << "5. System Monitoring\n";
        std::cout << "6. Configuration Management\n";
        std::cout << "7. View Help\n";
        std::cout << "8. Back to Command Line\n";
        std::cout << "\nSelect option (1-8): ";
        
        int choice;
        std::cin >> choice;
        std::cin.ignore();
        
        std::string cmd;
        
        switch (choice) {
            case 1:
                std::cout << "\n--- Process Management ---\n";
                std::cout << "1. List Processes\n2. Spawn Process\n3. Kill Process\n";
                std::cout << "4. Get Process Status\n5. Restart Process\n";
                int pchoice;
                std::cin >> pchoice;
                std::cin.ignore();
                
                if (pchoice == 1) {
                    cmd = "LIST_PROCESSES";
                } else if (pchoice == 2) {
                    std::cout << "Enter app_id: ";
                    std::string app_id;
                    std::getline(std::cin, app_id);
                    cmd = "SPAWN_PROCESS " + app_id;
                } else if (pchoice == 3) {
                    std::cout << "Enter app_id: ";
                    std::string app_id;
                    std::getline(std::cin, app_id);
                    cmd = "KILL_PROCESS " + app_id;
                } else if (pchoice == 4) {
                    std::cout << "Enter app_id: ";
                    std::string app_id;
                    std::getline(std::cin, app_id);
                    cmd = "GET_PROCESS_STATUS " + app_id;
                } else if (pchoice == 5) {
                    std::cout << "Enter app_id: ";
                    std::string app_id;
                    std::getline(std::cin, app_id);
                    cmd = "PROCESS_RESTART " + app_id;
                } else {
                    continue;
                }
                std::cout << "\n" << sendCommand(cmd) << "\n";
                break;
                
            case 2:
                std::cout << "\n--- Application Management ---\n";
                std::cout << "1. List Apps\n2. Register App\n3. Unregister App\n";
                std::cout << "4. App Info\n";
                std::cin >> pchoice;
                std::cin.ignore();
                
                if (pchoice == 1) {
                    cmd = "LIST_APPS";
                } else if (pchoice == 2) {
                    std::cout << "Enter app name: ";
                    std::string name;
                    std::getline(std::cin, name);
                    std::cout << "Enter app command: ";
                    std::string command;
                    std::getline(std::cin, command);
                    cmd = "REGISTER_APP " + name + " " + command;
                } else if (pchoice == 3) {
                    std::cout << "Enter app_id: ";
                    std::string app_id;
                    std::getline(std::cin, app_id);
                    cmd = "UNREGISTER_APP " + app_id;
                } else if (pchoice == 4) {
                    std::cout << "Enter app_id: ";
                    std::string app_id;
                    std::getline(std::cin, app_id);
                    cmd = "APP_INFO " + app_id;
                } else {
                    continue;
                }
                std::cout << "\n" << sendCommand(cmd) << "\n";
                break;
                
            case 3:
                std::cout << "\n--- Lifecycle Control ---\n";
                std::cout << "1. Start All\n2. Stop All\n3. Restart All\n";
                std::cout << "4. System Status\n5. System Health\n";
                std::cin >> pchoice;
                std::cin.ignore();
                
                if (pchoice == 1) {
                    cmd = "START_ALL";
                } else if (pchoice == 2) {
                    cmd = "STOP_ALL";
                } else if (pchoice == 3) {
                    cmd = "RESTART_ALL";
                } else if (pchoice == 4) {
                    cmd = "SYSTEM_STATUS";
                } else if (pchoice == 5) {
                    cmd = "SYSTEM_HEALTH";
                } else {
                    continue;
                }
                std::cout << "\n" << sendCommand(cmd) << "\n";
                break;
                
            case 4:
                std::cout << "\n--- Service Discovery ---\n";
                std::cout << "1. List Services\n2. Service Info\n3. Discover Services\n";
                std::cin >> pchoice;
                std::cin.ignore();
                
                if (pchoice == 1) {
                    cmd = "LIST_SERVICES";
                } else if (pchoice == 2) {
                    std::cout << "Enter service_id: ";
                    std::string svc_id;
                    std::getline(std::cin, svc_id);
                    cmd = "SERVICE_INFO " + svc_id;
                } else if (pchoice == 3) {
                    cmd = "DISCOVER_SERVICES";
                } else {
                    continue;
                }
                std::cout << "\n" << sendCommand(cmd) << "\n";
                break;
                
            case 5:
                std::cout << "\n--- System Monitoring ---\n";
                std::cout << "1. System Metrics\n2. Event Log\n3. Error Log\n";
                std::cin >> pchoice;
                std::cin.ignore();
                
                if (pchoice == 1) {
                    cmd = "SYSTEM_METRICS";
                } else if (pchoice == 2) {
                    cmd = "EVENT_LOG";
                } else if (pchoice == 3) {
                    cmd = "ERROR_LOG";
                } else {
                    continue;
                }
                std::cout << "\n" << sendCommand(cmd) << "\n";
                break;
                
            case 6:
                std::cout << "\n--- Configuration Management ---\n";
                std::cout << "1. Show Config\n2. Save Config\n3. Load Config\n";
                std::cout << "4. Reset Config\n";
                std::cin >> pchoice;
                std::cin.ignore();
                
                if (pchoice == 1) {
                    cmd = "SHOW_CONFIG";
                } else if (pchoice == 2) {
                    cmd = "SAVE_CONFIG";
                } else if (pchoice == 3) {
                    std::cout << "Enter config file path: ";
                    std::string path;
                    std::getline(std::cin, path);
                    cmd = "LOAD_CONFIG " + path;
                } else if (pchoice == 4) {
                    cmd = "RESET_CONFIG";
                } else {
                    continue;
                }
                std::cout << "\n" << sendCommand(cmd) << "\n";
                break;
                
            case 7:
                printDetailedHelp();
                break;
                
            case 8:
                return;
                
            default:
                std::cout << "Invalid option!\n";
        }
    }
}

// ============================================================================
// Main CLI
// ============================================================================
int main() {
    printHeader();
    
    std::string input;
    std::vector<std::string> history;
    
    while (true) {
        std::cout << "middleware> ";
        std::getline(std::cin, input);
        
        // Convert to uppercase for command comparison
        std::string cmd_upper = input;
        std::transform(cmd_upper.begin(), cmd_upper.end(), cmd_upper.begin(), ::toupper);
        
        // Check for utility commands
        if (cmd_upper == "EXIT" || cmd_upper == "QUIT") {
            std::cout << "\nExiting middleware client...\n";
            break;
        }
        
        if (cmd_upper == "CLEAR") {
            system("clear");
            printHeader();
            continue;
        }
        
        if (cmd_upper == "MENU") {
            showInteractiveMenu();
            continue;
        }
        
        if (cmd_upper == "HELP" || cmd_upper == "HELP") {
            printDetailedHelp();
            continue;
        }
        
        if (cmd_upper == "HISTORY") {
            std::cout << "\nCommand History:\n";
            for (size_t i = 0; i < history.size(); i++) {
                std::cout << i + 1 << ". " << history[i] << "\n";
            }
            std::cout << "\n";
            continue;
        }
        
        // Skip empty commands
        if (input.empty()) {
            continue;
        }
        
        // Add to history
        history.push_back(input);
        
        // Send command to daemon and get response
        std::cout << "\n";
        std::string response = sendCommand(input);
        std::cout << response;
        if (!response.empty() && response.back() != '\n') {
            std::cout << "\n";
        }
        std::cout << "\n";
    }
    
    return 0;
}
