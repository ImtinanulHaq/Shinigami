#ifndef SECCOMP_FILTER_H
#define SECCOMP_FILTER_H

#include <stdint.h>
#include <stddef.h>

// seccomp = syscall firewall
// each service gets ONLY the syscalls it needs
// any other syscall = process killed immediately

// service types — each gets different syscall whitelist
typedef enum {
    SERVICE_TYPE_AUDIO  = 0,
    SERVICE_TYPE_SENSOR = 1,
    SERVICE_TYPE_CAMERA = 2,
} service_type_t;

// seccomp configuration for fine-grained control
typedef struct {
    service_type_t type;
    int enable_logging;         // log violation attempts
    int enable_arg_filtering;   // restrict syscall arguments
    const char** allowed_devices; // device paths for ioctl restriction
    size_t device_count;
} seccomp_config_t;

// apply seccomp filter with configuration
// call this ONCE at service startup — cannot be undone
// returns 0 on success, -1 on failure
int seccomp_apply_config(const seccomp_config_t* config);

// simplified interface - uses default config for service type
int seccomp_apply(service_type_t type);

// get default configuration for a service type
seccomp_config_t seccomp_get_default_config(service_type_t type);

// enable runtime monitoring (must be called before seccomp_apply)
int seccomp_enable_monitoring(const char* log_path);

#endif // SECCOMP_FILTER_H