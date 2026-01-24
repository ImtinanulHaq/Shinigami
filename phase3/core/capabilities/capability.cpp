#include "../capabilities/capability.hpp"
#include <algorithm>

static CapabilityManager* g_cap_manager = nullptr;

CapabilityManager& CapabilityManager::getInstance() {
    if (!g_cap_manager) {
        g_cap_manager = new CapabilityManager();
    }
    return *g_cap_manager;
}

CapabilityManager::CapabilityManager() {}

std::shared_ptr<CapabilityToken> CapabilityManager::grant(
    const std::string& holder_id, 
    CapabilityType capability,
    bool /*persistent*/) {
    
    std::lock_guard<std::mutex> lock(cap_mutex);
    
    // Add to capability set
    capabilities[holder_id].insert(capability);
    
    // Create token
    auto token = std::make_shared<CapabilityToken>(capability, holder_id);
    tokens[holder_id].push_back(token);
    
    return token;
}

bool CapabilityManager::revoke(const std::string& holder_id, CapabilityType capability) {
    std::lock_guard<std::mutex> lock(cap_mutex);
    
    auto it = capabilities.find(holder_id);
    if (it != capabilities.end()) {
        it->second.erase(capability);
        
        // Revoke associated tokens
        if (tokens.find(holder_id) != tokens.end()) {
            for (auto& token : tokens[holder_id]) {
                if (token->getCapability() == capability) {
                    token->revoke();
                }
            }
        }
        
        return true;
    }
    
    return false;
}

bool CapabilityManager::revokeAll(const std::string& holder_id) {
    std::lock_guard<std::mutex> lock(cap_mutex);
    
    capabilities.erase(holder_id);
    
    if (tokens.find(holder_id) != tokens.end()) {
        for (auto& token : tokens[holder_id]) {
            token->revoke();
        }
        tokens.erase(holder_id);
    }
    
    constraints.erase(holder_id);
    
    return true;
}

bool CapabilityManager::hasCapability(const std::string& holder_id, CapabilityType capability) {
    std::lock_guard<std::mutex> lock(cap_mutex);
    
    auto it = capabilities.find(holder_id);
    if (it != capabilities.end()) {
        return it->second.count(capability) > 0;
    }
    
    return false;  // Fail-safe: deny by default
}

bool CapabilityManager::verify(const std::shared_ptr<CapabilityToken>& token) {
    if (!token) return false;
    return token->isValid();
}

std::vector<CapabilityType> CapabilityManager::getCapabilities(const std::string& holder_id) {
    std::lock_guard<std::mutex> lock(cap_mutex);
    
    std::vector<CapabilityType> result;
    
    auto it = capabilities.find(holder_id);
    if (it != capabilities.end()) {
        for (const auto& cap : it->second) {
            result.push_back(cap);
        }
    }
    
    return result;
}

std::string CapabilityManager::getCapabilityName(CapabilityType capability) const {
    switch (capability) {
        case CapabilityType::SYS_SHUTDOWN: return "System Shutdown";
        case CapabilityType::SYS_REBOOT: return "System Reboot";
        case CapabilityType::GPU_RENDER: return "GPU Rendering";
        case CapabilityType::GUI_CREATE_WINDOW: return "Create Windows";
        case CapabilityType::FS_READ: return "Read Filesystem";
        case CapabilityType::FS_WRITE: return "Write Filesystem";
        case CapabilityType::NET_SOCKET_CREATE: return "Create Network Sockets";
        case CapabilityType::AUDIO_PLAYBACK: return "Audio Playback";
        case CapabilityType::INPUT_KEYBOARD: return "Keyboard Input";
        case CapabilityType::IPC_MESSAGE: return "IPC Messaging";
        default: return "Unknown Capability";
    }
}

std::string CapabilityManager::getCapabilityDescription(CapabilityType capability) const {
    switch (capability) {
        case CapabilityType::SYS_SHUTDOWN: 
            return "Allows the application to shutdown the system";
        case CapabilityType::GPU_RENDER:
            return "Allows GPU rendering and graphics operations";
        case CapabilityType::GUI_CREATE_WINDOW:
            return "Allows creation and management of GUI windows";
        case CapabilityType::FS_READ:
            return "Allows reading files from the filesystem";
        case CapabilityType::FS_WRITE:
            return "Allows writing files to the filesystem";
        case CapabilityType::NET_SOCKET_CREATE:
            return "Allows creating network sockets";
        default: return "";
    }
}

void CapabilityManager::setConstraint(const std::string& holder_id, 
                                      CapabilityType capability, 
                                      const std::string& constraint_name,
                                      int64_t value) {
    std::lock_guard<std::mutex> lock(cap_mutex);
    constraints[holder_id][capability][constraint_name] = value;
}

int64_t CapabilityManager::getConstraint(const std::string& holder_id,
                                        CapabilityType capability,
                                        const std::string& constraint_name) const {
    std::lock_guard<std::mutex> lock(cap_mutex);
    
    auto it1 = constraints.find(holder_id);
    if (it1 == constraints.end()) return 0;
    
    auto it2 = it1->second.find(capability);
    if (it2 == it1->second.end()) return 0;
    
    auto it3 = it2->second.find(constraint_name);
    if (it3 == it2->second.end()) return 0;
    
    return it3->second;
}
