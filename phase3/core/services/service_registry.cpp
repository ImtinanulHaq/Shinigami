#include "../../core/services/service_registry.hpp"
#include "../../core/services/microservice.hpp"

namespace phase3::core::services {

static ServiceRegistry* g_service_registry = nullptr;

ServiceRegistry& ServiceRegistry::getInstance() {
    if (!g_service_registry) {
        g_service_registry = new ServiceRegistry();
    }
    return *g_service_registry;
}

void ServiceRegistry::registerService(const std::string& service_name, 
                                      std::shared_ptr<MicroService> service) {
    if (!service) {
        return;
    }
    
    services[service_name] = service;
    
    // Log registration
    // LOG(INFO) << "Service registered: " << service_name;
}

void ServiceRegistry::unregisterService(const std::string& service_name) {
    auto it = services.find(service_name);
    if (it != services.end()) {
        // Note: Service shutdown would be handled elsewhere
        // if (it->second && it->second->getState() == ServiceState::RUNNING) {
        //     it->second->stop();
        // }
        
        services.erase(it);
        // LOG(INFO) << "Service unregistered: " << service_name;
    }
}

std::shared_ptr<MicroService> ServiceRegistry::getService(const std::string& service_name) const {
    auto it = services.find(service_name);
    if (it != services.end()) {
        return it->second;
    }
    return nullptr;
}

bool ServiceRegistry::hasService(const std::string& service_name) const {
    return services.find(service_name) != services.end();
}

std::vector<std::string> ServiceRegistry::getServiceNames() const {
    std::vector<std::string> names;
    names.reserve(services.size());
    
    for (const auto& pair : services) {
        names.push_back(pair.first);
    }
    
    return names;
}

size_t ServiceRegistry::getServiceCount() const {
    return services.size();
}

void ServiceRegistry::clear() {
    // Note: Service shutdown would be handled elsewhere
    // for (auto& pair : services) {
    //     if (pair.second && pair.second->getState() == ServiceState::RUNNING) {
    //         pair.second->stop();
    //     }
    // }
    
    services.clear();
}

std::vector<std::shared_ptr<MicroService>> 
ServiceRegistry::findServicesByCapability(const std::string& /*capability*/) const {
    std::vector<std::shared_ptr<MicroService>> result;
    
    // Note: Full capability checking would require complete MicroService definition
    // For now, return all services
    for (const auto& pair : services) {
        if (pair.second) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::string> ServiceRegistry::getUnhealthyServices() const {
    std::vector<std::string> unhealthy;
    
    // Note: Health checking would require complete MicroService definition
    // For now, return empty (all services are healthy)
    return unhealthy;
}

}  // namespace phase3::core::services
