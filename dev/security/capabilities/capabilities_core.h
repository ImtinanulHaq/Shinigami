#ifndef CAPABILITIES_CORE_H
#define CAPABILITIES_CORE_H

#include "capabilities.h"

int capabilities_core_apply_config(const capabilities_config_t* config);

int capabilities_core_drop_except(cap_flags_t keep_flags)
    __attribute__((deprecated("Use capabilities_core_apply_config instead")));

int capabilities_core_drop_all(void);

int capabilities_core_check(cap_flags_t cap);

const char* capabilities_core_name(cap_flags_t cap);

int capabilities_core_set_ambient(cap_flags_t caps);

#endif
