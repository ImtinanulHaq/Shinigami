#define _POSIX_C_SOURCE 200809L

/*
 * sm_discovery.c - Service discovery and publish/subscribe implementation
 *
 * Thread-safe event publication system for service lifecycle notifications
 */

#include "../enterprise/sm_discovery.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MAX_SERVICES 64
#define MAX_EVENT_MASK 0x3F

typedef struct {
    int subscription_id;
    char filter_service_name[64];
    sm_discovery_event_t event_mask;
    sm_discovery_callback_t callback;
    void* userdata;
    int active;
} subscription_t;

typedef struct {
    char service_name[64];
    char socket_path[256];
    int32_t pid;
    int32_t priority;
    uint64_t registration_time_ns;
    sm_discovery_event_t last_event;
} registered_service_t;

typedef struct {
    subscription_t* subscriptions;
    int max_subscribers;
    int subscriber_count;
    
    registered_service_t* services;
    int service_count;
    
    uint64_t total_events_published;
    
    pthread_rwlock_t lock;
} discovery_system_t;

static discovery_system_t g_discovery = {0};
static int g_discovery_initialized = 0;

static uint64_t get_current_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int sm_discovery_init(int max_subscribers)
{
    if (g_discovery_initialized) {
        sm_log(SM_LOG_WARN, "discovery: already initialized");
        return -1;
    }
    
    if (max_subscribers <= 0 || max_subscribers > 1024) {
        sm_log(SM_LOG_ERROR, "discovery: invalid max_subscribers %d", max_subscribers);
        return -1;
    }
    
    memset(&g_discovery, 0, sizeof(g_discovery));
    
    /* Initialize RW lock */
    if (pthread_rwlock_init(&g_discovery.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "discovery: pthread_rwlock_init failed: %s", strerror(errno));
        return -1;
    }
    
    /* Allocate subscription array */
    g_discovery.subscriptions = (subscription_t*)calloc(max_subscribers, sizeof(subscription_t));
    if (!g_discovery.subscriptions) {
        sm_log(SM_LOG_ERROR, "discovery: malloc subscriptions failed");
        pthread_rwlock_destroy(&g_discovery.lock);
        return -1;
    }
    
    /* Allocate service registry */
    g_discovery.services = (registered_service_t*)calloc(MAX_SERVICES, sizeof(registered_service_t));
    if (!g_discovery.services) {
        sm_log(SM_LOG_ERROR, "discovery: malloc services failed");
        free(g_discovery.subscriptions);
        pthread_rwlock_destroy(&g_discovery.lock);
        return -1;
    }
    
    g_discovery.max_subscribers = max_subscribers;
    
    g_discovery_initialized = 1;
    sm_log(SM_LOG_INFO, "discovery: initialized with max %d subscribers", max_subscribers);
    return 0;
}

int sm_discovery_publish(sm_discovery_event_t event_type, const char* service_name,
                         const char* socket_path, int32_t pid, int priority)
{
    if (!g_discovery_initialized) {
        sm_log(SM_LOG_ERROR, "discovery: not initialized");
        return -1;
    }
    
    /* Validate inputs */
    if (!service_name || strlen(service_name) == 0) {
        sm_log(SM_LOG_ERROR, "discovery: invalid service_name");
        return -1;
    }
    
    if (event_type < SM_EVENT_SERVICE_REGISTERED || event_type > SM_EVENT_SERVICE_UPDATED) {
        sm_log(SM_LOG_ERROR, "discovery: invalid event_type %d", event_type);
        return -1;
    }
    
    if (priority < 0 || priority > 2) {
        priority = 1;  /* Default to normal */
    }
    
    pthread_rwlock_wrlock(&g_discovery.lock);
    
    /* Update service registry based on event */
    if (event_type == SM_EVENT_SERVICE_REGISTERED) {
        if (g_discovery.service_count >= MAX_SERVICES) {
            pthread_rwlock_unlock(&g_discovery.lock);
            sm_log(SM_LOG_ERROR, "discovery: service registry full");
            return -1;
        }
        
        registered_service_t* svc = &g_discovery.services[g_discovery.service_count++];
        strncpy(svc->service_name, service_name, sizeof(svc->service_name) - 1);
        strncpy(svc->socket_path, socket_path ? socket_path : "", sizeof(svc->socket_path) - 1);
        svc->pid = pid;
        svc->priority = (int32_t)priority;
        svc->registration_time_ns = get_current_time_ns();
        svc->last_event = event_type;
    } else if (event_type == SM_EVENT_SERVICE_DEREGISTERED) {
        /* Remove service from registry */
        for (int i = 0; i < g_discovery.service_count; i++) {
            if (strcmp(g_discovery.services[i].service_name, service_name) == 0) {
                memmove(&g_discovery.services[i], &g_discovery.services[i + 1],
                        (g_discovery.service_count - i - 1) * sizeof(registered_service_t));
                g_discovery.service_count--;
                break;
            }
        }
    } else {
        /* Update service status */
        for (int i = 0; i < g_discovery.service_count; i++) {
            if (strcmp(g_discovery.services[i].service_name, service_name) == 0) {
                g_discovery.services[i].last_event = event_type;
                break;
            }
        }
    }
    
    g_discovery.total_events_published++;
    
    /* Create notification event */
    sm_discovery_event_notification_t notification = {0};
    notification.event_type = event_type;
    notification.timestamp_ns = get_current_time_ns();
    strncpy(notification.service_name, service_name, sizeof(notification.service_name) - 1);
    if (socket_path) strncpy(notification.socket_path, socket_path, sizeof(notification.socket_path) - 1);
    notification.pid = pid;
    notification.priority = priority;
    
    /* Notify all subscribers */
    for (int i = 0; i < g_discovery.subscriber_count; i++) {
        subscription_t* sub = &g_discovery.subscriptions[i];
        
        if (!sub->active) continue;
        
        /* Check if subscriber is interested in this event */
        int matches_filter = (sub->event_mask == 0) || (sub->event_mask & event_type);
        int matches_service = (strlen(sub->filter_service_name) == 0) ||
                             (strcmp(sub->filter_service_name, service_name) == 0);
        
        if (matches_filter && matches_service && sub->callback) {
            pthread_rwlock_unlock(&g_discovery.lock);
            sm_log(SM_LOG_DEBUG, "discovery: invoking callback for subscriber %d", sub->subscription_id);
            sub->callback(&notification, sub->userdata);
            pthread_rwlock_wrlock(&g_discovery.lock);
        }
    }
    
    pthread_rwlock_unlock(&g_discovery.lock);
    
    sm_log(SM_LOG_INFO, "discovery: published event %d for service '%s' (pid=%d)",
           event_type, service_name, pid);
    return 0;
}

int sm_discovery_subscribe(const char* filter_service_name, sm_discovery_event_t event_mask,
                           sm_discovery_callback_t callback, void* userdata)
{
    if (!g_discovery_initialized) {
        sm_log(SM_LOG_ERROR, "discovery: not initialized");
        return -1;
    }
    
    if (!callback) {
        sm_log(SM_LOG_ERROR, "discovery: callback is NULL");
        return -1;
    }
    
    if (event_mask & ~MAX_EVENT_MASK) {
        sm_log(SM_LOG_ERROR, "discovery: invalid event_mask 0x%x", event_mask);
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_discovery.lock);
    
    if (g_discovery.subscriber_count >= g_discovery.max_subscribers) {
        pthread_rwlock_unlock(&g_discovery.lock);
        sm_log(SM_LOG_ERROR, "discovery: subscriber limit reached");
        return -1;
    }
    
    int subscription_id = g_discovery.subscriber_count++;
    subscription_t* sub = &g_discovery.subscriptions[subscription_id];
    
    sub->subscription_id = subscription_id;
    sub->event_mask = event_mask;
    sub->callback = callback;
    sub->userdata = userdata;
    sub->active = 1;
    
    if (filter_service_name) {
        strncpy(sub->filter_service_name, filter_service_name, sizeof(sub->filter_service_name) - 1);
    }
    
    pthread_rwlock_unlock(&g_discovery.lock);
    
    sm_log(SM_LOG_INFO, "discovery: new subscription %d (filter='%s', mask=0x%x)",
           subscription_id, filter_service_name ? filter_service_name : "*", event_mask);
    return subscription_id;
}

int sm_discovery_unsubscribe(int subscription_id)
{
    if (!g_discovery_initialized) {
        return -1;
    }
    
    if (subscription_id < 0 || subscription_id >= g_discovery.subscriber_count) {
        sm_log(SM_LOG_WARN, "discovery: subscription id %d not found", subscription_id);
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_discovery.lock);
    g_discovery.subscriptions[subscription_id].active = 0;
    pthread_rwlock_unlock(&g_discovery.lock);
    
    sm_log(SM_LOG_INFO, "discovery: unsubscribed id %d", subscription_id);
    return 0;
}

sm_discovery_service_t* sm_discovery_query_services(const char* filter_name, int* count_out)
{
    if (!g_discovery_initialized || !count_out) {
        if (count_out) *count_out = 0;
        return NULL;
    }
    
    pthread_rwlock_rdlock(&g_discovery.lock);
    
    /* Count matching services */
    int count = 0;
    for (int i = 0; i < g_discovery.service_count; i++) {
        if (!filter_name || strlen(filter_name) == 0 ||
            strcmp(g_discovery.services[i].service_name, filter_name) == 0) {
            count++;
        }
    }
    
    /* Allocate result array */
    sm_discovery_service_t* result = (sm_discovery_service_t*)malloc(count * sizeof(sm_discovery_service_t));
    if (!result && count > 0) {
        pthread_rwlock_unlock(&g_discovery.lock);
        *count_out = 0;
        return NULL;
    }
    
    /* Copy matching services */
    int idx = 0;
    for (int i = 0; i < g_discovery.service_count && idx < count; i++) {
        if (!filter_name || strlen(filter_name) == 0 ||
            strcmp(g_discovery.services[i].service_name, filter_name) == 0) {
            result[idx].pid = g_discovery.services[i].pid;
            result[idx].priority = g_discovery.services[i].priority;
            result[idx].registration_time_ns = g_discovery.services[i].registration_time_ns;
            strncpy(result[idx].service_name, g_discovery.services[i].service_name,
                   sizeof(result[idx].service_name) - 1);
            result[idx].service_name[sizeof(result[idx].service_name) - 1] = '\0';
            strncpy(result[idx].socket_path, g_discovery.services[i].socket_path,
                   sizeof(result[idx].socket_path) - 1);
            result[idx].socket_path[sizeof(result[idx].socket_path) - 1] = '\0';
            idx++;
        }
    }
    
    pthread_rwlock_unlock(&g_discovery.lock);
    
    *count_out = count;
    return result;
}

void sm_discovery_free_service_list(sm_discovery_service_t* services)
{
    if (services) {
        free(services);
    }
}

sm_discovery_stats_t sm_discovery_get_stats(void)
{
    sm_discovery_stats_t stats = {0};
    
    if (!g_discovery_initialized) {
        return stats;
    }
    
    pthread_rwlock_rdlock(&g_discovery.lock);
    stats.total_subscribers = g_discovery.subscriber_count;
    stats.total_events_published = (int)g_discovery.total_events_published;
    stats.active_services = g_discovery.service_count;
    pthread_rwlock_unlock(&g_discovery.lock);
    
    return stats;
}

void sm_discovery_cleanup(void)
{
    if (!g_discovery_initialized) {
        return;
    }
    
    pthread_rwlock_wrlock(&g_discovery.lock);
    
    free(g_discovery.subscriptions);
    free(g_discovery.services);
    memset(&g_discovery, 0, sizeof(g_discovery));
    
    pthread_rwlock_unlock(&g_discovery.lock);
    pthread_rwlock_destroy(&g_discovery.lock);
    
    g_discovery_initialized = 0;
    sm_log(SM_LOG_INFO, "discovery: cleanup complete");
}
