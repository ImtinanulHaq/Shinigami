#ifndef CAPABILITIES_AUDIT_H
#define CAPABILITIES_AUDIT_H

#include "capabilities.h"
#include <stdint.h>

int capabilities_audit_init(const char* log_path);

void capabilities_audit_cleanup(void);

void capabilities_audit_log(const char* action, cap_flags_t caps, const char* details);

int capabilities_audit_is_enabled(void);

int capabilities_audit_rotate(const char* new_log_path);

#endif
