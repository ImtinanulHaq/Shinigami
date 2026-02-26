#ifndef SECCOMP_POLICY_MINIMAL_H
#define SECCOMP_POLICY_MINIMAL_H

#include "seccomp_core.h"

int seccomp_policy_minimal_apply(scmp_filter_ctx ctx);

#endif
