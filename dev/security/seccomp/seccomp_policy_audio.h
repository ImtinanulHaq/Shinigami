#ifndef SECCOMP_POLICY_AUDIO_H
#define SECCOMP_POLICY_AUDIO_H

#include <seccomp.h>
#include <stddef.h>

int seccomp_policy_audio_apply(scmp_filter_ctx ctx);
const char** seccomp_policy_audio_devices(size_t* count);

#endif
