/**
 * @file service_config.c
 * @brief INI parser with environment-variable override and validation helpers.
 *
 * INI parsing rules:
 *   - Lines starting with '#' or ';' are comments.
 *   - Section headers: [section_name]
 *   - Key/value pairs: key = value   (inline ';' starts a comment)
 *   - Blank lines are ignored.
 *   - Keys and section names are case-insensitive (stored in lower-case).
 *
 * After parsing, environment variable overrides are applied.  For a service
 * with prefix "AUDIO_SERVICE", the section "server" and key "socket_path"
 * is overridden by the env var AUDIO_SERVICE_SERVER_SOCKET_PATH.
 */

#define _GNU_SOURCE
#include "service_config.h"
#include "service_base.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────── */

static void str_lower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void str_upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/** Strip leading and trailing whitespace in-place. */
static char *str_trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

/** Strip everything after the first ';' (inline comment). */
static void strip_comment(char *s)
{
    char *p = strchr(s, ';');
    if (p) *p = '\0';
}

static cfg_entry_t *find_entry(service_config_t *cfg,
                                const char *section, const char *key)
{
    for (int i = 0; i < cfg->count; i++) {
        if (strcasecmp(cfg->entries[i].section, section) == 0 &&
            strcasecmp(cfg->entries[i].key,     key)     == 0)
            return &cfg->entries[i];
    }
    return NULL;
}

static const cfg_entry_t *find_entry_c(const service_config_t *cfg,
                                        const char *section, const char *key)
{
    for (int i = 0; i < cfg->count; i++) {
        if (strcasecmp(cfg->entries[i].section, section) == 0 &&
            strcasecmp(cfg->entries[i].key,     key)     == 0)
            return &cfg->entries[i];
    }
    return NULL;
}

/* ── private: parse one open FILE ────────────────────────────────────── */

static int parse_file(service_config_t *cfg, FILE *fp)
{
    char line[CFG_MAX_LINE];
    char cur_section[CFG_MAX_KEY_LEN] = "";
    int  lineno = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        /* Strip trailing newline */
        char *s = str_trim(line);
        if (s[0] == '\0' || s[0] == '#' || s[0] == ';')
            continue;

        /* Section header */
        if (s[0] == '[') {
            char *end = strchr(s, ']');
            if (!end) {
                LOG_WARN("config line %d: malformed section header", lineno);
                continue;
            }
            *end = '\0';
            strncpy(cur_section, str_trim(s + 1), CFG_MAX_KEY_LEN - 1);
            str_lower(cur_section);
            continue;
        }

        /* Key = value */
        char *eq = strchr(s, '=');
        if (!eq) {
            LOG_WARN("config line %d: no '=' found — skipping", lineno);
            continue;
        }
        *eq = '\0';
        char key_buf[CFG_MAX_KEY_LEN];
        char val_buf[CFG_MAX_VALUE_LEN];

        strncpy(key_buf, str_trim(s),      CFG_MAX_KEY_LEN   - 1);
        str_lower(key_buf);
        char *vp = str_trim(eq + 1);
        strip_comment(vp);
        vp = str_trim(vp);
        strncpy(val_buf, vp, CFG_MAX_VALUE_LEN - 1);

        /* Update existing key or add new */
        cfg_entry_t *e = find_entry(cfg, cur_section, key_buf);
        if (e) {
            strncpy(e->value, val_buf, CFG_MAX_VALUE_LEN - 1);
        } else {
            if (cfg->count >= CFG_MAX_SECTIONS * CFG_MAX_KEYS) {
                LOG_WARN("config: entry limit reached, skipping %s.%s",
                         cur_section, key_buf);
                continue;
            }
            e = &cfg->entries[cfg->count++];
            strncpy(e->section, cur_section, CFG_MAX_KEY_LEN - 1);
            strncpy(e->key,     key_buf,     CFG_MAX_KEY_LEN - 1);
            strncpy(e->value,   val_buf,     CFG_MAX_VALUE_LEN - 1);
        }
    }
    return 0;
}

/* ── private: apply env overrides ────────────────────────────────────── */

static void apply_env_overrides(service_config_t *cfg)
{
    if (cfg->svc_prefix[0] == '\0') return;

    for (int i = 0; i < cfg->count; i++) {
        char env_name[256];
        /* Build: PREFIX_SECTION_KEY (all uppercase, '.' → '_') */
        char sec[CFG_MAX_KEY_LEN], key[CFG_MAX_KEY_LEN];
        strncpy(sec, cfg->entries[i].section, sizeof(sec) - 1);
        strncpy(key, cfg->entries[i].key,     sizeof(key) - 1);
        str_upper(sec);
        str_upper(key);
        /* Replace any dots or hyphens with '_' */
        for (char *p = sec; *p; p++) if (*p == '.' || *p == '-') *p = '_';
        for (char *p = key; *p; p++) if (*p == '.' || *p == '-') *p = '_';

        snprintf(env_name, sizeof(env_name), "%s_%s_%s",
                 cfg->svc_prefix, sec, key);

        const char *val = getenv(env_name);
        if (val) {
            strncpy(cfg->entries[i].value, val, CFG_MAX_VALUE_LEN - 1);
            LOG_INFO("env override: %s → %s.%s = %s",
                     env_name,
                     cfg->entries[i].section,
                     cfg->entries[i].key,
                     val);
        }
    }
}

/* ── public API ───────────────────────────────────────────────────────── */

int service_config_load(service_config_t *cfg, const char *path,
                        const char *svc_prefix)
{
    if (!cfg || !path) return -1;
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->path, path, sizeof(cfg->path) - 1);
    if (svc_prefix)
        strncpy(cfg->svc_prefix, svc_prefix, sizeof(cfg->svc_prefix) - 1);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        LOG_ERR("service_config_load: cannot open '%s': %s",
                path, strerror(errno));
        return -1;
    }
    int rc = parse_file(cfg, fp);
    fclose(fp);
    if (rc != 0) return rc;

    apply_env_overrides(cfg);
    return 0;
}

int service_config_reload(service_config_t *cfg)
{
    if (!cfg) return -1;
    /* Preserve the metadata, clear entries */
    char path[256], prefix[64];
    strncpy(path,   cfg->path,       sizeof(path)   - 1);
    strncpy(prefix, cfg->svc_prefix, sizeof(prefix) - 1);
    return service_config_load(cfg, path, prefix);
}

void service_config_free(service_config_t *cfg)
{
    (void)cfg; /* nothing heap-allocated */
}

const char *service_config_get_string(const service_config_t *cfg,
                                      const char *section, const char *key,
                                      const char *def)
{
    if (!cfg || !section || !key) return def;
    const cfg_entry_t *e = find_entry_c(cfg, section, key);
    return (e && e->value[0] != '\0') ? e->value : def;
}

int service_config_get_int(const service_config_t *cfg,
                           const char *section, const char *key, int def)
{
    const char *s = service_config_get_string(cfg, section, key, NULL);
    if (!s) return def;
    char *end;
    long v = strtol(s, &end, 10);
    return (end != s) ? (int)v : def;
}

uint32_t service_config_get_uint32(const service_config_t *cfg,
                                   const char *section, const char *key,
                                   uint32_t def)
{
    const char *s = service_config_get_string(cfg, section, key, NULL);
    if (!s) return def;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    return (end != s) ? (uint32_t)v : def;
}

int service_config_get_bool(const service_config_t *cfg,
                            const char *section, const char *key, int def)
{
    const char *s = service_config_get_string(cfg, section, key, NULL);
    if (!s) return def;
    return (strcmp(s, "1") == 0 ||
            strcasecmp(s, "true") == 0 ||
            strcasecmp(s, "yes")  == 0) ? 1 : 0;
}

int service_config_validate(const service_config_t *cfg,
                            const cfg_required_t *required)
{
    if (!cfg || !required) return -1;
    for (int i = 0; required[i].section != NULL; i++) {
        const char *v = service_config_get_string(cfg,
                            required[i].section, required[i].key, NULL);
        if (!v || v[0] == '\0') {
            LOG_ERR("config validation failed: required key [%s] %s is missing "
                    "or empty in '%s'",
                    required[i].section, required[i].key, cfg->path);
            return -1;
        }
    }
    return 0;
}

void service_config_set(service_config_t *cfg, const char *section,
                        const char *key, const char *value)
{
    if (!cfg || !section || !key || !value) return;
    cfg_entry_t *e = find_entry(cfg, section, key);
    if (e) {
        strncpy(e->value, value, CFG_MAX_VALUE_LEN - 1);
        return;
    }
    if (cfg->count >= CFG_MAX_SECTIONS * CFG_MAX_KEYS) return;
    e = &cfg->entries[cfg->count++];
    strncpy(e->section, section, CFG_MAX_KEY_LEN   - 1);
    strncpy(e->key,     key,     CFG_MAX_KEY_LEN   - 1);
    strncpy(e->value,   value,   CFG_MAX_VALUE_LEN - 1);
}
