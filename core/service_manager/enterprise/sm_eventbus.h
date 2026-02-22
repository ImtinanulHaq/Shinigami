/*
 * sm_eventbus.h - Event bus for inter-service communication
 *
 * Services can publish and subscribe to events.
 * Enables loose coupling through pub/sub pattern.
 * 
 * EXAMPLES:
 *   database.ready - database service is ready
 *   cache.full - cache reached max capacity
 *   service.crash - service crashed
 *   config.reload - configuration reloaded
 * 
 * USAGE:
 *   sm_eventbus_init(max_subscribers);
 *   sm_eventbus_publish("database.ready", "postgres_db", priority=1);
 *   
 *   sub_id = sm_eventbus_subscribe("database.ready", callback, userdata);
 *   
 *   sm_eventbus_unsubscribe(sub_id);
 *   sm_eventbus_cleanup();
 */

#ifndef SM_EVENTBUS_H
#define SM_EVENTBUS_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event priority levels */
typedef enum {
    EVENT_PRIORITY_LOW = 0,
    EVENT_PRIORITY_NORMAL = 1,
    EVENT_PRIORITY_HIGH = 2,
    EVENT_PRIORITY_CRITICAL = 3,
} sm_event_priority_t;

/* Event info */
typedef struct {
    char event_type[128];        /* e.g., "database.ready" */
    char publisher_service[64];  /* Service that published event */
    char event_data[512];        /* Event-specific data */
    sm_event_priority_t priority;
    time_t timestamp;
    uint64_t event_id;           /* Unique event ID */
} sm_event_t;

/* Event callback type */
typedef void (*sm_event_callback_t)(const sm_event_t* event, void* userdata);

/* Event statistics */
typedef struct {
    uint64_t total_events_published;
    uint64_t total_subscriptions;
    uint64_t currently_active_subscriptions;
    uint64_t events_delivered;
    uint64_t events_dropped;
} sm_eventbus_stats_t;

/*
 * sm_eventbus_init(max_subscribers_per_event)
 * 
 * Initialize event bus system.
 * 
 * PARAMETERS:
 *   max_subscribers_per_event - max listeners per event type (default 256)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_eventbus_init(int max_subscribers_per_event);

/*
 * sm_eventbus_publish(event_type, publisher_service, event_data, priority)
 * 
 * Publish an event to all subscribers.
 * 
 * PARAMETERS:
 *   event_type - event identifier (e.g., "database.ready", "service.crash")
 *   publisher_service - name of service publishing event
 *   event_data - event payload (can be arbitrary data, JSON-formatted recommended)
 *   priority - EVENT_PRIORITY_CRITICAL for urgent events
 * 
 * RETURNS:
 *   Event ID on success
 *   0 on error
 * 
 * NOTES:
 *   - Non-blocking, calls subscribers synchronously
 *   - Slow subscribers may delay other events
 */
uint64_t sm_eventbus_publish(const char* event_type, const char* publisher_service,
                             const char* event_data, sm_event_priority_t priority);

/*
 * sm_eventbus_subscribe(event_type, callback, userdata)
 * 
 * Subscribe to events of a specific type.
 * 
 * PARAMETERS:
 *   event_type - event to listen for (use "*" for all events)
 *   callback - function to call when event published
 *   userdata - context to pass to callback
 * 
 * RETURNS:
 *   Subscription ID on success (>0)
 *   -1 on error
 * 
 * NOTES:
 *   - Wildcard "*" subscribes to all events
 *   - Callbacks should return quickly (no blocking operations)
 */
int sm_eventbus_subscribe(const char* event_type, sm_event_callback_t callback, void* userdata);

/*
 * sm_eventbus_unsubscribe(subscription_id)
 * 
 * Unsubscribe from events.
 * 
 * RETURNS:
 *   0 on success
 *   -1 if subscription not found
 */
int sm_eventbus_unsubscribe(int subscription_id);

/*
 * sm_eventbus_get_event_history(event_type, history_buffer, max_events)
 * 
 * Get recent published events (for debugging/monitoring).
 * 
 * PARAMETERS:
 *   event_type - filter by type (NULL for all types)
 *   history_buffer - output array
 *   max_events - max to return
 * 
 * RETURNS:
 *   Number of events returned
 */
int sm_eventbus_get_event_history(const char* event_type, sm_event_t* history_buffer, int max_events);

/*
 * sm_eventbus_query_subscribers(event_type)
 * 
 * Get number of subscribers for an event type.
 * 
 * RETURNS:
 *   Subscriber count
 */
int sm_eventbus_query_subscribers(const char* event_type);

/*
 * sm_eventbus_get_stats()
 * 
 * Get event bus statistics.
 * 
 * RETURNS:
 *   Statistics structure
 */
sm_eventbus_stats_t sm_eventbus_get_stats(void);

/*
 * sm_eventbus_clear_history()
 * 
 * Clear event history buffer.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_eventbus_clear_history(void);

/*
 * sm_eventbus_cleanup()
 * 
 * Shutdown event bus system.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_eventbus_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_EVENTBUS_H */
