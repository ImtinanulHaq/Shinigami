#ifndef STORAGE_MANAGER_HPP
#define STORAGE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>

struct StorageDevice {
    std::string device_id;
    std::string mount_point;
    std::string filesystem_type;
    long total_size_bytes;
    long used_size_bytes;
    long free_size_bytes;
    bool is_mounted;
    bool is_removable;
    int read_speed_mbps;
    int write_speed_mbps;
};

struct FileOperation {
    std::string operation_id;
    std::string source_path;
    std::string destination_path;
    std::string status;
    int progress_percent;
};

// ============================================================================
// Storage Manager Class
// ============================================================================
class StorageManager {
public:
    static StorageManager& getInstance();
    
    // Storage Device Management
    bool mountDevice(const std::string& device_id, const std::string& mount_point);
    bool unmountDevice(const std::string& device_id);
    bool formatDevice(const std::string& device_id, const std::string& filesystem_type);
    std::vector<StorageDevice> listStorageDevices();
    StorageDevice* getDeviceInfo(const std::string& device_id);
    
    // File Operations
    bool copyFile(const std::string& source, const std::string& destination);
    bool moveFile(const std::string& source, const std::string& destination);
    bool deleteFile(const std::string& file_path);
    bool deleteDirectory(const std::string& dir_path);
    std::vector<std::string> listDirectory(const std::string& dir_path);
    long getFileSize(const std::string& file_path);
    
    // Backup & Restore
    bool createBackup(const std::string& backup_source, const std::string& backup_destination);
    bool restoreBackup(const std::string& backup_path, const std::string& restore_destination);
    std::vector<std::string> listBackups();
    
    // Storage Monitoring
    long getInternalStorageUsed();
    long getInternalStorageTotal();
    long getExternalStorageUsed();
    long getExternalStorageTotal();
    long getStorageUsagePercent();
    bool enableStorageLowWarning(long threshold_bytes);
    
    // Cache Management
    bool clearCache(const std::string& app_id);
    long getCacheSize(const std::string& app_id);
    bool clearAllCache();
    
    // File Indexing
    bool indexFiles(const std::string& directory);
    std::vector<std::string> searchFiles(const std::string& query);
    
private:
    StorageManager();
    ~StorageManager();
    
    std::map<std::string, StorageDevice> storage_devices;
    std::map<std::string, FileOperation> active_operations;
    
    void initializeStorageDevices();
};

#endif // STORAGE_MANAGER_HPP
