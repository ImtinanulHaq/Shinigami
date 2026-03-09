/**
 * @file sensor_service_security.c
 * @brief Security profile application for the sensor service.
 *
 * Mandatory application order: Sandbox → Capabilities → Verify → Seccomp
 */

#include "sensor_service_security.h"

#include <syslog.h>
#include <string.h>

#include "../../dev/security/core/security_manager.h"

int sensor_service_apply_security(sensor_service_ctx_t *ctx)
{
    if (!ctx) return SVC_ERR_INVALID;

    SVC_INFO("applying security profile for 'sensor'");

    security_full_config_t sec_cfg = security_manager_default_config("sensor");

    const char *key_path = service_config_get_string(&ctx->config, "security",
                                             "verify_key_file", NULL);
    if (key_path) {
        sec_cfg.verify_key_file = key_path;
        sec_cfg.skip_verify     = 0;
    }

    sec_cfg.skip_sandbox = service_config_get_bool(&ctx->config, "security",
                                           "skip_sandbox", sec_cfg.skip_sandbox);
    sec_cfg.skip_capabilities = service_config_get_bool(&ctx->config, "security",
                                                "skip_capabilities",
                                                sec_cfg.skip_capabilities);
    sec_cfg.skip_seccomp = service_config_get_bool(&ctx->config, "security",
                                           "skip_seccomp", sec_cfg.skip_seccomp);
    sec_cfg.skip_verify = service_config_get_bool(&ctx->config, "security",
                                          "skip_verify", sec_cfg.skip_verify);

    security_manager_t sec_mgr;
    int rc = security_manager_init(&sec_mgr);
    if (rc != 0) {
        SVC_ERR("security_manager_init failed: %d", rc);
        return SVC_ERR_SECURITY;
    }

    rc = security_manager_apply_all(&sec_mgr, &sec_cfg);
    if (rc != 0) {
        SVC_ERR("security_manager_apply_all failed: %d", rc);
        security_manager_cleanup(&sec_mgr);
        return SVC_ERR_SECURITY;
    }

    ctx->security_applied = 1;
    SVC_INFO("sensor security profile applied (state=%d)",
             security_manager_get_state(&sec_mgr));
    return SVC_OK;
}
