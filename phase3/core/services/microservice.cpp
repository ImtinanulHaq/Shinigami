#include "../services/microservice.hpp"
#include <algorithm>
#include <sstream>

std::string MicroService::getStateString() const {
    switch (current_state) {
        case ServiceState::CREATED: return "CREATED";
        case ServiceState::INITIALIZING: return "INITIALIZING";
        case ServiceState::READY: return "READY";
        case ServiceState::RUNNING: return "RUNNING";
        case ServiceState::PAUSED: return "PAUSED";
        case ServiceState::STOPPING: return "STOPPING";
        case ServiceState::STOPPED: return "STOPPED";
        case ServiceState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

uint64_t MicroService::onEvent(EventType event_type, EventHandler handler) {
    return EventBus::getInstance().subscribeToSource(event_type, service_id, handler);
}

bool MicroService::offEvent(uint64_t subscription_id) {
    return EventBus::getInstance().unsubscribe(subscription_id);
}

void MicroService::publishEvent(EventType event_type, const std::string& description) {
    Event event(event_type, service_id);
    if (!description.empty()) {
        event.payload["message"] = description;
    }
    publishEvent(event);
}

void MicroService::publishEvent(const Event& event) {
    EventBus::getInstance().publish(event);
    events_published++;
}

void MicroService::registerMethod(const std::string& method_name, ServiceMethod method) {
    methods[method_name] = method;
}

std::string MicroService::callMethod(const std::string& method_name, 
                                    const std::map<std::string, std::string>& params) {
    auto it = methods.find(method_name);
    if (it != methods.end()) {
        methods_called++;
        return it->second(params);
    }
    
    return "ERROR: Method not found: " + method_name;
}

std::vector<std::string> MicroService::getAvailableMethods() const {
    std::vector<std::string> result;
    for (const auto& pair : methods) {
        result.push_back(pair.first);
    }
    return result;
}

bool MicroService::requestCapability(CapabilityType capability) {
    auto token = CapabilityManager::getInstance().grant(service_id, capability, true);
    if (token && token->isValid()) {
        granted_capabilities.push_back(capability);
        return true;
    }
    return false;
}

bool MicroService::hasCapability(CapabilityType capability) const {
    return CapabilityManager::getInstance().hasCapability(service_id, capability);
}

std::vector<CapabilityType> MicroService::getCapabilities() const {
    return CapabilityManager::getInstance().getCapabilities(service_id);
}

void MicroService::setConfig(const std::string& key, const std::string& value) {
    config[key] = value;
}

std::string MicroService::getConfig(const std::string& key, const std::string& default_value) const {
    auto it = config.find(key);
    return (it != config.end()) ? it->second : default_value;
}

bool MicroService::sendMessageToService(const std::string& /*target_service_id*/, 
                                       const std::string& /*message*/) {
    messages_sent++;
    // Implementation would involve service registry lookup
    return true;
}

MicroService::ServiceMetrics MicroService::getMetrics() const {
    return {
        events_published,
        methods_called,
        messages_sent,
        start_time
    };
}
