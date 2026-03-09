/**
 * @file audio_service_security.c
 * @brief Security profile application for the audio service.
 *
 * Calls security_manager_default_config("audio") to obtain the
 * service-specific security configuration, then applies the full
 * security stack in the mandatory order:
 *   Sandbox → Capabilities → Verify → Seccomp
 */

#include "audio_service_security.h"

#include <syslog.h>
#include <string.h>

/* Security module headers */
#include "../../dev/security/core/security_manager.h"

int audio_service_apply_security(audio_service_ctx_t *ctx)
{
    if (!ctx)
        return SVC_ERR_INVALID;

    SVC_INFO("applying security profile for 'audio'");

    /* 1. Obtain the default security config for audio services */
    security_full_config_t sec_cfg = security_manager_default_config("audio");

    /* Allow overriding verify key from service config */
    const char *key_path = config_get_string(&ctx->config, "security",
                                             "verify_key_file", NULL);
    if (key_path) {
        sec_cfg.verify_key_file = key_path;
        sec_cfg.skip_verify     = 0;
    }

    /* Allow skipping individual layers via config (for development) */
    sec_cfg.skip_sandbox = config_get_bool(&ctx->config, "security",
                                           "skip_sandbox",
                                           sec_cfg.skip_sandbox);
    sec_cfg.skip_capabilities = config_get_bool(&ctx->config, "security",
                                                "skip_capabilities",
                                                sec_cfg.skip_capabilities);
    sec_cfg.skip_seccomp = config_get_bool(&ctx->config, "security",
                                           "skip_seccomp",
                                           sec_cfg.skip_seccomp);
    sec_cfg.skip_verify = config_get_bool(&ctx->config, "security",
                                          "skip_verify",
                                          sec_cfg.skip_verify);

    /* 2. Initialise the security manager and apply all layers */
    security_manager_t sec_mgr;
    int rc = security_manager_init(&sec_mgr);
    if (rc != 0) {
        SVC_ERR("security_manager_init failed: %d", rc);
        return SVC_ERR_SECURITY;
    }

    /*
     * security_manager_apply_all() enforces the correct ordering:
     *   Sandbox → Capabilities → Verify → Seccomp
     */
    rc = security_manager_apply_all(&sec_mgr, &sec_cfg);
    if (rc != 0) {
        SVC_ERR("security_manager_apply_all failed: %d", rc);
        security_manager_cleanup(&sec_mgr);
        return SVC_ERR_SECURITY;
    }

    ctx->security_applied = 1;
    SVC_INFO("security profile applied successfully (state=%d)",
             security_manager_get_state(&sec_mgr));

    /* Note: we intentionally do NOT call security_manager_cleanup here
     * because the security constraints must remain active for the
     * lifetime of the daemon process. */

    return SVC_OK;
}
