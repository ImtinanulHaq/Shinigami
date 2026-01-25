#ifndef POWER_MANAGER_HPP
#define POWER_MANAGER_HPP

#include <string>

enum class PowerMode {
    NORMAL,
    POWER_SAVING,
    ULTRA_POWER_SAVING,
    PERFORMANCE
};

// ============================================================================
// Power Manager Class
// ============================================================================
class PowerManager {
public:
    static PowerManager& getInstance();
    
    // Battery Management
    int getBatteryLevel();
    bool isBatteryCharging();
    std::string getBatteryHealth();
    long getBatteryCapacity();
    int getChargingTimeRemaining();
    int getDischargingTimeRemaining();
    bool enableBatteryOptimization();
    
    // Power Mode Control
    bool setPowerMode(PowerMode mode);
    PowerMode getCurrentPowerMode();
    std::string getPowerModeString();
    
    // CPU & Thermal Management
    bool setCPUFrequency(int frequency_mhz);
    int getCurrentCPUFrequency();
    bool enableCPUThrottling(bool enable);
    int getDeviceTemperature();
    bool enableThermalThrottling();
    
    // Screen Power Management
    bool setScreenTimeout(int timeout_seconds);
    bool keepScreenOn(bool keep_on);
    bool setScreenBrightness(int brightness_percent);
    int getScreenBrightness();
    
    // Power Events & Monitoring
    bool enableLowBatteryWarning(int threshold_percent);
    bool enableCriticalBatteryShutdown(int threshold_percent);
    std::string getPowerStatus();
    
    // Device Sleep/Wake
    bool enableSleep();
    bool disableSleep();
    bool wakeDevice();
    
private:
    PowerManager();
    ~PowerManager();
    
    PowerMode current_power_mode;
    int battery_level;
    bool is_charging;
    int device_temperature_celsius;
};

#endif // POWER_MANAGER_HPP
