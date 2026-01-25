#include "storage_manager.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

StorageManager::StorageManager() {
    initializeStorageDevices();
}

StorageManager::~StorageManager() {
}

StorageManager& StorageManager::getInstance() {
    static StorageManager instance;
    return instance;
}

void StorageManager::initializeStorageDevices() {
    StorageDevice internal;
    internal.device_id = "mmcblk0";
    internal.mount_point = "/data";
    internal.filesystem_type = "ext4";
    internal.total_size_bytes = 64L * 1024 * 1024 * 1024;
    internal.used_size_bytes = 35L * 1024 * 1024 * 1024;
    internal.free_size_bytes = internal.total_size_bytes - internal.used_size_bytes;
    internal.is_mounted = true;
    internal.is_removable = false;
    internal.read_speed_mbps = 300;
    internal.write_speed_mbps = 250;
    storage_devices["mmcblk0"] = internal;
    
    StorageDevice external;
    external.device_id = "sda1";
    external.mount_point = "/external";
    external.filesystem_type = "ext4";
    external.total_size_bytes = 32L * 1024 * 1024 * 1024;
    external.used_size_bytes = 15L * 1024 * 1024 * 1024;
    external.free_size_bytes = external.total_size_bytes - external.used_size_bytes;
    external.is_mounted = false;
    external.is_removable = true;
    external.read_speed_mbps = 150;
    external.write_speed_mbps = 120;
    storage_devices["sda1"] = external;
}

bool StorageManager::mountDevice(const std::string& device_id, const std::string& mount_point) {
    auto it = storage_devices.find(device_id);
    if (it == storage_devices.end()) return false;
    
    it->second.mount_point = mount_point;
    it->second.is_mounted = true;
    std::cout << "INFO: Device " << device_id << " mounted at " << mount_point << "\n";
    return true;
}

bool StorageManager::unmountDevice(const std::string& device_id) {
    auto it = storage_devices.find(device_id);
    if (it == storage_devices.end()) return false;
    
    it->second.is_mounted = false;
    std::cout << "INFO: Device " << device_id << " unmounted\n";
    return true;
}

bool StorageManager::formatDevice(const std::string& device_id, const std::string& filesystem_type) {
    auto it = storage_devices.find(device_id);
    if (it == storage_devices.end()) return false;
    
    it->second.filesystem_type = filesystem_type;
    it->second.used_size_bytes = 0;
    it->second.free_size_bytes = it->second.total_size_bytes;
    std::cout << "INFO: Device " << device_id << " formatted with " << filesystem_type << "\n";
    return true;
}

std::vector<StorageDevice> StorageManager::listStorageDevices() {
    std::vector<StorageDevice> devices;
    for (const auto& device : storage_devices) {
        devices.push_back(device.second);
    }
    return devices;
}

StorageDevice* StorageManager::getDeviceInfo(const std::string& device_id) {
    auto it = storage_devices.find(device_id);
    if (it == storage_devices.end()) return nullptr;
    return &it->second;
}

bool StorageManager::copyFile(const std::string& source, const std::string& destination) {
    std::cout << "INFO: Copying file from " << source << " to " << destination << "\n";
    return true;
}

bool StorageManager::moveFile(const std::string& source, const std::string& destination) {
    std::cout << "INFO: Moving file from " << source << " to " << destination << "\n";
    return true;
}

bool StorageManager::deleteFile(const std::string& file_path) {
    std::cout << "INFO: Deleting file: " << file_path << "\n";
    return true;
}

bool StorageManager::deleteDirectory(const std::string& dir_path) {
    std::cout << "INFO: Deleting directory: " << dir_path << "\n";
    return true;
}

std::vector<std::string> StorageManager::listDirectory(const std::string& dir_path) {
    std::vector<std::string> files;
    files.push_back("file1.txt");
    files.push_back("file2.bin");
    files.push_back("file3.log");
    files.push_back("subdir/");
    return files;
}

long StorageManager::getFileSize(const std::string& file_path) {
    return 1024 * 1024;
}

bool StorageManager::createBackup(const std::string& backup_source, const std::string& backup_destination) {
    std::cout << "INFO: Creating backup from " << backup_source << " to " << backup_destination << "\n";
    return true;
}

bool StorageManager::restoreBackup(const std::string& backup_path, const std::string& restore_destination) {
    std::cout << "INFO: Restoring backup from " << backup_path << " to " << restore_destination << "\n";
    return true;
}

std::vector<std::string> StorageManager::listBackups() {
    std::vector<std::string> backups;
    backups.push_back("/backups/backup_2026_01_25.tar.gz");
    backups.push_back("/backups/backup_2026_01_24.tar.gz");
    backups.push_back("/backups/backup_2026_01_23.tar.gz");
    return backups;
}

long StorageManager::getInternalStorageUsed() {
    auto it = storage_devices.find("mmcblk0");
    if (it == storage_devices.end()) return 0;
    return it->second.used_size_bytes;
}

long StorageManager::getInternalStorageTotal() {
    auto it = storage_devices.find("mmcblk0");
    if (it == storage_devices.end()) return 0;
    return it->second.total_size_bytes;
}

long StorageManager::getExternalStorageUsed() {
    auto it = storage_devices.find("sda1");
    if (it == storage_devices.end()) return 0;
    return it->second.used_size_bytes;
}

long StorageManager::getExternalStorageTotal() {
    auto it = storage_devices.find("sda1");
    if (it == storage_devices.end()) return 0;
    return it->second.total_size_bytes;
}

long StorageManager::getStorageUsagePercent() {
    long total_used = getInternalStorageUsed() + getExternalStorageUsed();
    long total_size = getInternalStorageTotal() + getExternalStorageTotal();
    return (total_used * 100) / total_size;
}

bool StorageManager::enableStorageLowWarning(long threshold_bytes) {
    std::cout << "INFO: Storage low warning enabled at " << threshold_bytes << " bytes\n";
    return true;
}

bool StorageManager::clearCache(const std::string& app_id) {
    std::cout << "INFO: Clearing cache for " << app_id << "\n";
    return true;
}

long StorageManager::getCacheSize(const std::string& app_id) {
    return 10 * 1024 * 1024;
}

bool StorageManager::clearAllCache() {
    std::cout << "INFO: Clearing all application caches\n";
    return true;
}

bool StorageManager::indexFiles(const std::string& directory) {
    std::cout << "INFO: Indexing files in " << directory << "\n";
    return true;
}

std::vector<std::string> StorageManager::searchFiles(const std::string& query) {
    std::vector<std::string> results;
    results.push_back("/data/file_" + query + "_1.txt");
    results.push_back("/data/file_" + query + "_2.txt");
    results.push_back("/external/file_" + query + "_3.bin");
    return results;
}
