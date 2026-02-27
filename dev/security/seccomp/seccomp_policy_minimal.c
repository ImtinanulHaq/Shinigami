#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "seccomp_policy_minimal.h"

int seccomp_policy_minimal_apply(scmp_filter_ctx ctx)
{
    if (seccomp_core_apply_common(ctx) < 0) return -1;

    return 0;
}
