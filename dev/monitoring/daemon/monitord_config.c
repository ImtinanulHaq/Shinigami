/**
 * @file    monitord_config.c
 * @brief   Configuration management implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "monitord_config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static char g_config_path[512] = {0};

static void set_defaults(monitord_config_t *config)
{
    config->refresh_interval_ms = 1000;
    config->snapshot_interval_ms = 1000;

    strncpy(config->unix_socket_path, "/tmp/middleware_monitor.sock",
            sizeof(config->unix_socket_path) - 1);
    config->http_port = 9090;
    strncpy(config->http_bind_addr, "0.0.0.0",
            sizeof(config->http_bind_addr) - 1);

    strncpy(config->log_level, "INFO", sizeof(config->log_level) - 1);
    strncpy(config->log_path, "/tmp/monitord.log",
            sizeof(config->log_path) - 1);
    strncpy(config->alert_log_path, "/tmp/monitord_alerts.log",
            sizeof(config->alert_log_path) - 1);

    config->webhook_url[0] = '\0';
}

int monitord_config_load(monitord_config_t *config, const char *path)
{
    /* Set defaults first */
    set_defaults(config);

    /* Try to open INI file */
    const char *paths[] = {
        path,
        "/etc/middleware/monitord.ini",
        NULL,  /* Will try $HOME/.config/monitord.ini */
        NULL
    };

    FILE *f = NULL;
    for (int i = 0; paths[i] || (i == 2); i++) {
        const char *p = paths[i];
        if (i == 2) {
            /* Try $HOME/.config/monitord.ini */
            const char *home = getenv("HOME");
            if (home) {
                static char homebuf[512];
                snprintf(homebuf, sizeof(homebuf), "%s/.config/monitord.ini", home);
                p = homebuf;
            } else {
                continue;
            }
        }
        if (!p) continue;

        f = fopen(p, "r");
        if (f) {
            strncpy(g_config_path, p, sizeof(g_config_path) - 1);
            break;
        }
    }

    if (!f) {
        /* No config file found; use defaults */
        fprintf(stderr, "[monitord_config] No config file found, using defaults\n");
        return 0;
    }

    /* Minimal INI parser (simplified) */
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == ';' || line[0] == '\n') continue;

        /* TODO: Full INI parser with sections */
        /* For now, just accept key=value pairs */
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        /* Trim whitespace */
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        char *end = value + strlen(value) - 1;
        while (end > value && (*end == '\n' || *end == '\r' || *end == ' '))
            *end-- = '\0';

        /* Apply settings */
        if (strcmp(key, "refresh_interval_ms") == 0)
            config->refresh_interval_ms = (uint32_t)atoi(value);
        else if (strcmp(key, "http_port") == 0)
            config->http_port = (uint16_t)atoi(value);
        else if (strcmp(key, "unix_socket_path") == 0)
            strncpy(config->unix_socket_path, value,
                    sizeof(config->unix_socket_path) - 1);
        else if (strcmp(key, "webhook_url") == 0)
            strncpy(config->webhook_url, value,
                    sizeof(config->webhook_url) - 1);
        /* Add more settings as needed */
    }

    fclose(f);
    return 0;
}

int monitord_config_reload(monitord_config_t *config)
{
    if (g_config_path[0] == '\0')
        return -1;  /* No config file was loaded initially */

    return monitord_config_load(config, g_config_path);
}
