/*
 * MicroOS Middleware Daemon (C++)
 * 
 * Core daemon that:
 * - Starts on boot
 * - Manages logging
 * - Accepts commands via socket
 * - Provides status information
 * 
 * Compile: g++ -std=c++17 -static -O2 -o daemon *.cpp
 */

#include "daemon.hpp"
#include "logger.hpp"
#include "socket.hpp"
#include "commands.hpp"
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace MicroOS {

Daemon* Daemon::instance_ = nullptr;

Daemon::Daemon() 
    : running_(true), 
      socket_path_("/var/run/middleware.sock"),
      log_file_("/var/log/middleware.log") {}

Daemon& Daemon::getInstance() {
    if (!instance_) {
        instance_ = new Daemon();
    }
    return *instance_;
}

void Daemon::signalHandler(int sig) {
    Daemon::getInstance().handleSignal(sig);
}

void Daemon::handleSignal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        Logger::getInstance().info("Received signal %d, shutting down gracefully", sig);
        running_ = false;
    }
}

bool Daemon::initialize() {
    // Initialize logger
    Logger::getInstance().init(log_file_);
    Logger::getInstance().setLevel(LOG_INFO);
    
    Logger::getInstance().info("===========================================");
    Logger::getInstance().info("MicroOS Middleware Daemon Starting");
    Logger::getInstance().info("===========================================");
    
    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    
    Logger::getInstance().info("Signal handlers installed");
    
    // Create listening socket
    Socket& socket = socket_;
    if (!socket.create(socket_path_)) {
        Logger::getInstance().error("Failed to create socket at %s", socket_path_.c_str());
        return false;
    }
    
    Logger::getInstance().info("Socket created at %s", socket_path_.c_str());
    Logger::getInstance().info("Listening for incoming connections...");
    
    return true;
}

void Daemon::run() {
    if (!initialize()) {
        cleanup();
        return;
    }
    
    CommandHandler cmd_handler;
    
    // Main loop
    while (running_) {
        int client_fd = socket_.accept();
        
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;  // Signal interrupted accept, retry
            }
            Logger::getInstance().warn("Error accepting connection: %s", strerror(errno));
            continue;
        }
        
        Logger::getInstance().debug("Accepted new connection");
        handleClient(client_fd, cmd_handler);
        close(client_fd);
    }
    
    cleanup();
}

void Daemon::handleClient(int client_fd, CommandHandler& cmd_handler) {
    char command[256];
    memset(command, 0, sizeof(command));
    
    // Read command from client
    ssize_t n = read(client_fd, command, sizeof(command) - 1);
    
    if (n <= 0) {
        Logger::getInstance().warn("Failed to read command from client");
        return;
    }
    
    command[n] = '\0';
    
    // Remove trailing newline
    if (command[n-1] == '\n') {
        command[n-1] = '\0';
    }
    
    Logger::getInstance().debug("Received command: %s", command);
    
    // Parse and execute command
    std::string response = cmd_handler.parseCommand(command);
    
    // Send response back
    if (write(client_fd, response.c_str(), response.length()) < 0) {
        Logger::getInstance().warn("Failed to write response to client: %s", strerror(errno));
    }
}

void Daemon::cleanup() {
    socket_.close();
    
    Logger::getInstance().info("===========================================");
    Logger::getInstance().info("MicroOS Middleware Daemon Shutdown Complete");
    Logger::getInstance().info("===========================================");
    Logger::getInstance().close();
}

} // namespace MicroOS

int main(int /* argc */, char* /* argv */[]) {
    MicroOS::Daemon& daemon = MicroOS::Daemon::getInstance();
    daemon.run();
    return 0;
}
