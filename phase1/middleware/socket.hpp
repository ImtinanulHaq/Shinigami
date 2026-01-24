/*
 * Socket Communication Layer (C++)
 * 
 * Handles:
 * - Unix domain socket creation
 * - Socket binding and listening
 * - Client connections
 * - Socket cleanup
 */

#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <string>

namespace MicroOS {

class Socket {
private:
    int socket_fd_;
    std::string socket_path_;
    
public:
    Socket();
    ~Socket();
    
    // Create and bind socket
    bool create(const std::string& socket_path);
    
    // Accept incoming connection
    int accept();
    
    // Close socket
    void close();
    
    // Connect to existing socket
    bool connect(const std::string& socket_path);
    
    // Get socket file descriptor
    int getFd() const { return socket_fd_; }
    
    // Get socket path
    const std::string& getPath() const { return socket_path_; }
};

} // namespace MicroOS

#endif /* SOCKET_HPP */
