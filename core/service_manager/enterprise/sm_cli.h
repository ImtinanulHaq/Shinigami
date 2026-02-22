/*
 * sm_cli.h - Command-line interface for service manager
 *
 * Internal APIs used by the servicemanagerctl CLI tool.
 * Provides human-readable output and command processing.
 */

#ifndef SM_CLI_H
#define SM_CLI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CLI command results */
typedef struct {
    int exit_code;
    char output[4096];
    char error[512];
} sm_cli_result_t;

/* CLI command types */
typedef enum {
    CLI_CMD_STATUS = 1,        /* Get overall status */
    CLI_CMD_LIST = 2,          /* List all services */
    CLI_CMD_START = 3,         /* Start service */
    CLI_CMD_STOP = 4,          /* Stop service */
    CLI_CMD_RESTART = 5,       /* Restart service */
    CLI_CMD_LOGS = 6,          /* Get service logs */
    CLI_CMD_INFO = 7,          /* Get service details */
    CLI_CMD_MONITOR = 8,       /* Monitor stats in realtime */
    CLI_CMD_CONFIG = 9,        /* Show/update configuration */
    CLI_CMD_HELP = 10,         /* Help text */
} sm_cli_command_t;

/*
 * sm_cli_connect_to_manager(socket_path)
 * 
 * Connect to running service manager instance.
 * 
 * PARAMETERS:
 *   socket_path - Unix socket path to service manager
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (not running, permission denied)
 */
int sm_cli_connect_to_manager(const char* socket_path);

/*
 * sm_cli_execute_command(command, arg1, arg2)
 * 
 * Execute a CLI command against the service manager.
 * 
 * PARAMETERS:
 *   command - CLI_CMD_STATUS, CLI_CMD_LIST, etc
 *   arg1 - first argument (e.g., service name)
 *   arg2 - second argument (e.g., parameter)
 * 
 * RETURNS:
 *   Command result with exit code and output
 */
sm_cli_result_t sm_cli_execute_command(sm_cli_command_t command,
                                       const char* arg1, const char* arg2);

/*
 * sm_cli_status() - servicemanagerctl status
 * 
 * Shows overall service manager status.
 * 
 * RETURNS:
 *   Command result
 */
sm_cli_result_t sm_cli_status(void);

/*
 * sm_cli_list_services() - servicemanagerctl list
 * 
 * Lists all registered services with status.
 * 
 * RETURNS:
 *   Command result with table of services
 */
sm_cli_result_t sm_cli_list_services(void);

/*
 * sm_cli_service_start(service_name) - servicemanagerctl start <service>
 * 
 * Start a service.
 * 
 * RETURNS:
 *   Command result
 */
sm_cli_result_t sm_cli_service_start(const char* service_name);

/*
 * sm_cli_service_stop(service_name) - servicemanagerctl stop <service>
 * 
 * Stop a service.
 * 
 * RETURNS:
 *   Command result
 */
sm_cli_result_t sm_cli_service_stop(const char* service_name);

/*
 * sm_cli_service_restart(service_name) - servicemanagerctl restart <service>
 * 
 * Restart a service.
 * 
 * RETURNS:
 *   Command result
 */
sm_cli_result_t sm_cli_service_restart(const char* service_name);

/*
 * sm_cli_service_logs(service_name, lines) - servicemanagerctl logs <service>
 * 
 * Get recent service logs.
 * 
 * PARAMETERS:
 *   service_name - service identifier
 *   lines - number of log lines to retrieve (default 50)
 * 
 * RETURNS:
 *   Command result with log output
 */
sm_cli_result_t sm_cli_service_logs(const char* service_name, int lines);

/*
 * sm_cli_service_info(service_name) - servicemanagerctl info <service>
 * 
 * Get detailed service information.
 * 
 * RETURNS:
 *   Command result with JSON/table format info
 */
sm_cli_result_t sm_cli_service_info(const char* service_name);

/*
 * sm_cli_monitor_stats() - servicemanagerctl monitor
 * 
 * Display real-time statistics (refreshes periodically).
 * 
 * RETURNS:
 *   Command result
 */
sm_cli_result_t sm_cli_monitor_stats(void);

/*
 * sm_cli_get_help(command_name) - servicemanagerctl help [command]
 * 
 * Display help text.
 * 
 * RETURNS:
 *   Command result with help output
 */
sm_cli_result_t sm_cli_get_help(const char* command_name);

/*
 * sm_cli_disconnect()
 * 
 * Close connection to service manager.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_cli_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_CLI_H */
