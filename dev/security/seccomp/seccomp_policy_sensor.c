#define _POSIX_C_SOURCE 200809L
#include "seccomp_policy_sensor.h"
#include "seccomp_core.h"

static const char* sensor_devices[] = {
    "/dev/iio:device0",
    "/sys/bus/iio/devices/iio:device0"
};

const char** seccomp_policy_sensor_devices(size_t* count)
{
    if (count) *count = sizeof(sensor_devices) / sizeof(sensor_devices[0]);
    return sensor_devices;
}

int seccomp_policy_sensor_apply(scmp_filter_ctx ctx)
{
    seccomp_core_apply_common(ctx);
    return seccomp_core_add_ioctl_filter(ctx);
}
