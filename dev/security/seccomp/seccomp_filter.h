#ifndef SECCOMP_FILTER_H
#define SECCOMP_FILTER_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    SERVICE_TYPE_AUDIO   = 0,
    SERVICE_TYPE_SENSOR  = 1,
    SERVICE_TYPE_CAMERA  = 2,
    SERVICE_TYPE_NETWORK = 3,
    SERVICE_TYPE_MINIMAL = 4,
} service_type_t;

typedef struct {
    service_type_t type;
    int enable_logging;
    int enable_arg_filtering;
    const char** allowed_devices;
    size_t device_count;
} seccomp_config_t;

int seccomp_apply_config(const seccomp_config_t* config);
int seccomp_apply(service_type_t type);
seccomp_config_t seccomp_get_default_config(service_type_t type);
int seccomp_enable_monitoring(const char* log_path);

#endif
