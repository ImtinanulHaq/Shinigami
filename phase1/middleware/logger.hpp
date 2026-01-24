/*
 * Logging System for MicroOS Middleware (C++)
 * 
 * Features:
 * - Multiple log levels (ERROR, WARN, INFO, DEBUG)
 * - File and console output
 * - Timestamped messages
 * - Easy-to-use C++ interface
 */

#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace MicroOS {

enum LogLevel {
    LOG_ERROR   = 0,
    LOG_WARN    = 1,
    LOG_INFO    = 2,
    LOG_DEBUG   = 3
};

class Logger {
private:
    static Logger* instance_;
    FILE* log_file_;
    LogLevel log_level_;
    
    // Private constructor for singleton
    Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
public:
    ~Logger();
    
    // Get singleton instance
    static Logger& getInstance();
    
    // Initialize logger
    void init(const std::string& logfile = "");
    
    // Close logger
    void close();
    
    // Set log level threshold
    void setLevel(LogLevel level);
    
    // Log a message
    void log(LogLevel level, const char* fmt, ...);
    
    // Convenience methods
    void error(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void info(const char* fmt, ...);
    void debug(const char* fmt, ...);
};

// Convenience macros
#define LOG_ERROR_MSG(fmt, ...) MicroOS::Logger::getInstance().error(fmt, ##__VA_ARGS__)
#define LOG_WARN_MSG(fmt, ...)  MicroOS::Logger::getInstance().warn(fmt, ##__VA_ARGS__)
#define LOG_INFO_MSG(fmt, ...)  MicroOS::Logger::getInstance().info(fmt, ##__VA_ARGS__)
#define LOG_DEBUG_MSG(fmt, ...) MicroOS::Logger::getInstance().debug(fmt, ##__VA_ARGS__)

} // namespace MicroOS

#endif /* LOGGER_HPP */
