#ifndef CAPABILITIES_POLICY_H
#define CAPABILITIES_POLICY_H

#include "capabilities.h"

capabilities_config_t capabilities_policy_get_config(const char* service_name);

const service_capabilities_t* capabilities_policy_lookup(const char* service_name);

const service_capabilities_t* capabilities_policy_get_all(int* count);

int capabilities_policy_register(const service_capabilities_t* policy);

#endif
