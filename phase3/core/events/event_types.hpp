#ifndef EVENT_TYPES_HPP
#define EVENT_TYPES_HPP

#include <string>
#include <map>
#include <variant>
#include <cstdint>
#include <chrono>

// ============================================================================
// Event Types - Define all system and application events
// ============================================================================

// Base event type enumeration
enum class EventType : uint16_t {
    // System Events (0-99)
    SYSTEM_BOOT = 0,
    SYSTEM_SHUTDOWN = 1,
    SYSTEM_READY = 2,
    SYSTEM_ERROR = 3,
    SYSTEM_STATE_CHANGED = 4,
    
    // Application Events (100-199)
    APP_LAUNCH = 100,
    APP_READY = 101,
    APP_CLOSE = 102,
    APP_ERROR = 103,
    APP_FOCUSED = 104,
    APP_UNFOCUSED = 105,
    
    // GUI Events (200-299)
    GUI_WINDOW_CREATED = 200,
    GUI_WINDOW_CLOSED = 201,
    GUI_WINDOW_MOVED = 202,
    GUI_WINDOW_RESIZED = 203,
    GUI_BUTTON_CLICKED = 204,
    GUI_TOUCH_INPUT = 205,
    GUI_MOUSE_MOVED = 206,
    GUI_KEYBOARD_INPUT = 207,
    
    // Notification Events (300-399)
    NOTIFICATION_POSTED = 300,
    NOTIFICATION_DISMISSED = 301,
    NOTIFICATION_CLICKED = 302,
    
    // Service Events (400-499)
    SERVICE_REGISTERED = 400,
    SERVICE_UNREGISTERED = 401,
    SERVICE_STATE_CHANGED = 402,
    SERVICE_HEALTH_CHECK = 403,
    
    // Custom Events (500+)
    CUSTOM_EVENT = 500
};

// Event priority levels
enum class EventPriority : uint8_t {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

// Event data variant - flexible event payload
using EventData = std::variant<
    int32_t,
    uint32_t,
    int64_t,
    uint64_t,
    float,
    double,
    std::string,
    bool,
    std::nullptr_t
>;

// ============================================================================
// Event Structure
// ============================================================================
struct Event {
    EventType type;
    EventPriority priority;
    std::string source_id;           // Service/component that emitted event
    std::string target_id;           // Optional: specific target service
    std::map<std::string, EventData> payload;  // Event-specific data
    uint64_t timestamp_ms;           // Event creation timestamp
    uint64_t event_id;               // Unique event identifier
    bool is_critical;                // Critical events cannot be dropped
    
    Event()
        : type(EventType::CUSTOM_EVENT), 
          priority(EventPriority::NORMAL),
          source_id("unknown"), 
          target_id(""),
          timestamp_ms(0), 
          event_id(0), 
          is_critical(false) {}
    
    Event(EventType t, const std::string& src)
        : type(t), 
          priority(EventPriority::NORMAL),
          source_id(src), 
          target_id(""),
          timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count()),
          event_id(0), 
          is_critical(false) {}
};

#endif // EVENT_TYPES_HPP
