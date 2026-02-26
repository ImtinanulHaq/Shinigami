#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include "../sandbox/sandbox.h"
#include "../capabilities/capabilities.h"
#include "../seccomp/seccomp_filter.h"
#include "../verify/verify.h"

typedef enum {
    SEC_STATE_UNINITIALIZED = 0,
    SEC_STATE_SANDBOX_APPLIED,
    SEC_STATE_CAPABILITIES_APPLIED,
    SEC_STATE_SECCOMP_APPLIED,
    SEC_STATE_VERIFY_INITIALIZED,
    SEC_STATE_FULLY_SECURED
} security_state_t;

typedef struct {
    sandbox_config_t sandbox;
    capabilities_config_t capabilities;
    seccomp_config_t seccomp;
    const char* verify_key_file;
    int skip_sandbox;
    int skip_capabilities;
    int skip_seccomp;
    int skip_verify;
} security_full_config_t;

typedef struct {
    security_state_t state;
    int sandbox_applied;
    int capabilities_applied;
    int seccomp_applied;
    int verify_initialized;
    verify_context_t verify_ctx;
} security_manager_t;

int security_manager_init(security_manager_t* mgr);
int security_manager_apply_all(security_manager_t* mgr, const security_full_config_t* config);
int security_manager_apply_sandbox(security_manager_t* mgr, const sandbox_config_t* cfg);
int security_manager_apply_capabilities(security_manager_t* mgr, const capabilities_config_t* cfg);
int security_manager_apply_seccomp(security_manager_t* mgr, const seccomp_config_t* cfg);
int security_manager_init_verify(security_manager_t* mgr, const char* key_file);
security_state_t security_manager_get_state(const security_manager_t* mgr);
security_full_config_t security_manager_default_config(const char* service_name);
int security_manager_load_config(security_full_config_t* config, const char* config_path);
void security_manager_cleanup(security_manager_t* mgr);

#endif
