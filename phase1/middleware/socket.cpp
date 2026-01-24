/*
 * Socket Communication Implementation (C++)
 */

#include "socket.hpp"
#include "logger.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace MicroOS {

Socket::Socket() : socket_fd_(-1) {}

Socket::~Socket() {
    close();
}

bool Socket::create(const std::string& socket_path) {
    socket_path_ = socket_path;
    
    // Create socket
    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        Logger::getInstance().error("socket() failed: %s", strerror(errno));
        return false;
    }
    
    // Remove existing socket file
    unlink(socket_path.c_str());
    
    // Setup address structure
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    // Bind socket
    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::getInstance().error("bind() failed: %s", strerror(errno));
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Listen for connections
    if (listen(socket_fd_, 5) < 0) {
        Logger::getInstance().error("listen() failed: %s", strerror(errno));
        ::close(socket_fd_);
        unlink(socket_path.c_str());
        socket_fd_ = -1;
        return false;
    }
    
    return true;
}

int Socket::accept() {
    if (socket_fd_ < 0) {
        Logger::getInstance().error("Socket not initialized");
        return -1;
    }
    
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    
    int client_fd = ::accept(socket_fd_, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EINTR) {
            Logger::getInstance().error("accept() failed: %s", strerror(errno));
        }
        return -1;
    }
    
    return client_fd;
}

void Socket::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        if (!socket_path_.empty()) {
            unlink(socket_path_.c_str());
        }
        socket_fd_ = -1;
    }
}

bool Socket::connect(const std::string& socket_path) {
    // Create socket
    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        perror("socket");
        return false;
    }
    
    // Setup address structure
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    // Connect
    if (::connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    socket_path_ = socket_path;
    return true;
}

} // namespace MicroOS
