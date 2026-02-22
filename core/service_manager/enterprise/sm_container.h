/*
 * sm_container.h - Container support for Docker and LXC
 *
 * Track services running in containers and manage their lifecycle.
 * Supports Docker, LXC, and systemd containers.
 * 
 * USAGE:
 *   sm_container_init();
 *   sm_container_register_service(service_name, container_id, container_type);
 *   sm_container_get_services_in_container(container_id);
 *   sm_container_cleanup();
 */

#ifndef SM_CONTAINER_H
#define SM_CONTAINER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Container runtime types */
typedef enum {
    CONTAINER_TYPE_DOCKER = 1,
    CONTAINER_TYPE_LXC = 2,
    CONTAINER_TYPE_SYSTEMD = 3,
    CONTAINER_TYPE_PODMAN = 4,
    CONTAINER_TYPE_NATIVE = 5,  /* Not in container */
} sm_container_type_t;

/* Container info */
typedef struct {
    char container_id[128];
    sm_container_type_t type;
    char container_name[128];
    int is_running;
    uint64_t created_time;
    uint64_t pid_namespace;
    uint64_t network_namespace;
} sm_container_info_t;

/* Service in container */
typedef struct {
    char service_name[64];
    char container_id[128];
    sm_container_type_t container_type;
    int container_process_id;  /* PID within container */
    int host_process_id;       /* PID on host */
    int priority;
    uint64_t registration_time;
} sm_service_container_mapping_t;

/*
 * sm_container_init()
 * 
 * Initialize container support system.
 * Auto-detects container runtime if running inside container.
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_container_init(void);

/*
 * sm_container_use_namespace_path(namespace_path)
 * 
 * Explicitly set path to container namespaces
 * (e.g., /var/run/netns/, /proc/PID/ns/, etc for monitoring)
 * 
 * PARAMETERS:
 *   namespace_path - path to namespace directory
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_container_set_namespace_path(const char* namespace_path);

/*
 * sm_container_register_service(service_name, container_id, container_type, host_pid)
 * 
 * Register a service running in a container.
 * 
 * PARAMETERS:
 *   service_name - service identifier
 *   container_id - container ID (short or long Docker ID, LXC name, etc)
 *   container_type - CONTAINER_TYPE_DOCKER, CONTAINER_TYPE_LXC, etc
 *   host_pid - service process ID on host system
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (container not found, invalid type)
 */
int sm_container_register_service(const char* service_name, const char* container_id,
                                  sm_container_type_t container_type, int host_pid);

/*
 * sm_container_unregister_service(service_name)
 * 
 * Unregister a service from container tracking.
 * 
 * RETURNS:
 *   0 on success
 *   -1 if not found
 */
int sm_container_unregister_service(const char* service_name);

/*
 * sm_container_get_service_info(service_name)
 * 
 * Get container information for a service.
 * 
 * RETURNS:
 *   Service-container mapping (or zero-filled if not found)
 */
sm_service_container_mapping_t sm_container_get_service_info(const char* service_name);

/*
 * sm_container_get_container_info(container_id)
 * 
 * Get container runtime information.
 * 
 * RETURNS:
 *   Container info (or zero-filled if not found)
 */
sm_container_info_t sm_container_get_container_info(const char* container_id);

/*
 * sm_container_get_services_in_container(container_id, service_list, max_services)
 * 
 * Get all services running in a specific container.
 * 
 * PARAMETERS:
 *   container_id - container identifier
 *   service_list - output array for service names
 *   max_services - max services to return
 * 
 * RETURNS:
 *   Number of services found in container
 */
int sm_container_get_services_in_container(const char* container_id,
                                           char service_list[][64], int max_services);

/*
 * sm_container_get_running_containers(container_list, max_containers)
 * 
 * List all containers that are currently running.
 * 
 * RETURNS:
 *   Number of running containers
 */
int sm_container_get_running_containers(sm_container_info_t* container_list, int max_containers);

/*
 * sm_container_is_container_alive(container_id)
 * 
 * Check if a container is running.
 * 
 * RETURNS:
 *   1 if running, 0 if not, -1 if error
 */
int sm_container_is_container_alive(const char* container_id);

/*
 * sm_container_get_current_container()
 * 
 * Get container info for current process (if running in a container).
 * 
 * RETURNS:
 *   Container info (zero-filled if not in container)
 */
sm_container_info_t sm_container_get_current_container(void);

/*
 * sm_container_enter_container_namespace(container_id)
 * 
 * Use nsenter-like approach to access container resources
 * for monitoring/debugging purposes.
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_container_enter_container_namespace(const char* container_id);

/*
 * sm_container_cleanup()
 * 
 * Shutdown container support system.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_container_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_CONTAINER_H */
