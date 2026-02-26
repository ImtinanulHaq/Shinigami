#define _POSIX_C_SOURCE 200809L
#include "seccomp_policy_audio.h"
#include "seccomp_core.h"

static const char* audio_devices[] = {
    "/dev/snd/controlC0",
    "/dev/snd/pcmC0D0p"
};

const char** seccomp_policy_audio_devices(size_t* count)
{
    if (count) *count = sizeof(audio_devices) / sizeof(audio_devices[0]);
    return audio_devices;
}

int seccomp_policy_audio_apply(scmp_filter_ctx ctx)
{
    seccomp_core_apply_common(ctx);
    return seccomp_core_add_ioctl_filter(ctx);
}
