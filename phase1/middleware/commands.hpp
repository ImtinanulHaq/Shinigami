/*
 * Command Parser and Handler (C++)
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <string>
#include <map>
#include <functional>
#include <ctime>

namespace MicroOS {

class CommandHandler {
private:
    std::time_t start_time_;
    std::map<std::string, std::function<std::string(const std::string&)>> commands_;
    
    // Command implementations
    std::string cmdStatus(const std::string& args);
    std::string cmdPing(const std::string& args);
    std::string cmdHelp(const std::string& args);
    std::string cmdLogLevel(const std::string& args);
    std::string cmdShutdown(const std::string& args);
    std::string cmdUnknown(const std::string& args);
    
    // Helper function to get uptime
    std::string getUptimeString();
    
public:
    CommandHandler();
    
    // Parse and execute command
    std::string parseCommand(const std::string& command);
    
    // Register custom command (for extensibility)
    void registerCommand(const std::string& name, 
                        std::function<std::string(const std::string&)> handler);
};

} // namespace MicroOS

#endif /* COMMANDS_HPP */
