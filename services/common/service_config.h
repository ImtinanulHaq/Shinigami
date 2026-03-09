/**
 * @file service_config.h
 * @brief INI config loader with environment-variable override support.
 *
 * Config files follow INI format:
 *   [section]
 *   key = value   ; inline comment after semicolon
 *
 * After loading, env-var overrides in the form
 *   <SERVICE_NAME_UPPER>_<SECTION_UPPER>_<KEY_UPPER>
 * are applied automatically.  For example:
 *   AUDIO_SERVICE_SERVER_SOCKET_PATH=... overrides [server] socket_path.
 *
 * Validation helpers let the caller assert required keys are present with
 * meaningful error messages before the daemon proceeds to daemonize.
 */

#ifndef SERVICE_CONFIG_H
#define SERVICE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* ── limits ───────────────────────────────────────────────────────────── */

#define CFG_MAX_SECTIONS    16
#define CFG_MAX_KEYS        64
#define CFG_MAX_KEY_LEN     64
#define CFG_MAX_VALUE_LEN   256
#define CFG_MAX_LINE        512

/* ── data structures ──────────────────────────────────────────────────── */

typedef struct {
    char section[CFG_MAX_KEY_LEN];
    char key[CFG_MAX_KEY_LEN];
    char value[CFG_MAX_VALUE_LEN];
} cfg_entry_t;

typedef struct {
    cfg_entry_t  entries[CFG_MAX_SECTIONS * CFG_MAX_KEYS];
    int          count;
    char         path[256];
    char         svc_prefix[64]; /**< e.g. "AUDIO_SERVICE" for env override */
} service_config_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Parse an INI config file and apply environment-variable overrides.
 *
 * @param cfg        Config struct to populate.
 * @param path       Absolute path to the .conf file.
 * @param svc_prefix Service prefix for env overrides (e.g. "AUDIO_SERVICE").
 *                   May be NULL to skip env-override step.
 * @return 0 on success, -1 on I/O error, -2 on parse error.
 */
int service_config_load(service_config_t *cfg, const char *path,
                        const char *svc_prefix);

/**
 * @brief Re-read the same INI file that was loaded with service_config_load().
 */
int service_config_reload(service_config_t *cfg);

/** @brief Free any dynamically allocated state (currently a no-op). */
void service_config_free(service_config_t *cfg);

/**
 * @brief Look up a string value.  Returns def if the key is absent.
 */
const char *service_config_get_string(const service_config_t *cfg,
                                      const char *section, const char *key,
                                      const char *def);

/**
 * @brief Look up an integer value.  Returns def on absence or parse failure.
 */
int service_config_get_int(const service_config_t *cfg,
                           const char *section, const char *key, int def);

/**
 * @brief Look up an unsigned 32-bit integer.
 */
uint32_t service_config_get_uint32(const service_config_t *cfg,
                                   const char *section, const char *key,
                                   uint32_t def);

/**
 * @brief Look up a boolean (1/true/yes → 1, everything else → 0).
 */
int service_config_get_bool(const service_config_t *cfg,
                            const char *section, const char *key, int def);

/**
 * @brief Validate that every (section, key) pair in the required[] array
 *        is present and non-empty.
 *
 * On the first missing key, logs a meaningful error message and returns -1.
 * required must be terminated by a { NULL, NULL } sentinel.
 *
 * @param cfg      Loaded config.
 * @param required NULL-terminated array of { section, key } pairs.
 * @return 0 if all keys present, -1 on first missing key.
 */
typedef struct { const char *section; const char *key; } cfg_required_t;
int service_config_validate(const service_config_t *cfg,
                            const cfg_required_t *required);

/**
 * @brief Store or update a key's value at runtime (in-memory only).
 */
void service_config_set(service_config_t *cfg, const char *section,
                        const char *key, const char *value);

#endif /* SERVICE_CONFIG_H */
