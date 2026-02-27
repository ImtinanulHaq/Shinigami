#define _POSIX_C_SOURCE 200809L
#include "security_manager.h"
#include "sec_error.h"
#include "../seccomp/seccomp_core.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>

int security_manager_init(security_manager_t* mgr)
{
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(*mgr));
    mgr->state = SEC_STATE_UNINITIALIZED;
    return 0;
}

int security_manager_apply_sandbox(security_manager_t* mgr, const sandbox_config_t* cfg)
{
    if (!mgr || !cfg) return -1;

    if (mgr->seccomp_applied) return -1;

    if (sandbox_apply_config(cfg) < 0) return -1;

    mgr->sandbox_applied = 1;
    mgr->state = SEC_STATE_SANDBOX_APPLIED;
    return 0;
}

int security_manager_apply_capabilities(security_manager_t* mgr, const capabilities_config_t* cfg)
{
    if (!mgr || !cfg) return -1;

    if (mgr->seccomp_applied) return -1;

    if (capabilities_apply_config(cfg) < 0) return -1;

    mgr->capabilities_applied = 1;
    mgr->state = SEC_STATE_CAPABILITIES_APPLIED;
    return 0;
}

int security_manager_apply_seccomp(security_manager_t* mgr, const seccomp_config_t* cfg)
{
    if (!mgr || !cfg) return -1;

    if (seccomp_apply_config(cfg) < 0) return -1;

    mgr->seccomp_applied = 1;
    mgr->state = SEC_STATE_SECCOMP_APPLIED;
    return 0;
}

int security_manager_init_verify(security_manager_t* mgr, const char* key_file)
{
    if (!mgr || !key_file) return -1;

    if (verify_init_from_file(&mgr->verify_ctx, key_file) < 0) return -1;

    mgr->verify_initialized = 1;
    mgr->state = SEC_STATE_VERIFY_INITIALIZED;
    return 0;
}

int security_manager_apply_all(security_manager_t* mgr, const security_full_config_t* config)
{
    if (!mgr || !config) return -1;

    if (!config->skip_sandbox) {
        if (security_manager_apply_sandbox(mgr, &config->sandbox) < 0) {
            syslog(LOG_ERR, "[sec_mgr] sandbox application failed");
            goto rollback;
        }
    }

    if (!config->skip_capabilities) {
        if (security_manager_apply_capabilities(mgr, &config->capabilities) < 0) {
            syslog(LOG_ERR, "[sec_mgr] capabilities application failed");
            goto rollback;
        }
    }

    if (!config->skip_verify && config->verify_key_file) {
        if (security_manager_init_verify(mgr, config->verify_key_file) < 0) {
            syslog(LOG_ERR, "[sec_mgr] verify initialization failed");
            goto rollback;
        }
    }

    if (!config->skip_seccomp) {
        if (security_manager_apply_seccomp(mgr, &config->seccomp) < 0) {
            syslog(LOG_ERR, "[sec_mgr] seccomp application failed");
            goto rollback;
        }
    }

    mgr->state = SEC_STATE_FULLY_SECURED;
    return 0;

rollback:
    syslog(LOG_WARNING, "[sec_mgr] rolling back partial security state");

    if (mgr->verify_initialized) {
        verify_cleanup(&mgr->verify_ctx);
        mgr->verify_initialized = 0;
    }

    if (mgr->sandbox_applied && config->sandbox.enable_cgroups && config->sandbox.cgroup_name) {
        sandbox_destroy_cgroup(config->sandbox.cgroup_name);
    }

    mgr->state = SEC_STATE_UNINITIALIZED;
    mgr->sandbox_applied = 0;
    mgr->capabilities_applied = 0;
    mgr->seccomp_applied = 0;
    return -1;
}

security_state_t security_manager_get_state(const security_manager_t* mgr)
{
    if (!mgr) return SEC_STATE_UNINITIALIZED;
    return mgr->state;
}

security_full_config_t security_manager_default_config(const char* service_name)
{
    security_full_config_t config;
    memset(&config, 0, sizeof(config));

    config.sandbox = sandbox_get_service_config(service_name);
    config.capabilities = capabilities_get_service_config(service_name);

    if (strcmp(service_name, "audio") == 0)
        config.seccomp = seccomp_get_default_config(SERVICE_TYPE_AUDIO);
    else if (strcmp(service_name, "camera") == 0)
        config.seccomp = seccomp_get_default_config(SERVICE_TYPE_CAMERA);
    else if (strcmp(service_name, "sensor") == 0)
        config.seccomp = seccomp_get_default_config(SERVICE_TYPE_SENSOR);
    else if (strcmp(service_name, "network") == 0)
        config.seccomp = seccomp_get_default_config(SERVICE_TYPE_NETWORK);
    else {
        syslog(LOG_WARNING, "[sec_mgr] unknown service '%s', using minimal seccomp profile",
               service_name);
        config.seccomp = seccomp_get_default_config(SERVICE_TYPE_MINIMAL);
    }

    config.verify_key_file = NULL;
    config.skip_sandbox = 0;
    config.skip_capabilities = 0;
    config.skip_seccomp = 0;
    config.skip_verify = 1;

    return config;
}

int security_manager_load_config(security_full_config_t* config, const char* config_path)
{
    if (!config || !config_path) return -1;

    FILE* f = fopen(config_path, "r");
    if (!f) {
        syslog(LOG_ERR, "[sec_mgr] cannot open config '%s': %s",
               config_path, strerror(errno));
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        char key[64], value[128];
        if (sscanf(line, "%63[^=]=%127[^\n]", key, value) != 2) continue;

        if (strcmp(key, "skip_sandbox") == 0)
            config->skip_sandbox = atoi(value);
        else if (strcmp(key, "skip_capabilities") == 0)
            config->skip_capabilities = atoi(value);
        else if (strcmp(key, "skip_seccomp") == 0)
            config->skip_seccomp = atoi(value);
        else if (strcmp(key, "skip_verify") == 0)
            config->skip_verify = atoi(value);
        else if (strcmp(key, "verify_key_file") == 0) {
            static char key_path[128];
            strncpy(key_path, value, sizeof(key_path) - 1);
            key_path[sizeof(key_path) - 1] = '\0';
            config->verify_key_file = key_path;
        }
        else if (strcmp(key, "memory_limit_mb") == 0)
            config->sandbox.limits.memory_limit_mb = (uint64_t)atol(value);
        else if (strcmp(key, "cpu_quota_percent") == 0)
            config->sandbox.limits.cpu_quota_percent = (uint32_t)atoi(value);
        else if (strcmp(key, "pids_limit") == 0)
            config->sandbox.limits.pids_limit = (uint32_t)atoi(value);
    }

    fclose(f);
    syslog(LOG_INFO, "[sec_mgr] loaded config from '%s'", config_path);
    return 0;
}

void security_manager_cleanup(security_manager_t* mgr)
{
    if (!mgr) return;

    if (mgr->verify_initialized)
        verify_cleanup(&mgr->verify_ctx);

    seccomp_core_cleanup_device_fds();
    seccomp_core_cleanup_monitoring();

    memset(mgr, 0, sizeof(*mgr));
    mgr->state = SEC_STATE_UNINITIALIZED;
}
