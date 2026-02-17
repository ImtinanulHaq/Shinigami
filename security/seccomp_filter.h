#ifndef SECCOMP_FILTER_H
#define SECCOMP_FILTER_H

// seccomp = syscall firewall
// each service gets ONLY the syscalls it needs
// any other syscall = process killed immediately

// service types — each gets different syscall whitelist
typedef enum {
    SERVICE_TYPE_AUDIO  = 0,
    SERVICE_TYPE_SENSOR = 1,
    SERVICE_TYPE_CAMERA = 2,
} service_type_t;

// apply seccomp filter for given service type
// call this ONCE at service startup — cannot be undone
// returns 0 on success, -1 on failure
int seccomp_apply(service_type_t type);

#endif // SECCOMP_FILTER_H