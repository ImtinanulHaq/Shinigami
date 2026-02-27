#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "seccomp_policy_network.h"

static const char* network_devices[] = {
    "/dev/net/tun",
    NULL
};

const char** seccomp_policy_network_devices(size_t* count)
{
    if (count) *count = 1;
    return network_devices;
}

int seccomp_policy_network_apply(scmp_filter_ctx ctx)
{
    if (seccomp_core_apply_common(ctx) < 0) return -1;

    if (seccomp_core_allow_networking(ctx) < 0) return -1;

    seccomp_core_allow(ctx, SCMP_SYS(ioctl));
    seccomp_core_allow(ctx, SCMP_SYS(setsockopt));
    seccomp_core_allow(ctx, SCMP_SYS(getsockopt));
    seccomp_core_allow(ctx, SCMP_SYS(poll));
    seccomp_core_allow(ctx, SCMP_SYS(ppoll));
    seccomp_core_allow(ctx, SCMP_SYS(select));

    return 0;
}
