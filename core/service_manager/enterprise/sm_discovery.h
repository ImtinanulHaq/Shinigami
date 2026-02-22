#ifndef SM_DISCOVERY_H
#define SM_DISCOVERY_H

/*
 * sm_discovery.h - Service discovery and publish/subscribe system
 *
 * Enables services to publish events (registration, status change, deregistration)
 * and other services to subscribe for notifications. Implements observer pattern
 * for automatic service discovery without polling.
 */

#include <stdint.h>

typedef enum {
    SM_EVENT_SERVICE_REGISTERED = 1,
    SM_EVENT_SERVICE_DEREGISTERED = 2,
    SM_EVENT_SERVICE_READY = 3,
    SM_EVENT_SERVICE_FAILED = 4,
    SM_EVENT_SERVICE_UPDATED = 5,
} sm_discovery_event_t;

typedef struct {
    sm_discovery_event_t event_type;
    uint64_t timestamp_ns;
    char service_name[64];
    char socket_path[256];
    int32_t pid;
    int priority;  /* 0=low, 1=normal, 2=high */
} sm_discovery_event_notification_t;

typedef void (*sm_discovery_callback_t)(sm_discovery_event_notification_t* event, void* userdata);

/*
 * sm_discovery_init() - Initialize discovery system
 *
 * Sets up internal data structures for event publishing and subscription.
 * Max subscribers is the maximum number of callback registrations allowed.
 */
int sm_discovery_init(int max_subscribers);

/*
 * sm_discovery_publish() - Publish an event about service status change
 *
 * Immediately notifies all registered subscribers with event details.
 * Returns 0 on success, -1 on error (invalid event_type, service_name empty, etc).
 */
int sm_discovery_publish(sm_discovery_event_t event_type, const char* service_name,
                         const char* socket_path, int32_t pid, int priority);

/*
 * sm_discovery_subscribe() - Register callback for service events
 *
 * Callback will be invoked in publisher's thread context when matching events occur.
 * Returns subscription ID on success (>= 0), -1 on error.
 * Callback must return quickly and not block.
 */
int sm_discovery_subscribe(const char* filter_service_name, sm_discovery_event_t event_mask,
                           sm_discovery_callback_t callback, void* userdata);

/*
 * sm_discovery_unsubscribe() - Remove callback registration
 *
 * Returns 0 on success, -1 if subscription ID not found
 */
int sm_discovery_unsubscribe(int subscription_id);

/*
 * sm_discovery_query_services() - Get list of currently registered services
 *
 * Returns snapshot of registered services matching optional filter.
 * Caller must free returned array with sm_discovery_free_service_list().
 */
typedef struct {
    char service_name[64];
    char socket_path[256];
    int32_t pid;
    int32_t priority;
    uint64_t registration_time_ns;
} sm_discovery_service_t;

sm_discovery_service_t* sm_discovery_query_services(const char* filter_name, int* count_out);
void sm_discovery_free_service_list(sm_discovery_service_t* services);

/*
 * sm_discovery_get_stats() - Get event system statistics
 */
typedef struct {
    int total_subscribers;
    int total_events_published;
    int active_services;
} sm_discovery_stats_t;

sm_discovery_stats_t sm_discovery_get_stats(void);

/*
 * sm_discovery_cleanup() - Cleanup and shutdown discovery system
 */
void sm_discovery_cleanup(void);

#endif /* SM_DISCOVERY_H */
