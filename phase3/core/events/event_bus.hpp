#ifndef EVENT_BUS_HPP
#define EVENT_BUS_HPP

#include "event_types.hpp"
#include <functional>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <atomic>

// ============================================================================
// Event Bus - Central event dispatch system for microservices
// ============================================================================

using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
    // Singleton pattern
    static EventBus& getInstance();
    
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    
    // ========================================================================
    // Event Subscription
    // ========================================================================
    
    // Subscribe to specific event type
    // Returns subscription ID for later unsubscribe
    uint64_t subscribe(EventType event_type, EventHandler handler);
    
    // Subscribe to specific event from specific source
    uint64_t subscribeToSource(EventType event_type, const std::string& source_id, EventHandler handler);
    
    // Unsubscribe from event
    bool unsubscribe(uint64_t subscription_id);
    
    // ========================================================================
    // Event Publishing
    // ========================================================================
    
    // Publish event to all interested subscribers (async)
    void publish(const Event& event);
    
    // Publish event synchronously (blocking until all handlers complete)
    void publishSync(const Event& event);
    
    // ========================================================================
    // Bus Control
    // ========================================================================
    
    // Start event processing thread
    bool start();
    
    // Stop event processing
    void stop();
    
    // Drain all pending events
    void drain();
    
    // Check if bus is running
    bool isRunning() const;
    
    // Get pending event count
    int getPendingEventCount() const;
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct BusStatistics {
        uint64_t total_events_published;
        uint64_t total_events_processed;
        uint64_t total_handlers_called;
        uint64_t dropped_events;
        int current_queue_depth;
    };
    
    BusStatistics getStatistics() const;

private:
    // Private constructor
    EventBus();
    
    // Destructor
    ~EventBus();
    
    // Event processing worker thread
    void processingWorker();
    
    // Match event to subscriptions
    void matchAndDispatch(const Event& event);
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    // Subscription management
    struct Subscription {
        uint64_t subscription_id;
        EventType event_type;
        std::string source_id;  // Empty = all sources
        EventHandler handler;
    };
    
    std::vector<Subscription> subscriptions;
    std::atomic<uint64_t> next_subscription_id{1};
    
    // Event queue
    std::queue<Event> event_queue;
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    
    // Processing thread
    std::unique_ptr<std::thread> processing_thread;
    std::atomic<bool> is_running{false};
    std::atomic<bool> should_stop{false};
    
    // Statistics
    mutable std::mutex stats_mutex;
    uint64_t total_events_published{0};
    uint64_t total_events_processed{0};
    uint64_t total_handlers_called{0};
    uint64_t dropped_events{0};
    
    // Max queue depth before dropping non-critical events
    static constexpr int MAX_QUEUE_DEPTH = 10000;
    static constexpr int CRITICAL_QUEUE_DEPTH = 9000;  // Reserve space for critical
};

#endif // EVENT_BUS_HPP
