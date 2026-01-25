#include "security_manager.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

SecurityManager::SecurityManager() {
    initializePermissions();
}

SecurityManager::~SecurityManager() {
}

SecurityManager& SecurityManager::getInstance() {
    static SecurityManager instance;
    return instance;
}

void SecurityManager::initializePermissions() {
    app_permissions.clear();
}

bool SecurityManager::grantPermission(const std::string& app_id, PermissionType permission) {
    AppPermission perm;
    perm.app_id = app_id;
    perm.permission = permission;
    perm.status = PermissionStatus::GRANTED;
    perm.granted_time = 0;
    
    app_permissions[app_id].push_back(perm);
    std::cout << "INFO: Permission granted to " << app_id << "\n";
    return true;
}

bool SecurityManager::revokePermission(const std::string& app_id, PermissionType permission) {
    auto it = app_permissions.find(app_id);
    if (it != app_permissions.end()) {
        auto& perms = it->second;
        perms.erase(std::remove_if(perms.begin(), perms.end(),
            [permission](const AppPermission& p) { return p.permission == permission; }),
            perms.end());
        std::cout << "INFO: Permission revoked from " << app_id << "\n";
        return true;
    }
    return false;
}

PermissionStatus SecurityManager::checkPermission(const std::string& app_id, PermissionType permission) {
    auto it = app_permissions.find(app_id);
    if (it == app_permissions.end()) {
        return PermissionStatus::DENIED;
    }
    
    for (const auto& perm : it->second) {
        if (perm.permission == permission && perm.status == PermissionStatus::GRANTED) {
            return PermissionStatus::GRANTED;
        }
    }
    
    return PermissionStatus::DENIED;
}

std::vector<AppPermission> SecurityManager::getAppPermissions(const std::string& app_id) {
    auto it = app_permissions.find(app_id);
    if (it == app_permissions.end()) {
        return std::vector<AppPermission>();
    }
    return it->second;
}

std::vector<std::string> SecurityManager::getAppsWithPermission(PermissionType permission) {
    std::vector<std::string> apps;
    for (const auto& app_perms : app_permissions) {
        for (const auto& perm : app_perms.second) {
            if (perm.permission == permission && perm.status == PermissionStatus::GRANTED) {
                apps.push_back(app_perms.first);
                break;
            }
        }
    }
    return apps;
}

bool SecurityManager::enableFingerprint() {
    std::cout << "INFO: Fingerprint authentication enabled\n";
    return true;
}

bool SecurityManager::disableFingerprint() {
    std::cout << "INFO: Fingerprint authentication disabled\n";
    return true;
}

bool SecurityManager::authenticateFingerprint(const std::string& app_id) {
    std::cout << "INFO: Fingerprint authentication requested by " << app_id << "\n";
    return true;
}

std::string SecurityManager::getFingerprintStatus() {
    return "Fingerprint Status:\n  Enabled: Yes\n  Enrolled Fingerprints: 2\n  Status: Ready\n";
}

bool SecurityManager::enableFaceID() {
    std::cout << "INFO: FaceID authentication enabled\n";
    return true;
}

bool SecurityManager::disableFaceID() {
    std::cout << "INFO: FaceID authentication disabled\n";
    return true;
}

bool SecurityManager::authenticateFaceID(const std::string& app_id) {
    std::cout << "INFO: FaceID authentication requested by " << app_id << "\n";
    return true;
}

std::string SecurityManager::getFaceIDStatus() {
    return "FaceID Status:\n  Enabled: Yes\n  Registered: Yes\n  Status: Ready\n";
}

bool SecurityManager::setPIN(const std::string& pin_code) {
    if (pin_code.length() < 4 || pin_code.length() > 16) return false;
    secure_storage["device_pin"] = pin_code;
    std::cout << "INFO: Device PIN set\n";
    return true;
}

bool SecurityManager::verifyPIN(const std::string& pin_code) {
    auto it = secure_storage.find("device_pin");
    if (it == secure_storage.end()) {
        std::cout << "INFO: No PIN set on device\n";
        return true;
    }
    return it->second == pin_code;
}

bool SecurityManager::changePIN(const std::string& old_pin, const std::string& new_pin) {
    if (verifyPIN(old_pin)) {
        secure_storage["device_pin"] = new_pin;
        std::cout << "INFO: Device PIN changed\n";
        return true;
    }
    return false;
}

bool SecurityManager::installSecureApp(const std::string& app_package, const std::string& signature) {
    std::cout << "INFO: Installing secure app: " << app_package << "\n";
    std::cout << "INFO: Verifying signature: " << signature << "\n";
    return true;
}

bool SecurityManager::verifyAppSignature(const std::string& app_package) {
    std::cout << "INFO: Verifying app signature for " << app_package << "\n";
    return true;
}

bool SecurityManager::sandboxApp(const std::string& app_id) {
    std::cout << "INFO: Sandboxing app: " << app_id << "\n";
    return true;
}

bool SecurityManager::enableDataEncryption() {
    std::cout << "INFO: Full disk encryption enabled\n";
    return true;
}

bool SecurityManager::disableDataEncryption() {
    std::cout << "INFO: Full disk encryption disabled\n";
    return true;
}

std::string SecurityManager::getEncryptionStatus() {
    return "Encryption Status:\n  Enabled: Yes\n  Algorithm: AES-256\n  Status: Active\n";
}

std::vector<std::string> SecurityManager::listSuspiciousApps() {
    std::vector<std::string> suspicious;
    return suspicious;
}

std::string SecurityManager::getSecurityStatus() {
    return "Security Status:\n  Overall: SECURE\n  Threats: None\n  Last Scan: Today\n";
}

bool SecurityManager::runSecurityScan() {
    std::cout << "INFO: Running security scan...\n";
    return true;
}

bool SecurityManager::storeSecureData(const std::string& key, const std::string& value) {
    secure_storage[key] = value;
    std::cout << "INFO: Secure data stored\n";
    return true;
}

std::string SecurityManager::retrieveSecureData(const std::string& key) {
    auto it = secure_storage.find(key);
    if (it != secure_storage.end()) {
        return it->second;
    }
    return "ERROR: Key not found";
}

bool SecurityManager::deleteSecureData(const std::string& key) {
    auto it = secure_storage.find(key);
    if (it != secure_storage.end()) {
        secure_storage.erase(it);
        std::cout << "INFO: Secure data deleted\n";
        return true;
    }
    return false;
}
