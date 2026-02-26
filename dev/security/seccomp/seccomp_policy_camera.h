#ifndef SECCOMP_POLICY_CAMERA_H
#define SECCOMP_POLICY_CAMERA_H

#include <seccomp.h>
#include <stddef.h>

int seccomp_policy_camera_apply(scmp_filter_ctx ctx);
const char** seccomp_policy_camera_devices(size_t* count);

#endif
