#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "seccomp_filter.h"
#include "seccomp_core.h"
#include "seccomp_policy_audio.h"
#include "seccomp_policy_sensor.h"
#include "seccomp_policy_camera.h"
#include "seccomp_policy_network.h"
#include "seccomp_policy_minimal.h"

#include <stdio.h>
#include <seccomp.h>
#include <sys/prctl.h>

int seccomp_enable_monitoring(const char* log_path)
{
    return seccomp_core_setup_monitoring(log_path);
}

seccomp_config_t seccomp_get_default_config(service_type_t type)
{
    seccomp_config_t config = {0};
    config.type = type;
    config.enable_logging = 1;
    config.enable_arg_filtering = 1;

    switch (type) {
        case SERVICE_TYPE_AUDIO:
            config.allowed_devices = seccomp_policy_audio_devices(&config.device_count);
            break;
        case SERVICE_TYPE_CAMERA:
            config.allowed_devices = seccomp_policy_camera_devices(&config.device_count);
            break;
        case SERVICE_TYPE_SENSOR:
            config.allowed_devices = seccomp_policy_sensor_devices(&config.device_count);
            break;
        case SERVICE_TYPE_NETWORK:
            config.allowed_devices = seccomp_policy_network_devices(&config.device_count);
            break;
        case SERVICE_TYPE_MINIMAL:
            config.allowed_devices = NULL;
            config.device_count = 0;
            break;
    }

    return config;
}

int seccomp_apply_config(const seccomp_config_t* config)
{
    if (!config) return -1;

    if (config->enable_logging)
        seccomp_core_setup_violation_handler();

    if (config->enable_arg_filtering)
        seccomp_core_setup_device_fds(config->allowed_devices, config->device_count);

    scmp_filter_ctx ctx = seccomp_init(
        seccomp_core_is_monitoring_enabled() ? SCMP_ACT_TRAP : SCMP_ACT_KILL);
    if (!ctx) return -1;

    if (seccomp_core_validate_architecture(ctx) < 0) {
        seccomp_release(ctx);
        return -1;
    }

    int rc = 0;
    switch (config->type) {
        case SERVICE_TYPE_AUDIO:
            rc = seccomp_policy_audio_apply(ctx);
            break;
        case SERVICE_TYPE_SENSOR:
            rc = seccomp_policy_sensor_apply(ctx);
            break;
        case SERVICE_TYPE_CAMERA:
            rc = seccomp_policy_camera_apply(ctx);
            break;
        case SERVICE_TYPE_NETWORK:
            rc = seccomp_policy_network_apply(ctx);
            break;
        case SERVICE_TYPE_MINIMAL:
            rc = seccomp_policy_minimal_apply(ctx);
            break;
        default:
            seccomp_release(ctx);
            return -1;
    }

    if (rc != 0) {
        seccomp_release(ctx);
        return -1;
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        seccomp_release(ctx);
        return -1;
    }

    seccomp_attr_set(ctx, SCMP_FLTATR_CTL_TSYNC, 1);

    if (seccomp_load(ctx) != 0) {
        seccomp_release(ctx);
        seccomp_core_cleanup_device_fds();
        return -1;
    }

    seccomp_release(ctx);
    seccomp_core_cleanup_device_fds();
    return 0;
}

int seccomp_apply(service_type_t type)
{
    seccomp_config_t config = seccomp_get_default_config(type);
    return seccomp_apply_config(&config);
}
