#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "capabilities.h"
#include "capabilities_core.h"
#include "capabilities_audit.h"
#include "capabilities_policy.h"

int capabilities_apply_config(const capabilities_config_t* config)
{

    return capabilities_core_apply_config(config);
}

capabilities_config_t capabilities_get_service_config(const char* service_name)
{

    return capabilities_policy_get_config(service_name);
}

int capabilities_enable_auditing(const char* log_path)
{

    return capabilities_audit_init(log_path);
}

int capabilities_check(cap_flags_t cap)
{

    return capabilities_core_check(cap);
}

const char* capabilities_name(cap_flags_t cap)
{

    return capabilities_core_name(cap);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
int capabilities_drop_except(cap_flags_t keep_flags)
{
    return capabilities_core_drop_except(keep_flags);
}
#pragma GCC diagnostic pop

int capabilities_drop_all(void)
{

    return capabilities_core_drop_all();
}
