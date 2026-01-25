#include "hardware_manager.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>

HardwareManager::HardwareManager() {
    initializeDefaultDevices();
}

HardwareManager::~HardwareManager() {
    // Cleanup hardware resources
}

HardwareManager& HardwareManager::getInstance() {
    static HardwareManager instance;
    return instance;
}

void HardwareManager::initializeDefaultDevices() {
    devices["camera0"] = createDevice("camera0", "Front Camera", HardwareType::CAMERA);
    devices["camera1"] = createDevice("camera1", "Rear Camera", HardwareType::CAMERA);
    devices["mic0"] = createDevice("mic0", "Microphone", HardwareType::MICROPHONE);
    devices["speaker0"] = createDevice("speaker0", "Speaker", HardwareType::SPEAKER);
    devices["bt0"] = createDevice("bt0", "Bluetooth", HardwareType::BLUETOOTH);
    devices["wifi0"] = createDevice("wifi0", "WiFi", HardwareType::WIFI);
    devices["sensor_accel"] = createDevice("sensor_accel", "Accelerometer", HardwareType::SENSOR);
    devices["sensor_gyro"] = createDevice("sensor_gyro", "Gyroscope", HardwareType::SENSOR);
    devices["sensor_prox"] = createDevice("sensor_prox", "Proximity", HardwareType::SENSOR);
    devices["sensor_light"] = createDevice("sensor_light", "Light Sensor", HardwareType::SENSOR);
    devices["display0"] = createDevice("display0", "Display", HardwareType::DISPLAY);
    devices["touch0"] = createDevice("touch0", "Touchscreen", HardwareType::TOUCH);
    devices["gps0"] = createDevice("gps0", "GPS", HardwareType::GPS);
    devices["battery0"] = createDevice("battery0", "Battery", HardwareType::BATTERY);
    devices["thermal0"] = createDevice("thermal0", "Thermal Sensor", HardwareType::THERMAL);
}

HardwareDevice HardwareManager::createDevice(const std::string& id, const std::string& name, HardwareType type) {
    HardwareDevice device;
    device.device_id = id;
    device.device_name = name;
    device.type = type;
    device.state = DeviceState::DISCONNECTED;
    device.power_consumption_ma = 0;
    device.firmware_version = "1.0.0";
    device.driver_version = "1.0.0";
    device.is_available = true;
    device.last_error = "";
    return device;
}

// Camera Management
bool HardwareManager::startCamera(const std::string& camera_id, int width, int height) {
    auto it = devices.find(camera_id);
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::ACTIVE;
    it->second.power_consumption_ma = 250;
    std::cout << "INFO: Camera " << camera_id << " started (" << width << "x" << height << ")\n";
    return true;
}

bool HardwareManager::stopCamera(const std::string& camera_id) {
    auto it = devices.find(camera_id);
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::INACTIVE;
    it->second.power_consumption_ma = 0;
    std::cout << "INFO: Camera " << camera_id << " stopped\n";
    return true;
}

std::string HardwareManager::getCameraStatus(const std::string& camera_id) {
    auto it = devices.find(camera_id);
    if (it == devices.end()) return "ERROR: Camera not found";
    
    std::ostringstream oss;
    oss << "Camera: " << it->second.device_name << "\n";
    oss << "State: " << (it->second.state == DeviceState::ACTIVE ? "ACTIVE" : "INACTIVE") << "\n";
    oss << "Power: " << it->second.power_consumption_ma << "mA\n";
    return oss.str();
}

std::vector<std::string> HardwareManager::listCameras() {
    std::vector<std::string> cameras;
    for (const auto& device : devices) {
        if (device.second.type == HardwareType::CAMERA) {
            cameras.push_back(device.second.device_id);
        }
    }
    return cameras;
}

bool HardwareManager::setCameraProperty(const std::string& camera_id, const std::string& property, const std::string& value) {
    auto it = devices.find(camera_id);
    if (it == devices.end()) return false;
    
    std::cout << "INFO: Set " << camera_id << " property " << property << " = " << value << "\n";
    return true;
}

// Bluetooth Management
bool HardwareManager::enableBluetooth() {
    auto it = devices.find("bt0");
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::CONNECTED;
    it->second.power_consumption_ma = 50;
    std::cout << "INFO: Bluetooth enabled\n";
    return true;
}

bool HardwareManager::disableBluetooth() {
    auto it = devices.find("bt0");
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::DISCONNECTED;
    it->second.power_consumption_ma = 0;
    std::cout << "INFO: Bluetooth disabled\n";
    return true;
}

std::vector<std::string> HardwareManager::scanBluetoothDevices() {
    std::vector<std::string> devices_found;
    devices_found.push_back("BT_Device_1:AA:BB:CC:DD:EE:FF");
    devices_found.push_back("BT_Device_2:11:22:33:44:55:66");
    devices_found.push_back("BT_Device_3:77:88:99:AA:BB:CC");
    std::cout << "INFO: Bluetooth scan complete, found " << devices_found.size() << " devices\n";
    return devices_found;
}

bool HardwareManager::connectBluetoothDevice(const std::string& device_address) {
    std::cout << "INFO: Connecting to Bluetooth device: " << device_address << "\n";
    return true;
}

bool HardwareManager::disconnectBluetoothDevice(const std::string& device_address) {
    std::cout << "INFO: Disconnecting Bluetooth device: " << device_address << "\n";
    return true;
}

std::string HardwareManager::getBluetoothDeviceInfo(const std::string& device_address) {
    return "Device: " + device_address + "\nRSSI: -65dBm\nBattery: 85%\nStatus: Connected\n";
}

// Sensor Management
bool HardwareManager::enableSensor(const std::string& sensor_type) {
    for (auto& device : devices) {
        if (device.second.type == HardwareType::SENSOR && device.second.device_name.find(sensor_type) != std::string::npos) {
            device.second.state = DeviceState::ACTIVE;
            device.second.power_consumption_ma = 15;
            std::cout << "INFO: Sensor " << sensor_type << " enabled\n";
            return true;
        }
    }
    return false;
}

bool HardwareManager::disableSensor(const std::string& sensor_type) {
    for (auto& device : devices) {
        if (device.second.type == HardwareType::SENSOR && device.second.device_name.find(sensor_type) != std::string::npos) {
            device.second.state = DeviceState::INACTIVE;
            device.second.power_consumption_ma = 0;
            std::cout << "INFO: Sensor " << sensor_type << " disabled\n";
            return true;
        }
    }
    return false;
}

std::string HardwareManager::readSensor(const std::string& sensor_type) {
    std::ostringstream oss;
    oss << "Sensor: " << sensor_type << "\n";
    
    if (sensor_type.find("Accelerometer") != std::string::npos) {
        oss << "X: 0.05g, Y: -0.02g, Z: 9.81g\n";
    } else if (sensor_type.find("Gyroscope") != std::string::npos) {
        oss << "X: 1.2°/s, Y: -0.8°/s, Z: 0.1°/s\n";
    } else if (sensor_type.find("Proximity") != std::string::npos) {
        oss << "Distance: 5.2 cm\n";
    } else if (sensor_type.find("Light") != std::string::npos) {
        oss << "Lux: 450\n";
    }
    
    return oss.str();
}

std::vector<std::string> HardwareManager::listAvailableSensors() {
    std::vector<std::string> sensors;
    for (const auto& device : devices) {
        if (device.second.type == HardwareType::SENSOR) {
            sensors.push_back(device.second.device_id);
        }
    }
    return sensors;
}

// Display Management
bool HardwareManager::setBrightness(int brightness_percent) {
    if (brightness_percent < 0 || brightness_percent > 100) return false;
    
    auto it = devices.find("display0");
    if (it == devices.end()) return false;
    
    std::cout << "INFO: Display brightness set to " << brightness_percent << "%\n";
    return true;
}

int HardwareManager::getBrightness() {
    return 75;
}

bool HardwareManager::setDisplayTimeout(int timeout_seconds) {
    std::cout << "INFO: Display timeout set to " << timeout_seconds << " seconds\n";
    return true;
}

bool HardwareManager::enableAutoRotate(bool enable) {
    std::cout << "INFO: Auto-rotate " << (enable ? "enabled" : "disabled") << "\n";
    return true;
}

// Touch Management
bool HardwareManager::enableTouchInput() {
    auto it = devices.find("touch0");
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::ACTIVE;
    std::cout << "INFO: Touchscreen enabled\n";
    return true;
}

bool HardwareManager::disableTouchInput() {
    auto it = devices.find("touch0");
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::INACTIVE;
    std::cout << "INFO: Touchscreen disabled\n";
    return true;
}

bool HardwareManager::calibrateTouchScreen() {
    std::cout << "INFO: Touchscreen calibration started\n";
    return true;
}

// Device Management
HardwareDevice* HardwareManager::getDeviceInfo(const std::string& device_id) {
    auto it = devices.find(device_id);
    if (it == devices.end()) return nullptr;
    return &it->second;
}

std::vector<HardwareDevice> HardwareManager::getAllDevices() {
    std::vector<HardwareDevice> all_devices;
    for (const auto& device : devices) {
        all_devices.push_back(device.second);
    }
    return all_devices;
}

bool HardwareManager::initializeDevice(const std::string& device_id) {
    auto it = devices.find(device_id);
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::CONNECTED;
    std::cout << "INFO: Device " << device_id << " initialized\n";
    return true;
}

bool HardwareManager::shutdownDevice(const std::string& device_id) {
    auto it = devices.find(device_id);
    if (it == devices.end()) return false;
    
    it->second.state = DeviceState::DISCONNECTED;
    it->second.power_consumption_ma = 0;
    std::cout << "INFO: Device " << device_id << " shutdown\n";
    return true;
}

std::string HardwareManager::executeHardwareCommand(const std::string& device_id, const std::string& command) {
    auto it = devices.find(device_id);
    if (it == devices.end()) return "ERROR: Device not found";
    
    return "OK: Command '" + command + "' executed on " + device_id + "\n";
}

bool HardwareManager::setDeviceProperty(const std::string& device_id, const std::string& property, const std::string& value) {
    auto it = devices.find(device_id);
    if (it == devices.end()) return false;
    
    std::cout << "INFO: Device " << device_id << " property " << property << " = " << value << "\n";
    return true;
}

std::string HardwareManager::getDeviceProperty(const std::string& device_id, const std::string& property) {
    auto it = devices.find(device_id);
    if (it == devices.end()) return "ERROR: Device not found";
    
    if (property == "state") {
        return std::to_string(static_cast<int>(it->second.state));
    } else if (property == "power") {
        return std::to_string(it->second.power_consumption_ma) + "mA";
    }
    
    return "Unknown property";
}
