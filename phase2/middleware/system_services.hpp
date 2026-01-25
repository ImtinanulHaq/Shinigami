#ifndef SYSTEM_SERVICES_HPP
#define SYSTEM_SERVICES_HPP

#include <string>
#include <vector>
#include <map>

struct SystemLog {
    std::string timestamp;
    std::string level;
    std::string message;
    std::string source;
};

struct CrashReport {
    std::string app_id;
    std::string crash_time;
    std::string error_message;
    std::string stack_trace;
};

// ============================================================================
// System Services Manager Class
// ============================================================================
class SystemServices {
public:
    static SystemServices& getInstance();
    
    // System Logging
    void logEvent(const std::string& level, const std::string& message, const std::string& source);
    std::vector<SystemLog> getSystemLogs(int limit = 100);
    std::vector<SystemLog> getSystemLogsByLevel(const std::string& level, int limit = 50);
    void clearSystemLogs();
    std::string getLogFilePath();
    
    // Crash Reporting
    bool reportCrash(const std::string& app_id, const std::string& error, const std::string& stack_trace);
    std::vector<CrashReport> getCrashReports();
    std::vector<CrashReport> getCrashReports(const std::string& app_id);
    void clearCrashReports();
    bool sendCrashReport();
    
    // System Updates
    bool checkForUpdates();
    bool installUpdate(const std::string& update_package);
    std::string getSystemVersion();
    std::string getLastUpdateTime();
    bool scheduleAutoUpdate(int hour, int minute);
    
    // App Updates
    bool checkAppUpdates(const std::string& app_id);
    bool installAppUpdate(const std::string& app_id, const std::string& update_package);
    std::vector<std::string> getAvailableAppUpdates();
    
    // Sync Management
    bool enableCloudSync(const std::string& service);
    bool disableCloudSync(const std::string& service);
    bool syncNow(const std::string& service);
    std::string getSyncStatus();
    
    // System Health Check
    bool runHealthCheck();
    std::string getHealthCheckResult();
    std::vector<std::string> getSystemIssues();
    
    // Maintenance Tasks
    bool optimizeStorage();
    bool cleanupTemp();
    bool defragmentStorage();
    bool analyzePerformance();
    
    // Diagnostics
    std::string getDiagnosticReport();
    bool collectDiagnostics();
    
private:
    SystemServices();
    ~SystemServices();
    
    std::vector<SystemLog> system_logs;
    std::vector<CrashReport> crash_reports;
    
    void initializeServices();
};

#endif // SYSTEM_SERVICES_HPP
