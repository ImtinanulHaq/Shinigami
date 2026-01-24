#ifndef CAPABILITY_HPP
#define CAPABILITY_HPP

#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>

// ============================================================================
// Capability Framework - Capability-based access control (like Chromium)
// ============================================================================

// Capability types - what can be accessed/done
enum class CapabilityType : uint32_t {
    // System Capabilities
    SYS_SHUTDOWN = 0x0001,
    SYS_REBOOT = 0x0002,
    SYS_TIME_SYNC = 0x0003,
    
    // GPU/Rendering Capabilities
    GPU_RENDER = 0x0100,
    GPU_TEXTURE_CREATE = 0x0101,
    GPU_BUFFER_CREATE = 0x0102,
    
    // GUI/Window Capabilities
    GUI_CREATE_WINDOW = 0x0200,
    GUI_CREATE_DIALOG = 0x0201,
    GUI_FULLSCREEN = 0x0202,
    GUI_SYSTEM_TRAY = 0x0203,
    
    // Filesystem Capabilities
    FS_READ = 0x0300,
    FS_WRITE = 0x0301,
    FS_DELETE = 0x0302,
    FS_EXECUTE = 0x0303,
    FS_READ_SYSTEM = 0x0304,
    FS_WRITE_SYSTEM = 0x0305,
    
    // Network Capabilities
    NET_SOCKET_CREATE = 0x0400,
    NET_CONNECT = 0x0401,
    NET_BIND = 0x0402,
    NET_LISTEN = 0x0403,
    
    // Audio Capabilities
    AUDIO_PLAYBACK = 0x0500,
    AUDIO_RECORD = 0x0501,
    
    // Input Capabilities
    INPUT_KEYBOARD = 0x0600,
    INPUT_MOUSE = 0x0601,
    INPUT_TOUCH = 0x0602,
    INPUT_SENSOR = 0x0603,
    
    // IPC Capabilities
    IPC_MESSAGE = 0x0700,
    IPC_EVENT_BUS = 0x0701,
    IPC_SERVICE_CALL = 0x0702,
    
    // Permission Check Capabilities
    PERM_READ = 0x0800,
    PERM_GRANT = 0x0801,
    
    // Camera/Media Capabilities
    MEDIA_CAMERA = 0x0900,
    MEDIA_MICROPHONE = 0x0901,
    
    // Debug Capabilities
    DEBUG_TRACE = 0x0A00,
    DEBUG_PROFILE = 0x0A01,
    DEBUG_BREAKPOINT = 0x0A02
};

// Capability token - represents a granted capability
class CapabilityToken {
public:
    CapabilityToken() = default;
    
    CapabilityToken(CapabilityType cap, const std::string& holder_id)
        : capability(cap), holder_id(holder_id), is_valid(true) {}
    
    CapabilityType getCapability() const { return capability; }
    std::string getHolderId() const { return holder_id; }
    bool isValid() const { return is_valid; }
    void revoke() { is_valid = false; }
    
    // Check if this token matches a requested capability
    bool matches(CapabilityType requested) const {
        return is_valid && capability == requested;
    }

private:
    CapabilityType capability = static_cast<CapabilityType>(0);
    std::string holder_id;
    bool is_valid = false;
};

// ============================================================================
// Capability Manager - Grant and verify capabilities
// ============================================================================

class CapabilityManager {
public:
    // Singleton pattern
    static CapabilityManager& getInstance();
    
    CapabilityManager(const CapabilityManager&) = delete;
    CapabilityManager& operator=(const CapabilityManager&) = delete;
    
    // ========================================================================
    // Capability Grant/Revoke
    // ========================================================================
    
    // Grant a capability to a service/app
    // Returns token for verification
    std::shared_ptr<CapabilityToken> grant(
        const std::string& holder_id, 
        CapabilityType capability,
        bool persistent = true);  // persist = save in app manifest
    
    // Revoke specific capability
    bool revoke(const std::string& holder_id, CapabilityType capability);
    
    // Revoke all capabilities for a holder
    bool revokeAll(const std::string& holder_id);
    
    // ========================================================================
    // Capability Verification
    // ========================================================================
    
    // Check if holder has capability (using token)
    bool hasCapability(const std::string& holder_id, CapabilityType capability);
    
    // Verify a specific token
    bool verify(const std::shared_ptr<CapabilityToken>& token);
    
    // ========================================================================
    // Capability Queries
    // ========================================================================
    
    // Get all capabilities for a holder
    std::vector<CapabilityType> getCapabilities(const std::string& holder_id);
    
    // Get capability name for display
    std::string getCapabilityName(CapabilityType capability) const;
    
    // Get capability description
    std::string getCapabilityDescription(CapabilityType capability) const;
    
    // ========================================================================
    // Capability Constraints
    // ========================================================================
    
    // Set resource limit for a capability (e.g., max file size for FS_WRITE)
    void setConstraint(const std::string& holder_id, 
                       CapabilityType capability, 
                       const std::string& constraint_name,
                       int64_t value);
    
    // Get constraint value
    int64_t getConstraint(const std::string& holder_id,
                         CapabilityType capability,
                         const std::string& constraint_name) const;

private:
    // Private constructor
    CapabilityManager();
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    // Capability store: holder_id -> list of capabilities
    std::map<std::string, std::set<CapabilityType>> capabilities;
    std::map<std::string, std::vector<std::shared_ptr<CapabilityToken>>> tokens;
    
    // Constraints: holder_id -> capability -> constraint_name -> value
    std::map<std::string, std::map<CapabilityType, std::map<std::string, int64_t>>> constraints;
    
    // Thread safety
    mutable std::mutex cap_mutex;
    
    // Fail-safe defaults: by default, grant NO capabilities
    // Apps must explicitly request and be granted each capability
};

#endif // CAPABILITY_HPP
