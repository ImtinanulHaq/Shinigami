#define _POSIX_C_SOURCE 200809L

/*
 * sm_container.c - Container support implementation
 *
 * Docker, LXC, Podman, systemd-nspawn container management
 */

#include "../enterprise/sm_container.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

#define MAX_SERVICES_PER_CONTAINER 64
#define MAX_CONTAINERS 256
#define MAX_SERVICE_MAPPINGS 512

typedef struct {
    char service_name[64];
    char container_id[128];
    sm_container_type_t container_type;
    int host_pid;
    int container_pid;
    uint64_t registration_time;
} service_mapping_t;

typedef struct {
    char container_id[128];
    sm_container_type_t type;
    char container_name[128];
    int is_running;
    uint64_t created_time;
    char runtime_socket[256];  /* Docker socket, LXC root, etc */
} container_entry_t;

typedef struct {
    service_mapping_t service_mappings[MAX_SERVICE_MAPPINGS];
    int mapping_count;
    
    container_entry_t containers[MAX_CONTAINERS];
    int container_count;
    
    sm_container_info_t current_container;
    int running_in_container;
    
    char namespace_path[256];
    
    pthread_rwlock_t lock;
    int initialized;
} container_system_t;

static container_system_t g_container_system = {0};

static sm_container_type_t detect_container_runtime(void)
{
    /* Check for /.dockerenv (Docker) */
    if (access("/.dockerenv", F_OK) == 0) {
        return CONTAINER_TYPE_DOCKER;
    }
    
    /* Check for LXC */
    if (access("/proc/self/cgroup", F_OK) == 0) {
        FILE* f = fopen("/proc/self/cgroup", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "/lxc/") || strstr(line, "/lxc-")) {
                    fclose(f);
                    return CONTAINER_TYPE_LXC;
                }
                if (strstr(line, "/docker/") || strstr(line, "/docker-")) {
                    fclose(f);
                    return CONTAINER_TYPE_DOCKER;
                }
                if (strstr(line, "/podman/")) {
                    fclose(f);
                    return CONTAINER_TYPE_PODMAN;
                }
            }
            fclose(f);
        }
    }
    
    return CONTAINER_TYPE_NATIVE;
}

int sm_container_init(void)
{
    if (g_container_system.initialized) {
        return 0;
    }
    
    memset(&g_container_system, 0, sizeof(g_container_system));
    
    if (pthread_rwlock_init(&g_container_system.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "container: pthread_rwlock_init failed: %s", strerror(errno));
        return -1;
    }
    
    /* Detect current container */
    g_container_system.current_container.type = detect_container_runtime();
    
    if (g_container_system.current_container.type != CONTAINER_TYPE_NATIVE) {
        g_container_system.running_in_container = 1;
        sm_log(SM_LOG_INFO, "container: running inside container (type=%d)",
               g_container_system.current_container.type);
    } else {
        sm_log(SM_LOG_INFO, "container: running on native host");
    }
    
    /* Default namespace path */
    strncpy(g_container_system.namespace_path, "/proc", sizeof(g_container_system.namespace_path) - 1);
    
    g_container_system.initialized = 1;
    return 0;
}

int sm_container_set_namespace_path(const char* namespace_path)
{
    if (!namespace_path) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_container_system.lock);
    strncpy(g_container_system.namespace_path, namespace_path,
           sizeof(g_container_system.namespace_path) - 1);
    pthread_rwlock_unlock(&g_container_system.lock);
    
    return 0;
}

int sm_container_register_service(const char* service_name, const char* container_id,
                                  sm_container_type_t container_type, int host_pid)
{
    if (!g_container_system.initialized) {
        sm_log(SM_LOG_ERROR, "container: not initialized");
        return -1;
    }
    
    if (!service_name || !container_id || host_pid <= 0) {
        sm_log(SM_LOG_ERROR, "container: invalid parameters");
        return -1;
    }
    
    pthread_rwlock_wrlock(&g_container_system.lock);
    
    if (g_container_system.mapping_count >= MAX_SERVICE_MAPPINGS) {
        pthread_rwlock_unlock(&g_container_system.lock);
        sm_log(SM_LOG_ERROR, "container: max mappings reached");
        return -1;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < g_container_system.mapping_count; i++) {
        if (strcmp(g_container_system.service_mappings[i].service_name, service_name) == 0) {
            pthread_rwlock_unlock(&g_container_system.lock);
            sm_log(SM_LOG_WARN, "container: duplicate service mapping '%s'", service_name);
            return -1;
        }
    }
    
    service_mapping_t* mapping = &g_container_system.service_mappings[g_container_system.mapping_count++];
    strncpy(mapping->service_name, service_name, sizeof(mapping->service_name) - 1);
    strncpy(mapping->container_id, container_id, sizeof(mapping->container_id) - 1);
    mapping->container_type = container_type;
    mapping->host_pid = host_pid;
    mapping->registration_time = time(NULL);
    
    pthread_rwlock_unlock(&g_container_system.lock);
    
    sm_log(SM_LOG_INFO, "container: registered service '%s' in container '%s' (pid=%d)",
           service_name, container_id, host_pid);
    return 0;
}

int sm_container_unregister_service(const char* service_name)
{
    if (!service_name) return -1;
    
    pthread_rwlock_wrlock(&g_container_system.lock);
    
    for (int i = 0; i < g_container_system.mapping_count; i++) {
        if (strcmp(g_container_system.service_mappings[i].service_name, service_name) == 0) {
            memmove(&g_container_system.service_mappings[i],
                   &g_container_system.service_mappings[i + 1],
                   (g_container_system.mapping_count - i - 1) * sizeof(service_mapping_t));
            g_container_system.mapping_count--;
            pthread_rwlock_unlock(&g_container_system.lock);
            return 0;
        }
    }
    
    pthread_rwlock_unlock(&g_container_system.lock);
    return -1;
}

sm_service_container_mapping_t sm_container_get_service_info(const char* service_name)
{
    sm_service_container_mapping_t mapping = {0};
    
    if (!service_name) return mapping;
    
    pthread_rwlock_rdlock(&g_container_system.lock);
    
    for (int i = 0; i < g_container_system.mapping_count; i++) {
        if (strcmp(g_container_system.service_mappings[i].service_name, service_name) == 0) {
            strncpy(mapping.service_name, g_container_system.service_mappings[i].service_name,
                   sizeof(mapping.service_name) - 1);
            strncpy(mapping.container_id, g_container_system.service_mappings[i].container_id,
                   sizeof(mapping.container_id) - 1);
            mapping.container_type = g_container_system.service_mappings[i].container_type;
            mapping.host_process_id = g_container_system.service_mappings[i].host_pid;
            mapping.registration_time = g_container_system.service_mappings[i].registration_time;
            break;
        }
    }
    
    pthread_rwlock_unlock(&g_container_system.lock);
    return mapping;
}

sm_container_info_t sm_container_get_container_info(const char* container_id)
{
    sm_container_info_t info = {0};
    
    if (!container_id) return info;
    
    pthread_rwlock_rdlock(&g_container_system.lock);
    
    for (int i = 0; i < g_container_system.container_count; i++) {
        if (strcmp(g_container_system.containers[i].container_id, container_id) == 0) {
            strncpy(info.container_id, g_container_system.containers[i].container_id,
                   sizeof(info.container_id) - 1);
            info.type = g_container_system.containers[i].type;
            strncpy(info.container_name, g_container_system.containers[i].container_name,
                   sizeof(info.container_name) - 1);
            info.is_running = g_container_system.containers[i].is_running;
            info.created_time = g_container_system.containers[i].created_time;
            break;
        }
    }
    
    pthread_rwlock_unlock(&g_container_system.lock);
    return info;
}

int sm_container_get_services_in_container(const char* container_id,
                                           char service_list[][64], int max_services)
{
    if (!container_id || !service_list || max_services <= 0) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&g_container_system.lock);
    
    int count = 0;
    for (int i = 0; i < g_container_system.mapping_count && count < max_services; i++) {
        if (strcmp(g_container_system.service_mappings[i].container_id, container_id) == 0) {
            strncpy(service_list[count], g_container_system.service_mappings[i].service_name,
                   63);
            count++;
        }
    }
    
    pthread_rwlock_unlock(&g_container_system.lock);
    return count;
}

int sm_container_get_running_containers(sm_container_info_t* container_list, int max_containers)
{
    if (!container_list || max_containers <= 0) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&g_container_system.lock);
    
    int count = 0;
    for (int i = 0; i < g_container_system.container_count && count < max_containers; i++) {
        if (g_container_system.containers[i].is_running) {
            container_list[count] = sm_container_get_container_info(
                g_container_system.containers[i].container_id);
            count++;
        }
    }
    
    pthread_rwlock_unlock(&g_container_system.lock);
    return count;
}

int sm_container_is_container_alive(const char* container_id)
{
    if (!container_id) return -1;
    
    sm_container_info_t info = sm_container_get_container_info(container_id);
    
    if (strlen(info.container_id) == 0) {
        return -1;  /* Not found */
    }
    
    return info.is_running ? 1 : 0;
}

sm_container_info_t sm_container_get_current_container(void)
{
    sm_container_info_t info;
    
    pthread_rwlock_rdlock(&g_container_system.lock);
    info = g_container_system.current_container;
    pthread_rwlock_unlock(&g_container_system.lock);
    
    return info;
}

int sm_container_enter_container_namespace(const char* container_id)
{
    if (!container_id) {
        return -1;
    }
    
    sm_log(SM_LOG_DEBUG, "container: entering namespace for '%s'", container_id);
    /* Stub: Would use setns() or nsenter approach for actual implementation */
    
    return 0;
}

int sm_container_cleanup(void)
{
    if (!g_container_system.initialized) {
        return 0;
    }
    
    pthread_rwlock_wrlock(&g_container_system.lock);
    memset(&g_container_system, 0, sizeof(g_container_system));
    pthread_rwlock_unlock(&g_container_system.lock);
    pthread_rwlock_destroy(&g_container_system.lock);
    
    sm_log(SM_LOG_INFO, "container: cleanup complete");
    return 0;
}
