#ifndef SM_DEPENDENCIES_H
#define SM_DEPENDENCIES_H

/*
 * sm_dependencies.h - Service dependency management
 *
 * Services can declare dependencies on other services.
 * Registry tracks dependency graph and validates consistency.
 */

#include <sys/types.h>

/* Max dependencies per service */
#define SM_MAX_DEPS 8

typedef struct {
    char service_name[64];
    char depends_on[SM_MAX_DEPS][64];
    int  dep_count;
} service_deps_t;

/* Register service with dependencies */
int sm_deps_register(const char* name, const char** dependencies, int dep_count);

/* Check if all dependencies are satisfied */
int sm_deps_check_satisfied(const char* name);

/* Detect circular dependencies */
int sm_deps_detect_circular(void);

/* Get ordered list for startup (topological sort) */
int sm_deps_get_startup_order(char (*services)[64], int* count);

/* Update dependency status */
int sm_deps_service_ready(const char* name);

/* Cleanup */
void sm_deps_cleanup(void);

#endif /* SM_DEPENDENCIES_H */
