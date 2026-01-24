/*
 * Logging System Implementation (C++)
 */

#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace MicroOS {

Logger* Logger::instance_ = nullptr;

Logger::Logger() : log_file_(stdout), log_level_(LOG_INFO) {}

Logger::~Logger() {
    close();
}

Logger& Logger::getInstance() {
    if (!instance_) {
        instance_ = new Logger();
    }
    return *instance_;
}

void Logger::init(const std::string& logfile) {
    if (logfile.empty()) {
        log_file_ = stdout;
        return;
    }
    
    log_file_ = fopen(logfile.c_str(), "a");
    if (!log_file_) {
        std::cerr << "Warning: Could not open log file " << logfile << std::endl;
        log_file_ = stdout;
    }
}

void Logger::close() {
    if (log_file_ && log_file_ != stdout && log_file_ != stderr) {
        fclose(log_file_);
        log_file_ = nullptr;
    }
}

void Logger::setLevel(LogLevel level) {
    if (level >= LOG_ERROR && level <= LOG_DEBUG) {
        log_level_ = level;
    }
}

void Logger::log(LogLevel level, const char* fmt, ...) {
    if (level > log_level_) {
        return;
    }
    
    static const char* level_names[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    static const char* level_colors[] = {
        "\x1b[31m",  // RED
        "\x1b[33m",  // YELLOW
        "\x1b[32m",  // GREEN
        "\x1b[36m"   // CYAN
    };
    static const char* color_reset = "\x1b[0m";
    
    // Format timestamp
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    // Format message
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    // Log to file
    if (log_file_) {
        fprintf(log_file_, "[%s] [%s] %s\n",
                timestamp,
                level_names[level],
                buffer);
        fflush(log_file_);
    }
    
    // Log to console with color
    fprintf(stderr, "%s[%s] [%s]%s %s\n",
            level_colors[level],
            timestamp,
            level_names[level],
            color_reset,
            buffer);
}

void Logger::error(const char* fmt, ...) {
    if (LOG_ERROR > log_level_) return;
    
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LOG_ERROR, "%s", buffer);
}

void Logger::warn(const char* fmt, ...) {
    if (LOG_WARN > log_level_) return;
    
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LOG_WARN, "%s", buffer);
}

void Logger::info(const char* fmt, ...) {
    if (LOG_INFO > log_level_) return;
    
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LOG_INFO, "%s", buffer);
}

void Logger::debug(const char* fmt, ...) {
    if (LOG_DEBUG > log_level_) return;
    
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LOG_DEBUG, "%s", buffer);
}

} // namespace MicroOS
