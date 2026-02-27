#ifndef SECCOMP_POLICY_NETWORK_H
#define SECCOMP_POLICY_NETWORK_H

#include "seccomp_core.h"

int seccomp_policy_network_apply(scmp_filter_ctx ctx);
const char** seccomp_policy_network_devices(size_t* count);

#endif
