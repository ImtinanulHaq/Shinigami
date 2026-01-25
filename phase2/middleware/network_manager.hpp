#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <string>
#include <vector>
#include <map>

enum class NetworkType {
    WIFI,
    CELLULAR,
    BLUETOOTH,
    ETHERNET,
    VPN,
    NFC
};

enum class NetworkState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERROR
};

struct NetworkInterface {
    std::string interface_id;
    std::string interface_name;
    NetworkType type;
    NetworkState state;
    std::string ip_address;
    std::string mac_address;
    long signal_strength;
    long data_in_bytes;
    long data_out_bytes;
    bool is_active;
};

// ============================================================================
// Network Manager Class
// ============================================================================
class NetworkManager {
public:
    static NetworkManager& getInstance();
    
    // WiFi Management
    bool enableWiFi();
    bool disableWiFi();
    std::vector<std::string> scanWiFiNetworks();
    bool connectToWiFi(const std::string& ssid, const std::string& password);
    bool disconnectFromWiFi();
    std::string getWiFiStatus();
    
    // Cellular Management
    bool enableCellular();
    bool disableCellular();
    std::string getSignalStrength();
    bool switchToMobileData(const std::string& apn);
    std::string getTelephonyStatus();
    
    // Bluetooth Network
    bool enableBluetoothNetwork();
    bool disableBluetoothNetwork();
    bool connectBluetoothNetwork(const std::string& device_address);
    
    // VPN Management
    bool enableVPN(const std::string& vpn_profile);
    bool disableVPN();
    std::string getVPNStatus();
    bool configureVPN(const std::string& config_json);
    
    // Network Monitoring
    NetworkInterface* getInterfaceInfo(const std::string& interface_id);
    std::vector<NetworkInterface> getAllInterfaces();
    long getTotalDataUsage();
    long getDataUsageToday();
    
    // Network Operations
    bool setDNS(const std::string& primary_dns, const std::string& secondary_dns);
    std::string resolveDomain(const std::string& domain);
    bool pingHost(const std::string& host);
    
private:
    NetworkManager();
    ~NetworkManager();
    
    std::map<std::string, NetworkInterface> interfaces;
    
    void initializeInterfaces();
};

#endif // NETWORK_MANAGER_HPP
