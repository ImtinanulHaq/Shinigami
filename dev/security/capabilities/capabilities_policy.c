#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "capabilities_policy.h"
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>

static const service_capabilities_t builtin_policies[] = {
    {
        .service_name = "audio",
        .required_caps = MCAP_SYS_RAWIO,
        .optional_caps = MCAP_NONE,
        .recommended_uid = 1001,
        .recommended_gid = 1001
    },
    {
        .service_name = "camera",
        .required_caps = MCAP_SYS_RAWIO,
        .optional_caps = MCAP_NONE,
        .recommended_uid = 1002,
        .recommended_gid = 1002
    },
    {
        .service_name = "sensor",
        .required_caps = MCAP_NONE,
        .optional_caps = MCAP_SYS_RAWIO,
        .recommended_uid = 1003,
        .recommended_gid = 1003
    },
    {
        .service_name = "network",
        .required_caps = MCAP_NET_BIND | MCAP_NET_RAW,
        .optional_caps = MCAP_SYS_ADMIN,
        .recommended_uid = 1004,
        .recommended_gid = 1004
    },
    {
        .service_name = "gpio",
        .required_caps = MCAP_SYS_RAWIO,
        .optional_caps = MCAP_NONE,
        .recommended_uid = 1005,
        .recommended_gid = 1005
    },
    {NULL, 0, 0, 0, 0}
};

#define MAX_DYNAMIC_POLICIES 32
static service_capabilities_t dynamic_policies[MAX_DYNAMIC_POLICIES];
static int dynamic_policy_count = 0;
static pthread_mutex_t policy_lock = PTHREAD_MUTEX_INITIALIZER;

const service_capabilities_t* capabilities_policy_lookup(const char* service_name)
{
    if (!service_name) return NULL;

    for (int i = 0; builtin_policies[i].service_name != NULL; i++) {
        if (strcmp(builtin_policies[i].service_name, service_name) == 0) {
            return &builtin_policies[i];
        }
    }

    pthread_mutex_lock(&policy_lock);
    for (int i = 0; i < dynamic_policy_count; i++) {
        if (strcmp(dynamic_policies[i].service_name, service_name) == 0) {
            const service_capabilities_t* result = &dynamic_policies[i];
            pthread_mutex_unlock(&policy_lock);
            return result;
        }
    }
    pthread_mutex_unlock(&policy_lock);

    return NULL;
}

capabilities_config_t capabilities_policy_get_config(const char* service_name)
{
    capabilities_config_t config = {0};

    const service_capabilities_t* policy = capabilities_policy_lookup(service_name);

    if (policy) {

        config.effective = policy->required_caps;
        config.permitted = policy->required_caps | policy->optional_caps;
        config.inheritable = MCAP_NONE;
        config.bounding = policy->required_caps | policy->optional_caps;
        config.enable_auditing = 1;
        config.target_uid = policy->recommended_uid;
        config.target_gid = policy->recommended_gid;
    } else {

        syslog(LOG_WARNING, "[cap_policy] Unknown service '%s', using minimal privileges",
               service_name);
        config.effective = MCAP_NONE;
        config.permitted = MCAP_NONE;
        config.inheritable = MCAP_NONE;
        config.bounding = MCAP_NONE;
        config.enable_auditing = 1;
        config.target_uid = 65534;
        config.target_gid = 65534;
    }

    return config;
}

const service_capabilities_t* capabilities_policy_get_all(int* count)
{
    if (count) {
        int builtin_count = 0;
        while (builtin_policies[builtin_count].service_name != NULL) {
            builtin_count++;
        }
        *count = builtin_count;
    }
    return builtin_policies;
}

int capabilities_policy_register(const service_capabilities_t* policy)
{
    if (!policy || !policy->service_name) {
        return -1;
    }

    pthread_mutex_lock(&policy_lock);

    if (dynamic_policy_count >= MAX_DYNAMIC_POLICIES) {
        syslog(LOG_ERR, "[cap_policy] Dynamic policy table full");
        pthread_mutex_unlock(&policy_lock);
        return -1;
    }

    for (int i = 0; i < dynamic_policy_count; i++) {
        if (strcmp(dynamic_policies[i].service_name, policy->service_name) == 0) {
            syslog(LOG_WARNING, "[cap_policy] Policy for '%s' already exists",
                   policy->service_name);
            pthread_mutex_unlock(&policy_lock);
            return -1;
        }
    }

    dynamic_policies[dynamic_policy_count] = *policy;
    dynamic_policy_count++;

    pthread_mutex_unlock(&policy_lock);

    syslog(LOG_INFO, "[cap_policy] Registered policy for service '%s'",
           policy->service_name);
    return 0;
}
