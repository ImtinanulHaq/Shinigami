#define _POSIX_C_SOURCE 200809L
#include "seccomp_policy_camera.h"
#include "seccomp_core.h"

static const char* camera_devices[] = {
    "/dev/video0",
    "/dev/video1"
};

const char** seccomp_policy_camera_devices(size_t* count)
{
    if (count) *count = sizeof(camera_devices) / sizeof(camera_devices[0]);
    return camera_devices;
}

int seccomp_policy_camera_apply(scmp_filter_ctx ctx)
{
    seccomp_core_apply_common(ctx);

    if (seccomp_core_add_ioctl_filter(ctx) < 0) return -1;

    seccomp_core_allow(ctx, SCMP_SYS(mlock));
    seccomp_core_allow(ctx, SCMP_SYS(munlock));

    return 0;
}
