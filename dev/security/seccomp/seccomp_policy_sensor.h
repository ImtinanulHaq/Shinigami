#ifndef SECCOMP_POLICY_SENSOR_H
#define SECCOMP_POLICY_SENSOR_H

#include <seccomp.h>
#include <stddef.h>

int seccomp_policy_sensor_apply(scmp_filter_ctx ctx);
const char** seccomp_policy_sensor_devices(size_t* count);

#endif
