/*
 * MicroOS Middleware Daemon Header (C++)
 */

#ifndef DAEMON_HPP
#define DAEMON_HPP

#include "socket.hpp"
#include "commands.hpp"
#include <string>

namespace MicroOS {

class Daemon {
private:
    static Daemon* instance_;
    bool running_;
    Socket socket_;
    std::string socket_path_;
    std::string log_file_;
    
    // Private constructor for singleton
    Daemon();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    
    // Signal handler
    static void signalHandler(int sig);
    void handleSignal(int sig);
    
    // Client handler
    void handleClient(int client_fd, CommandHandler& cmd_handler);
    
public:
    ~Daemon() = default;
    
    // Get singleton instance
    static Daemon& getInstance();
    
    // Initialize daemon
    bool initialize();
    
    // Run main loop
    void run();
    
    // Cleanup
    void cleanup();
    
    // Check if running
    bool isRunning() const { return running_; }
    
    // Stop daemon
    void stop() { running_ = false; }
};

} // namespace MicroOS

#endif /* DAEMON_HPP */
