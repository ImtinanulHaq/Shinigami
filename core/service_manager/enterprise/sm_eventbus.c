#define _POSIX_C_SOURCE 200809L

/*
 * sm_eventbus.c - Event bus implementation
 *
 * Pub/sub event system with history and statistics
 */

#include "../enterprise/sm_eventbus.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAX_EVENT_TYPES 256
#define MAX_SUBSCRIBERS_GLOBAL 4096
#define MAX_EVENT_HISTORY 512

typedef struct {
    int subscription_id;
    char event_type[128];
    sm_event_callback_t callback;
    void* userdata;
    int active;
} subscriber_t;

typedef struct {
    sm_event_t events[MAX_EVENT_HISTORY];
    int event_count;
    int current_idx;
} event_history_t;

typedef struct {
    subscriber_t subscribers[MAX_SUBSCRIBERS_GLOBAL];
    int subscriber_count;
    
    event_history_t history;
    
    uint64_t next_event_id;
    uint64_t events_published;
    uint64_t events_delivered;
    uint64_t events_dropped;
    
    int max_subs_per_event;
    
    pthread_rwlock_t lock;
    int initialized;
} eventbus_t;

static eventbus_t g_eventbus = {0};

int sm_eventbus_init(int max_subscribers_per_event)
{
    if (g_eventbus.initialized) {
        sm_log(SM_LOG_WARN, "eventbus: already initialized");
        return 0;
    }
    
    if (max_subscribers_per_event <= 0 || max_subscribers_per_event > 512) {
        sm_log(SM_LOG_ERROR, "eventbus: invalid max_subscribers %d", max_subscribers_per_event);
        return -1;
    }
    
    memset(&g_eventbus, 0, sizeof(g_eventbus));
    
    if (pthread_rwlock_init(&g_eventbus.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "eventbus: pthread_rwlock_init failed: %s", strerror(errno));
        return -1;
    }
    
    g_eventbus.max_subs_per_event = max_subscribers_per_event;
    g_eventbus.next_event_id = 1;
    g_eventbus.initialized = 1;
    
    sm_log(SM_LOG_INFO, "eventbus: initialized (max %d subs/event)", max_subscribers_per_event);
    return 0;
}

uint64_t sm_eventbus_publish(const char* event_type, const char* publisher_service,
                             const char* event_data, sm_event_priority_t priority)
{
    if (!g_eventbus.initialized) {
        return 0;
    }
    
    if (!event_type || !publisher_service) {
        sm_log(SM_LOG_ERROR, "eventbus: invalid parameters");
        return 0;
    }
    
    pthread_rwlock_wrlock(&g_eventbus.lock);
    
    /* Create event */
    sm_event_t event;
    memset(&event, 0, sizeof(event));
    event.event_id = g_eventbus.next_event_id++;
    event.timestamp = time(NULL);
    event.priority = priority;
    
    strncpy(event.event_type, event_type, sizeof(event.event_type) - 1);
    strncpy(event.publisher_service, publisher_service, sizeof(event.publisher_service) - 1);
    if (event_data) {
        strncpy(event.event_data, event_data, sizeof(event.event_data) - 1);
    }
    
    /* Add to history */
    if (g_eventbus.history.event_count < MAX_EVENT_HISTORY) {
        g_eventbus.history.event_count++;
    }
    g_eventbus.history.current_idx = (g_eventbus.history.current_idx + 1) % MAX_EVENT_HISTORY;
    g_eventbus.history.events[g_eventbus.history.current_idx] = event;
    
    g_eventbus.events_published++;
    uint64_t event_id = event.event_id;
    
    /* Notify subscribers */
    int delivered = 0;
    for (int i = 0; i < g_eventbus.subscriber_count; i++) {
        subscriber_t* sub = &g_eventbus.subscribers[i];
        
        if (!sub->active || !sub->callback) continue;
        
        /* Check if subscriber is interested in this event */
        int matches = (strcmp(sub->event_type, "*") == 0) ||
                     (strcmp(sub->event_type, event_type) == 0);
        
        if (matches) {
            /* Call subscriber outside lock to prevent deadlocks */
            pthread_rwlock_unlock(&g_eventbus.lock);
            
            sm_log(SM_LOG_DEBUG, "eventbus: delivering event %llu to subscriber %d",
                  (unsigned long long)event_id, sub->subscription_id);
            sub->callback(&event, sub->userdata);
            
            pthread_rwlock_wrlock(&g_eventbus.lock);
            delivered++;
        }
    }
    
    g_eventbus.events_delivered += delivered;
    
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    sm_log(SM_LOG_DEBUG, "eventbus: published event '%s' (id=%llu, subscribers=%d)",
           event_type, (unsigned long long)event_id, delivered);
    
    return event_id;
}

int sm_eventbus_subscribe(const char* event_type, sm_event_callback_t callback, void* userdata)
{
    if (!g_eventbus.initialized) {
        return -1;
    }
    
    if (!event_type || !callback) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_eventbus.lock);
    
    if (g_eventbus.subscriber_count >= MAX_SUBSCRIBERS_GLOBAL) {
        pthread_rwlock_unlock(&g_eventbus.lock);
        sm_log(SM_LOG_ERROR, "eventbus: subscriber limit reached");
        return -1;
    }
    
    int sub_id = g_eventbus.subscriber_count;
    subscriber_t* sub = &g_eventbus.subscribers[sub_id];
    
    memset(sub, 0, sizeof(*sub));
    sub->subscription_id = sub_id;
    sub->callback = callback;
    sub->userdata = userdata;
    sub->active = 1;
    strncpy(sub->event_type, event_type, sizeof(sub->event_type) - 1);
    
    g_eventbus.subscriber_count++;
    
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    sm_log(SM_LOG_INFO, "eventbus: new subscription %d (event='%s')", sub_id, event_type);
    return sub_id;
}

int sm_eventbus_unsubscribe(int subscription_id)
{
    if (!g_eventbus.initialized) {
        return -1;
    }
    
    if (subscription_id < 0 || subscription_id >= g_eventbus.subscriber_count) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_eventbus.lock);
    g_eventbus.subscribers[subscription_id].active = 0;
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    sm_log(SM_LOG_INFO, "eventbus: unsubscribed %d", subscription_id);
    return 0;
}

int sm_eventbus_get_event_history(const char* event_type, sm_event_t* history_buffer, int max_events)
{
    if (!history_buffer || max_events <= 0) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&g_eventbus.lock);
    
    int count = 0;
    for (int i = 0; i < g_eventbus.history.event_count && count < max_events; i++) {
        int idx = (g_eventbus.history.current_idx - g_eventbus.history.event_count + i + 1) %
                 MAX_EVENT_HISTORY;
        
        if (!event_type || strcmp(event_type, g_eventbus.history.events[idx].event_type) == 0) {
            history_buffer[count++] = g_eventbus.history.events[idx];
        }
    }
    
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    return count;
}

int sm_eventbus_query_subscribers(const char* event_type)
{
    if (!event_type) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&g_eventbus.lock);
    
    int count = 0;
    for (int i = 0; i < g_eventbus.subscriber_count; i++) {
        if (g_eventbus.subscribers[i].active &&
            (strcmp(g_eventbus.subscribers[i].event_type, "*") == 0 ||
             strcmp(g_eventbus.subscribers[i].event_type, event_type) == 0)) {
            count++;
        }
    }
    
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    return count;
}

sm_eventbus_stats_t sm_eventbus_get_stats(void)
{
    sm_eventbus_stats_t stats = {0};
    
    if (!g_eventbus.initialized) {
        return stats;
    }
    
    pthread_rwlock_rdlock(&g_eventbus.lock);
    
    stats.total_events_published = g_eventbus.events_published;
    stats.events_delivered = g_eventbus.events_delivered;
    stats.events_dropped = g_eventbus.events_dropped;
    
    int active_count = 0;
    for (int i = 0; i < g_eventbus.subscriber_count; i++) {
        if (g_eventbus.subscribers[i].active) {
            active_count++;
        }
    }
    
    stats.total_subscriptions = g_eventbus.subscriber_count;
    stats.currently_active_subscriptions = active_count;
    
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    return stats;
}

int sm_eventbus_clear_history(void)
{
    if (!g_eventbus.initialized) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_eventbus.lock);
    memset(&g_eventbus.history, 0, sizeof(g_eventbus.history));
    pthread_rwlock_unlock(&g_eventbus.lock);
    
    return 0;
}

int sm_eventbus_cleanup(void)
{
    if (!g_eventbus.initialized) {
        return 0;
    }
    
    pthread_rwlock_wrlock(&g_eventbus.lock);
    memset(&g_eventbus, 0, sizeof(g_eventbus));
    pthread_rwlock_unlock(&g_eventbus.lock);
    pthread_rwlock_destroy(&g_eventbus.lock);
    
    sm_log(SM_LOG_INFO, "eventbus: cleanup complete");
    return 0;
}
