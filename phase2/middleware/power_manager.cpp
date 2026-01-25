#include "power_manager.hpp"
#include <iostream>
#include <sstream>

PowerManager::PowerManager() {
    current_power_mode = PowerMode::NORMAL;
    battery_level = 85;
    is_charging = false;
    device_temperature_celsius = 42;
}

PowerManager::~PowerManager() {
}

PowerManager& PowerManager::getInstance() {
    static PowerManager instance;
    return instance;
}

int PowerManager::getBatteryLevel() {
    return battery_level;
}

bool PowerManager::isBatteryCharging() {
    return is_charging;
}

std::string PowerManager::getBatteryHealth() {
    if (battery_level >= 80) return "Good";
    if (battery_level >= 50) return "Fair";
    if (battery_level >= 20) return "Poor";
    return "Critical";
}

long PowerManager::getBatteryCapacity() {
    return 5000;
}

int PowerManager::getChargingTimeRemaining() {
    return is_charging ? 45 : -1;
}

int PowerManager::getDischargingTimeRemaining() {
    return !is_charging ? 360 : -1;
}

bool PowerManager::enableBatteryOptimization() {
    std::cout << "INFO: Battery optimization enabled\n";
    return true;
}

bool PowerManager::setPowerMode(PowerMode mode) {
    current_power_mode = mode;
    std::cout << "INFO: Power mode changed to " << getPowerModeString() << "\n";
    return true;
}

PowerMode PowerManager::getCurrentPowerMode() {
    return current_power_mode;
}

std::string PowerManager::getPowerModeString() {
    switch (current_power_mode) {
        case PowerMode::NORMAL: return "NORMAL";
        case PowerMode::POWER_SAVING: return "POWER_SAVING";
        case PowerMode::ULTRA_POWER_SAVING: return "ULTRA_POWER_SAVING";
        case PowerMode::PERFORMANCE: return "PERFORMANCE";
        default: return "UNKNOWN";
    }
}

bool PowerManager::setCPUFrequency(int frequency_mhz) {
    std::cout << "INFO: CPU frequency set to " << frequency_mhz << " MHz\n";
    return true;
}

int PowerManager::getCurrentCPUFrequency() {
    return 2400;
}

bool PowerManager::enableCPUThrottling(bool enable) {
    std::cout << "INFO: CPU throttling " << (enable ? "enabled" : "disabled") << "\n";
    return true;
}

int PowerManager::getDeviceTemperature() {
    return device_temperature_celsius;
}

bool PowerManager::enableThermalThrottling() {
    std::cout << "INFO: Thermal throttling enabled\n";
    return true;
}

bool PowerManager::setScreenTimeout(int timeout_seconds) {
    std::cout << "INFO: Screen timeout set to " << timeout_seconds << " seconds\n";
    return true;
}

bool PowerManager::keepScreenOn(bool keep_on) {
    std::cout << "INFO: Keep screen on: " << (keep_on ? "Yes" : "No") << "\n";
    return true;
}

bool PowerManager::setScreenBrightness(int brightness_percent) {
    if (brightness_percent < 0 || brightness_percent > 100) return false;
    std::cout << "INFO: Screen brightness set to " << brightness_percent << "%\n";
    return true;
}

int PowerManager::getScreenBrightness() {
    return 75;
}

bool PowerManager::enableLowBatteryWarning(int threshold_percent) {
    std::cout << "INFO: Low battery warning enabled at " << threshold_percent << "%\n";
    return true;
}

bool PowerManager::enableCriticalBatteryShutdown(int threshold_percent) {
    std::cout << "INFO: Critical battery shutdown enabled at " << threshold_percent << "%\n";
    return true;
}

std::string PowerManager::getPowerStatus() {
    std::ostringstream oss;
    oss << "Power Status:\n";
    oss << "  Battery Level: " << battery_level << "%\n";
    oss << "  Charging: " << (is_charging ? "Yes" : "No") << "\n";
    oss << "  Temperature: " << device_temperature_celsius << "°C\n";
    oss << "  Power Mode: " << getPowerModeString() << "\n";
    oss << "  CPU Frequency: " << getCurrentCPUFrequency() << " MHz\n";
    return oss.str();
}

bool PowerManager::enableSleep() {
    std::cout << "INFO: Sleep mode enabled\n";
    return true;
}

bool PowerManager::disableSleep() {
    std::cout << "INFO: Sleep mode disabled\n";
    return true;
}

bool PowerManager::wakeDevice() {
    std::cout << "INFO: Device woken up\n";
    return true;
}
