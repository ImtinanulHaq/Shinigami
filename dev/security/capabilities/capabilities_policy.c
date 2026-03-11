#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "capabilities_policy.h"
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <grp.h>

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

        /* Populate supplementary hardware-access groups based on service type.
         * This allows services to access /dev/snd (audio gid=29), /dev/video*
         * (video gid=44) etc. after privilege drop.
         *
         * Group IDs are resolved dynamically via getgrnam(); if that fails
         * (e.g. due to NSS/SSSD issues in certain service contexts) we fall
         * back to the standard Linux base-system GIDs so the behaviour is
         * always consistent on a typical Linux desktop/embedded system. */
        struct { const char *name; gid_t fallback_gid; } hw_groups[4] = {
            {NULL, 0}, {NULL, 0}, {NULL, 0}, {NULL, 0}
        };

        if (strcmp(service_name, "audio") == 0) {
            hw_groups[0].name = "audio";   hw_groups[0].fallback_gid = 29;
            hw_groups[1].name = "render";  hw_groups[1].fallback_gid = 992;
        } else if (strcmp(service_name, "camera") == 0) {
            hw_groups[0].name = "video";   hw_groups[0].fallback_gid = 44;
            hw_groups[1].name = "render";  hw_groups[1].fallback_gid = 992;
        } else if (strcmp(service_name, "gpio") == 0) {
            hw_groups[0].name = "gpio";    hw_groups[0].fallback_gid = 0; /* may not exist */
            hw_groups[1].name = "dialout"; hw_groups[1].fallback_gid = 20;
        } else if (strcmp(service_name, "sensor") == 0) {
            hw_groups[0].name = "plugdev"; hw_groups[0].fallback_gid = 46;
        }

        config.supplementary_gid_count = 0;
        for (int i = 0; i < 4 && hw_groups[i].name != NULL; i++) {
            gid_t gid = 0;
            int found = 0;
            struct group *grp = getgrnam(hw_groups[i].name);
            if (grp) {
                gid = grp->gr_gid;
                found = 1;
            } else if (hw_groups[i].fallback_gid != 0) {
                gid = hw_groups[i].fallback_gid;
                found = 1;
                syslog(LOG_WARNING,
                       "[cap_policy] %s: getgrnam(\"%s\") failed, using fallback gid=%d",
                       service_name, hw_groups[i].name, (int)gid);
            } else {
                syslog(LOG_WARNING,
                       "[cap_policy] %s: group '%s' not found, skipping",
                       service_name, hw_groups[i].name);
            }
            if (found && config.supplementary_gid_count < 8) {
                config.supplementary_gids[config.supplementary_gid_count++] = gid;
                syslog(LOG_INFO, "[cap_policy] %s: supplementary group %s(%d)",
                       service_name, hw_groups[i].name, (int)gid);
            }
        }

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
