#define _POSIX_C_SOURCE 200809L

/*
 * sm_dependencies.c - Service dependency graph management
 */

#include "../lifecycle/sm_dependencies.h"
#include "../observability/sm_logging.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_SERVICES_TRACKED 32
#define MAX_RECURSION_DEPTH 10

static struct {
    service_deps_t deps[MAX_SERVICES_TRACKED];
    int count;
    pthread_mutex_t mutex;
} g_deps = { .mutex = PTHREAD_MUTEX_INITIALIZER };  /* Static initialization */

int sm_deps_register(const char* name, const char** dependencies, int dep_count)
{
    if (!name || dep_count < 0 || dep_count > SM_MAX_DEPS) {
        return -1;
    }
    
    pthread_mutex_lock(&g_deps.mutex);
    
    /* Check if already registered */
    for (int i = 0; i < g_deps.count; i++) {
        if (!strcmp(g_deps.deps[i].service_name, name)) {
            pthread_mutex_unlock(&g_deps.mutex);
            return -1;  /* already exists */
        }
    }
    
    if (g_deps.count >= MAX_SERVICES_TRACKED) {
        pthread_mutex_unlock(&g_deps.mutex);
        return -1;  /* table full */
    }
    
    service_deps_t* d = &g_deps.deps[g_deps.count++];
    strncpy(d->service_name, name, sizeof(d->service_name) - 1);
    d->dep_count = dep_count;
    
    for (int i = 0; i < dep_count; i++) {
        strncpy(d->depends_on[i], dependencies[i], sizeof(d->depends_on[i]) - 1);
    }
    
    pthread_mutex_unlock(&g_deps.mutex);
    
    sm_log(SM_LOG_INFO, "deps: registered '%s' with %d dependencies", name, dep_count);
    return 0;
}

int sm_deps_check_satisfied(const char* name)
{
    if (!name) return -1;
    
    pthread_mutex_lock(&g_deps.mutex);
    
    for (int i = 0; i < g_deps.count; i++) {
        if (!strcmp(g_deps.deps[i].service_name, name)) {
            /* All dependencies should be satisfied */
            int all_satisfied = 1;
            for (int j = 0; j < g_deps.deps[i].dep_count; j++) {
                int found = 0;
                for (int k = 0; k < g_deps.count; k++) {
                    if (!strcmp(g_deps.deps[k].service_name, g_deps.deps[i].depends_on[j])) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    all_satisfied = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&g_deps.mutex);
            return all_satisfied ? 0 : 1;
        }
    }
    pthread_mutex_unlock(&g_deps.mutex);
    return 0;  /* service not tracked */
}

static int has_circular_visit(const char* name, int* visited, int* rec_stack, int depth)
{
    /* Prevent stack overflow from deep circular dependencies */
    if (depth > MAX_RECURSION_DEPTH) {
        sm_log(SM_LOG_ERROR, "deps: recursion depth limit exceeded");
        return 1;  /* treat as cycle */
    }
    
    for (int i = 0; i < g_deps.count; i++) {
        if (!strcmp(g_deps.deps[i].service_name, name)) {
            visited[i] = 1;
            rec_stack[i] = 1;
            
            for (int j = 0; j < g_deps.deps[i].dep_count; j++) {
                for (int k = 0; k < g_deps.count; k++) {
                    if (!strcmp(g_deps.deps[k].service_name, g_deps.deps[i].depends_on[j])) {
                        if (!visited[k]) {
                            if (has_circular_visit(g_deps.deps[k].service_name, visited, rec_stack, depth + 1)) {
                                return 1;
                            }
                        } else if (rec_stack[k]) {
                            return 1;  /* cycle detected */
                        }
                        break;
                    }
                }
            }
            
            rec_stack[i] = 0;
            return 0;
        }
    }
    return 0;
}

int sm_deps_detect_circular(void)
{
    int visited[MAX_SERVICES_TRACKED] = {0};
    int rec_stack[MAX_SERVICES_TRACKED] = {0};
    
    pthread_mutex_lock(&g_deps.mutex);
    
    for (int i = 0; i < g_deps.count; i++) {
        if (!visited[i]) {
            if (has_circular_visit(g_deps.deps[i].service_name, visited, rec_stack, 0)) {
                pthread_mutex_unlock(&g_deps.mutex);
                sm_log(SM_LOG_ERROR, "deps: circular dependency detected");
                return 1;
            }
        }
    }
    
    pthread_mutex_unlock(&g_deps.mutex);
    return 0;
}

int sm_deps_get_startup_order(char (*services)[64], int* count)
{
    if (!services || !count) return -1;
    
    int ordered = 0;
    int visited[MAX_SERVICES_TRACKED] = {0};
    
    /* Simple topological sort: services with no dependencies first */
    for (int round = 0; round < g_deps.count; round++) {
        for (int i = 0; i < g_deps.count; i++) {
            if (visited[i]) continue;
            
            int deps_satisfied = 1;
            for (int j = 0; j < g_deps.deps[i].dep_count; j++) {
                int found = 0;
                for (int k = 0; k < i; k++) {
                    if (visited[k] && !strcmp(services[k], g_deps.deps[i].depends_on[j])) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    deps_satisfied = 0;
                    break;
                }
            }
            
            if (deps_satisfied && ordered < *count) {
                strncpy(services[ordered], g_deps.deps[i].service_name, 63);
                visited[i] = 1;
                ordered++;
            }
        }
    }
    
    *count = ordered;
    return ordered == g_deps.count ? 0 : -1;
}

int sm_deps_service_ready(const char* name)
{
    sm_log(SM_LOG_INFO, "deps: service '%s' is ready", name);
    return 0;
}

void sm_deps_cleanup(void)
{
    pthread_mutex_lock(&g_deps.mutex);
    memset(&g_deps, 0, sizeof(g_deps));
    pthread_mutex_unlock(&g_deps.mutex);
}
