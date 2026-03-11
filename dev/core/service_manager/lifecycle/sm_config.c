#define _POSIX_C_SOURCE 200809L

/*
 * sm_config.c - Configuration file parsing
 *
 * Reads config from /etc/servicemanager.conf or environment variables.
 */

#include "../lifecycle/sm_config.h"
#include "../observability/sm_logging.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sm_config_t g_config;
static int g_config_loaded = 0;

sm_config_t sm_config_default(void) {
  sm_config_t cfg = {
      .max_services = 32,
      .max_restarts = 3,
      .heartbeat_timeout = 10,
      .message_timeout_sec = 5,

      .rate_pid_capacity = 10,
      .rate_pid_refill = 10.0,
      .rate_global_capacity = 50,
      .rate_global_refill = 50.0,

      .log_level = 1, /* INFO */
      .log_max_size_mb = 10,
      .log_backup_count = 5,

      .socket_mode = 0660,
      .socket_backlog = 32,

      .health_check_interval = 3,
      .health_restart_delay = 2,

      .management_port = 9999,
      .enable_management = 0,

      .thread_pool_size = 4,
      .work_queue_size = 64,

      .enable_persistence = 1,
  };

  strncpy(cfg.log_file, "/var/log/servicemanager.log",
          sizeof(cfg.log_file) - 1);
  strncpy(cfg.socket_path, "/run/servicemanager.sock",
          sizeof(cfg.socket_path) - 1);
  strncpy(cfg.persistence_file, "/var/lib/servicemanager/registry.dat",
          sizeof(cfg.persistence_file) - 1);
  strncpy(cfg.persistence_file, "/var/lib/servicemanager/registry.dat",
          sizeof(cfg.persistence_file) - 1);
  cfg.verify_key_file[0] = '\0'; // Add this line

  return cfg;
}

static int parse_int(const char *value) { return atoi(value); }

static int parse_octal_or_decimal(const char *value) {
  if (!value)
    return 0;
  /* Use strtol with base 8 to properly parse octal numbers like 0660 */
  if (value[0] == '0' && (value[1] >= '0' && value[1] <= '7')) {
    return (int)strtol(value, NULL, 8); /* octal */
  }
  return atoi(value); /* decimal */
}

static char *str_trim(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end))
    *end-- = '\0';
  return s;
}

int sm_config_load(const char *filename) {
  FILE *f;
  char line[512];
  char section[64] = "";

  if (!filename)
    filename = "/etc/servicemanager.conf";

  g_config = sm_config_default();

  f = fopen(filename, "r");
  if (!f) {
    sm_log(SM_LOG_WARN, "config: cannot open %s, using defaults", filename);
    g_config_loaded = 1;
    return 0;
  }

  while (fgets(line, sizeof(line), f)) {
    char *p = str_trim(line);

    if (!p[0] || p[0] == ';' || p[0] == '#')
      continue;

    if (p[0] == '[' && p[strlen(p) - 1] == ']') {
      strncpy(section, p + 1, sizeof(section) - 2);
      section[strlen(section) - 1] = '\0';
      continue;
    }

    char *eq = strchr(p, '=');
    if (!eq)
      continue;

    *eq = '\0';
    char *key = str_trim(p);
    char *val = str_trim(eq + 1);

    if (!strcmp(section, "limits")) {
      if (!strcmp(key, "max_services"))
        g_config.max_services = parse_int(val);
      else if (!strcmp(key, "max_restarts"))
        g_config.max_restarts = parse_int(val);
      else if (!strcmp(key, "heartbeat_timeout"))
        g_config.heartbeat_timeout = parse_int(val);
    } else if (!strcmp(section, "logging")) {
      if (!strcmp(key, "level"))
        g_config.log_level = parse_int(val);
      else if (!strcmp(key, "max_size_mb"))
        g_config.log_max_size_mb = parse_int(val);
      else if (!strcmp(key, "file"))
        strncpy(g_config.log_file, val, sizeof(g_config.log_file) - 1);
    } else if (!strcmp(section, "socket")) {
      if (!strcmp(key, "path"))
        strncpy(g_config.socket_path, val, sizeof(g_config.socket_path) - 1);
      else if (!strcmp(key, "mode"))
        g_config.socket_mode = parse_octal_or_decimal(val);
    } else if (!strcmp(section, "rate_limit")) {
      if (!strcmp(key, "pid_capacity"))
        g_config.rate_pid_capacity = parse_int(val);
      else if (!strcmp(key, "global_capacity"))
        g_config.rate_global_capacity = parse_int(val);
    } else if (!strcmp(section, "management")) {
      if (!strcmp(key, "enable"))
        g_config.enable_management = parse_int(val);
      else if (!strcmp(key, "port"))
        g_config.management_port = parse_int(val);
    } else if (!strcmp(section, "threadpool")) {
      if (!strcmp(key, "size"))
        g_config.thread_pool_size = parse_int(val);
      else if (!strcmp(key, "queue_size"))
        g_config.work_queue_size = parse_int(val);
    } else if (!strcmp(section, "security")) {
      if (!strcmp(key, "verify_key_file")) {
        strncpy(g_config.verify_key_file, val,
                sizeof(g_config.verify_key_file) - 1);
      }
    }
  }

  fclose(f);

  if (sm_config_validate(&g_config) < 0) {
    sm_log(SM_LOG_ERROR, "config: validation failed");
    return -1;
  }

  sm_log(SM_LOG_INFO, "config: loaded from %s", filename);
  g_config_loaded = 1;
  return 0;
}

const sm_config_t *sm_config_get(void) {
  if (!g_config_loaded) {
    g_config = sm_config_default();
    g_config_loaded = 1;
  }
  return &g_config;
}

int sm_config_set_int(const char *key, int value) {
  if (!key)
    return -1;

  if (!strcmp(key, "max_services"))
    g_config.max_services = value;
  else if (!strcmp(key, "max_restarts"))
    g_config.max_restarts = value;
  else if (!strcmp(key, "heartbeat_timeout"))
    g_config.heartbeat_timeout = value;
  else
    return -1;

  return 0;
}

int sm_config_set_string(const char *section, const char *key,
                         const char *value) {
  if (!section || !key || !value)
    return -1;

  if (!strcmp(section, "logging")) {
    if (!strcmp(key, "file")) {
      strncpy(g_config.log_file, value, sizeof(g_config.log_file) - 1);
      g_config.log_file[sizeof(g_config.log_file) - 1] = '\0';
    } else
      return -1;
  } else if (!strcmp(section, "socket")) {
    if (!strcmp(key, "path")) {
      strncpy(g_config.socket_path, value, sizeof(g_config.socket_path) - 1);
      g_config.socket_path[sizeof(g_config.socket_path) - 1] = '\0';
    } else
      return -1;
  } else if (!strcmp(section, "persistence")) {
    if (!strcmp(key, "file")) {
      strncpy(g_config.persistence_file, value,
              sizeof(g_config.persistence_file) - 1);
      g_config.persistence_file[sizeof(g_config.persistence_file) - 1] = '\0';
    } else
      return -1;
  } else {
    return -1;
  }

  return 0;
}

int sm_config_validate(const sm_config_t *cfg) {
  if (!cfg)
    return -1;
  if (cfg->max_services < 1 || cfg->max_services > 1024)
    return -1;
  if (cfg->max_restarts < 0)
    return -1;
  if (cfg->heartbeat_timeout < 1)
    return -1;
  if (cfg->thread_pool_size < 1 || cfg->thread_pool_size > 64)
    return -1;
  if (cfg->work_queue_size < 4 || cfg->work_queue_size > 4096)
    return -1;
  return 0;
}
