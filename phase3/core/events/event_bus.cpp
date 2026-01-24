#include "../events/event_bus.hpp"
#include <algorithm>
#include <iostream>

static EventBus* g_event_bus = nullptr;

EventBus& EventBus::getInstance() {
    if (!g_event_bus) {
        g_event_bus = new EventBus();
    }
    return *g_event_bus;
}

EventBus::EventBus() : is_running(false), should_stop(false) {}

EventBus::~EventBus() {
    stop();
}

uint64_t EventBus::subscribe(EventType event_type, EventHandler handler) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    
    uint64_t sub_id = next_subscription_id++;
    subscriptions.push_back({
        sub_id,
        event_type,
        "",  // No source filter
        handler
    });
    
    return sub_id;
}

uint64_t EventBus::subscribeToSource(EventType event_type, const std::string& source_id, EventHandler handler) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    
    uint64_t sub_id = next_subscription_id++;
    subscriptions.push_back({
        sub_id,
        event_type,
        source_id,
        handler
    });
    
    return sub_id;
}

bool EventBus::unsubscribe(uint64_t subscription_id) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    
    auto it = std::remove_if(subscriptions.begin(), subscriptions.end(),
        [subscription_id](const Subscription& s) { return s.subscription_id == subscription_id; });
    
    if (it != subscriptions.end()) {
        subscriptions.erase(it, subscriptions.end());
        return true;
    }
    
    return false;
}

void EventBus::publish(const Event& event) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    
    // Check queue depth
    if (event_queue.size() >= CRITICAL_QUEUE_DEPTH && !event.is_critical) {
        dropped_events++;
        return;  // Drop non-critical events when queue is full
    }
    
    if (event_queue.size() >= MAX_QUEUE_DEPTH) {
        dropped_events++;
        return;  // Drop even critical events at absolute max
    }
    
    event_queue.push(event);
    queue_cv.notify_one();
}

void EventBus::publishSync(const Event& event) {
    matchAndDispatch(event);
    total_events_processed++;
}

bool EventBus::start() {
    if (is_running) return false;
    
    is_running = true;
    should_stop = false;
    processing_thread = std::make_unique<std::thread>(&EventBus::processingWorker, this);
    
    return true;
}

void EventBus::stop() {
    if (!is_running) return;
    
    should_stop = true;
    is_running = false;
    
    if (processing_thread && processing_thread->joinable()) {
        queue_cv.notify_one();
        processing_thread->join();
    }
}

void EventBus::drain() {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (event_queue.empty()) break;
        
        Event event = event_queue.front();
        event_queue.pop();
        lock.unlock();
        
        matchAndDispatch(event);
    }
}

bool EventBus::isRunning() const {
    return is_running;
}

int EventBus::getPendingEventCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return event_queue.size();
}

EventBus::BusStatistics EventBus::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    return {
        total_events_published,
        total_events_processed,
        total_handlers_called,
        dropped_events,
        static_cast<int>(event_queue.size())
    };
}

void EventBus::processingWorker() {
    while (!should_stop) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        if (event_queue.empty()) {
            queue_cv.wait_for(lock, std::chrono::milliseconds(100));
            continue;
        }
        
        Event event = event_queue.front();
        event_queue.pop();
        lock.unlock();
        
        matchAndDispatch(event);
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex);
            total_events_processed++;
        }
    }
}

void EventBus::matchAndDispatch(const Event& event) {
    for (const auto& sub : subscriptions) {
        // Check event type
        if (sub.event_type != event.type) continue;
        
        // Check source filter (if set)
        if (!sub.source_id.empty() && sub.source_id != event.source_id) continue;
        
        // Call handler
        sub.handler(event);
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex);
            total_handlers_called++;
        }
    }
}
