#ifndef SECURITY_MANAGER_HPP
#define SECURITY_MANAGER_HPP

#include <string>
#include <vector>
#include <map>

enum class PermissionType {
    CAMERA,
    MICROPHONE,
    LOCATION,
    CONTACTS,
    FILES,
    CALENDAR,
    SMS,
    CALL_LOG,
    BLUETOOTH,
    WIFI,
    STORAGE
};

enum class PermissionStatus {
    GRANTED,
    DENIED,
    PENDING
};

struct AppPermission {
    std::string app_id;
    PermissionType permission;
    PermissionStatus status;
    long granted_time;
};

// ============================================================================
// Security Manager Class
// ============================================================================
class SecurityManager {
public:
    static SecurityManager& getInstance();
    
    // Permission Management
    bool grantPermission(const std::string& app_id, PermissionType permission);
    bool revokePermission(const std::string& app_id, PermissionType permission);
    PermissionStatus checkPermission(const std::string& app_id, PermissionType permission);
    std::vector<AppPermission> getAppPermissions(const std::string& app_id);
    std::vector<std::string> getAppsWithPermission(PermissionType permission);
    
    // Biometric Authentication
    bool enableFingerprint();
    bool disableFingerprint();
    bool authenticateFingerprint(const std::string& app_id);
    std::string getFingerprintStatus();
    
    bool enableFaceID();
    bool disableFaceID();
    bool authenticateFaceID(const std::string& app_id);
    std::string getFaceIDStatus();
    
    // PIN/Password Management
    bool setPIN(const std::string& pin_code);
    bool verifyPIN(const std::string& pin_code);
    bool changePIN(const std::string& old_pin, const std::string& new_pin);
    
    // App Security
    bool installSecureApp(const std::string& app_package, const std::string& signature);
    bool verifyAppSignature(const std::string& app_package);
    bool sandboxApp(const std::string& app_id);
    
    // Data Encryption
    bool enableDataEncryption();
    bool disableDataEncryption();
    std::string getEncryptionStatus();
    
    // Security Monitoring
    std::vector<std::string> listSuspiciousApps();
    std::string getSecurityStatus();
    bool runSecurityScan();
    
    // Secure Storage
    bool storeSecureData(const std::string& key, const std::string& value);
    std::string retrieveSecureData(const std::string& key);
    bool deleteSecureData(const std::string& key);
    
private:
    SecurityManager();
    ~SecurityManager();
    
    std::map<std::string, std::vector<AppPermission>> app_permissions;
    std::map<std::string, std::string> secure_storage;
    
    void initializePermissions();
};

#endif // SECURITY_MANAGER_HPP
