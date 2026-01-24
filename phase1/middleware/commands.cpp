/*
 * Command Parser and Handler Implementation (C++)
 */

#include "commands.hpp"
#include "logger.hpp"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <unistd.h>
#include <cstring>

namespace MicroOS {

CommandHandler::CommandHandler() {
    start_time_ = std::time(nullptr);
    
    // Register built-in commands
    commands_["STATUS"] = [this](const std::string& args) { return cmdStatus(args); };
    commands_["PING"] = [this](const std::string& args) { return cmdPing(args); };
    commands_["HELP"] = [this](const std::string& args) { return cmdHelp(args); };
    commands_["LOG_LEVEL"] = [this](const std::string& args) { return cmdLogLevel(args); };
    commands_["SHUTDOWN"] = [this](const std::string& args) { return cmdShutdown(args); };
}

std::string CommandHandler::getUptimeString() {
    std::time_t now = std::time(nullptr);
    int uptime_sec = static_cast<int>(now - start_time_);
    int hours = uptime_sec / 3600;
    int minutes = (uptime_sec % 3600) / 60;
    int seconds = uptime_sec % 60;
    
    std::ostringstream oss;
    oss << std::setfill('0') 
        << hours << ":" 
        << std::setw(2) << minutes << ":" 
        << std::setw(2) << seconds;
    return oss.str();
}

std::string CommandHandler::cmdStatus(const std::string& args) {
    std::ostringstream oss;
    oss << "OK\n"
        << "Status: Running\n"
        << "Uptime: " << getUptimeString() << "\n"
        << "PID: " << getpid() << "\n"
        << "Version: 1.0\n";
    return oss.str();
}

std::string CommandHandler::cmdPing(const std::string& args) {
    return "OK\nPONG\n";
}

std::string CommandHandler::cmdHelp(const std::string& args) {
    return "OK\n"
           "Available Commands:\n"
           "  STATUS              - Get daemon status\n"
           "  HELP                - Show this help\n"
           "  PING                - Health check\n"
           "  LOG_LEVEL <level>   - Set log level (0-3)\n"
           "  SHUTDOWN            - Graceful shutdown\n";
}

std::string CommandHandler::cmdLogLevel(const std::string& args) {
    if (args.empty()) {
        return "ERROR\nInvalid log level. Usage: LOG_LEVEL <0-3>\n";
    }
    
    try {
        int level = std::stoi(args);
        if (level < 0 || level > 3) {
            return "ERROR\nLog level must be 0 (ERROR) to 3 (DEBUG)\n";
        }
        
        std::ostringstream oss;
        oss << "OK\nLog level set to " << level << "\n";
        return oss.str();
    } catch (...) {
        return "ERROR\nInvalid log level. Usage: LOG_LEVEL <0-3>\n";
    }
}

std::string CommandHandler::cmdShutdown(const std::string& args) {
    return "OK\nShutdown initiated. Goodbye!\n";
}

std::string CommandHandler::cmdUnknown(const std::string& args) {
    return "ERROR\nUnknown command. Type HELP for available commands.\n";
}

std::string CommandHandler::parseCommand(const std::string& command) {
    if (command.empty()) {
        return cmdUnknown("");
    }
    
    // Parse command and arguments
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;
    
    // Get remaining arguments
    std::string args;
    std::getline(iss, args);
    
    // Trim leading whitespace from args
    args.erase(0, args.find_first_not_of(" \t"));
    
    // Convert command to uppercase
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    
    // Find and execute command
    auto it = commands_.find(cmd);
    if (it != commands_.end()) {
        Logger::getInstance().info("Executed command: %s", cmd.c_str());
        return it->second(args);
    }
    
    Logger::getInstance().info("Unknown command: %s", cmd.c_str());
    return cmdUnknown("");
}

void CommandHandler::registerCommand(const std::string& name,
                                      std::function<std::string(const std::string&)> handler) {
    commands_[name] = handler;
}

} // namespace MicroOS
