#ifndef SANDBOX_CORE_H
#define SANDBOX_CORE_H

#include "sandbox.h"

int sandbox_core_validate_config(const sandbox_config_t* cfg);
int sandbox_core_apply_namespaces(const sandbox_config_t* cfg);
int sandbox_core_setup_user_namespace(uid_t real_uid, gid_t real_gid);
sandbox_config_t sandbox_core_default_config(void);
sandbox_config_t sandbox_core_get_service_config(const char* service_name);

int sandbox_core_create_persistent_namespace(const char* name, int ns_types);
int sandbox_core_join_persistent_namespace(const char* name);
int sandbox_core_destroy_persistent_namespace(const char* name);

#endif
