/*
 * Middleware Client (C++)
 * 
 * Interactive CLI tool to communicate with the middleware daemon
 * 
 * Usage: client <socket_path>
 * Example: client /var/run/middleware.sock
 * 
 * Compile: g++ -std=c++17 -static -O2 -o client client.cpp socket.cpp logger.cpp
 */

#include "socket.hpp"
#include "logger.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>

using namespace MicroOS;

void printBanner() {
    std::cout << "╔════════════════════════════════════════════╗\n"
              << "║    MicroOS Middleware Client v1.0          ║\n"
              << "║    Connected to middleware daemon           ║\n"
              << "╚════════════════════════════════════════════╝\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <socket_path>\n";
        std::cerr << "Example: " << argv[0] << " /var/run/middleware.sock\n";
        return 1;
    }
    
    // Initialize logger (silent mode)
    Logger::getInstance().init("");
    
    // Connect to socket
    Socket client;
    if (!client.connect(argv[1])) {
        std::cerr << "Failed to connect to middleware at " << argv[1] << "\n";
        return 1;
    }
    
    printBanner();
    std::cout << "Type HELP for available commands, or exit to quit.\n\n";
    
    // Interactive command loop
    std::string command;
    while (true) {
        std::cout << "middleware> ";
        std::cout.flush();
        
        if (!std::getline(std::cin, command)) {
            break;
        }
        
        // Check for quit command
        if (command == "exit" || command == "quit") {
            std::cout << "Goodbye!\n";
            break;
        }
        
        // Skip empty commands
        if (command.empty()) {
            continue;
        }
        
        // Send command
        if (write(client.getFd(), command.c_str(), command.length()) < 0) {
            std::cerr << "Error sending command\n";
            break;
        }
        
        // Read response
        char response[1024];
        memset(response, 0, sizeof(response));
        ssize_t n = read(client.getFd(), response, sizeof(response) - 1);
        
        if (n <= 0) {
            std::cerr << "Connection closed by server\n";
            break;
        }
        
        response[n] = '\0';
        std::cout << response << "\n";
    }
    
    return 0;
}
