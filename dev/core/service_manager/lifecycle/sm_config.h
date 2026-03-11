#ifndef SM_CONFIG_H
#define SM_CONFIG_H

/*
 * sm_config.h - Configuration file parsing and management
 *
 * Supports INI-style config: /etc/servicemanager.conf
 * Programs can override via environment variables or direct API calls
 */

#include <time.h>

/* ── CONFIG STRUCTURE ────────────────────────────────────────────────────────
 */

typedef struct {
  /* Limits */
  int max_services;
  int max_restarts;
  int heartbeat_timeout;
  int message_timeout_sec;

  /* Rate limiting */
  int rate_pid_capacity;
  double rate_pid_refill;
  int rate_global_capacity;
  double rate_global_refill;

  /* Logging */
  char log_file[256];
  int log_level;
  int log_max_size_mb;
  int log_backup_count;

  /* Socket */
  char socket_path[256];
  int socket_mode;
  int socket_backlog;

  /* Health */
  int health_check_interval;
  int health_restart_delay;

  /* Management API */
  int management_port;
  int enable_management;

  /* Thread pool */
  int thread_pool_size;
  int work_queue_size;

  /* Persistence */
  int enable_persistence;
  char persistence_file[256];
  /* Security */
  char verify_key_file[256];
} sm_config_t;

/* Get global config (initialized on startup) */
const sm_config_t *sm_config_get(void);

/* Parse config file */
int sm_config_load(const char *filename);

/* Set individual config values */
int sm_config_set_int(const char *key, int value);
int sm_config_set_string(const char *section, const char *key,
                         const char *value);

/* Get default config */
sm_config_t sm_config_default(void);

/* Validate config values */
int sm_config_validate(const sm_config_t *cfg);

#endif /* SM_CONFIG_H */
