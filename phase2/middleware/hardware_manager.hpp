#ifndef HARDWARE_MANAGER_HPP
#define HARDWARE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>

// ============================================================================
// Hardware Definitions
// ============================================================================

enum class HardwareType {
    CAMERA,
    MICROPHONE,
    SPEAKER,
    BLUETOOTH,
    WIFI,
    SENSOR,
    DISPLAY,
    TOUCH,
    GPS,
    BATTERY,
    THERMAL,
    USB,
    NFC
};

enum class DeviceState {
    DISCONNECTED,
    CONNECTED,
    ACTIVE,
    INACTIVE,
    ERROR
};

struct HardwareDevice {
    std::string device_id;
    std::string device_name;
    HardwareType type;
    DeviceState state;
    int power_consumption_ma;
    std::string firmware_version;
    std::string driver_version;
    bool is_available;
    std::string last_error;
};

// ============================================================================
// Hardware Manager Class
// ============================================================================
class HardwareManager {
public:
    static HardwareManager& getInstance();
    
    // Camera Management
    bool startCamera(const std::string& camera_id, int width, int height);
    bool stopCamera(const std::string& camera_id);
    std::string getCameraStatus(const std::string& camera_id);
    std::vector<std::string> listCameras();
    bool setCameraProperty(const std::string& camera_id, const std::string& property, const std::string& value);
    
    // Bluetooth Management
    bool enableBluetooth();
    bool disableBluetooth();
    std::vector<std::string> scanBluetoothDevices();
    bool connectBluetoothDevice(const std::string& device_address);
    bool disconnectBluetoothDevice(const std::string& device_address);
    std::string getBluetoothDeviceInfo(const std::string& device_address);
    
    // Sensor Management
    bool enableSensor(const std::string& sensor_type);
    bool disableSensor(const std::string& sensor_type);
    std::string readSensor(const std::string& sensor_type);
    std::vector<std::string> listAvailableSensors();
    
    // Display Management
    bool setBrightness(int brightness_percent);
    int getBrightness();
    bool setDisplayTimeout(int timeout_seconds);
    bool enableAutoRotate(bool enable);
    
    // Touch Management
    bool enableTouchInput();
    bool disableTouchInput();
    bool calibrateTouchScreen();
    
    // Device Management
    HardwareDevice* getDeviceInfo(const std::string& device_id);
    std::vector<HardwareDevice> getAllDevices();
    bool initializeDevice(const std::string& device_id);
    bool shutdownDevice(const std::string& device_id);
    
    // Generic Hardware Operations
    std::string executeHardwareCommand(const std::string& device_id, const std::string& command);
    bool setDeviceProperty(const std::string& device_id, const std::string& property, const std::string& value);
    std::string getDeviceProperty(const std::string& device_id, const std::string& property);
    
private:
    HardwareManager();
    ~HardwareManager();
    
    std::map<std::string, HardwareDevice> devices;
    
    void initializeDefaultDevices();
    HardwareDevice createDevice(const std::string& id, const std::string& name, HardwareType type);
};

#endif // HARDWARE_MANAGER_HPP
