/*
 * sm_cli.h — Command-line interface for the Bankai Service Manager.
 *
 * Two distinct APIs are provided:
 *
 *   1. Existing servicemanagerctl API (sm_cli_*) — unchanged public interface.
 *   2. Bankai interactive terminal     (bankai_run) — new interactive REPL.
 *
 * Project structure:
 *   middleware/
 *   ├── enterprise/        ← sm_cli.c + sm_cli.h yahan hain
 *   ├── infrastructure/    ← sm_protocol.h yahan hai
 *   ├── observability/
 *   └── security/
 *
 * Build — main middleware/ folder se yeh command chalao:
 *   gcc -O2 -Wall -DBANKAI_STANDALONE \
 *       enterprise/sm_cli.c \
 *       -Ienterprise -Iinfrastructure -Iobservability -Isecurity \
 *       -o bankai -lpthread
 *
 * Build — library (daemon ke saath link):
 *   gcc -O2 -Wall -c enterprise/sm_cli.c \
 *       -Ienterprise -Iinfrastructure -Iobservability -Isecurity
 */

#ifndef SM_CLI_H
#define SM_CLI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════════════
 * SECTION A — Existing servicemanagerctl API  (public interface, unchanged)
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Result returned by every sm_cli_* call */
typedef struct {
    int  exit_code;       /* 0 = success, -1 = error             */
    char output[4096];    /* Human-readable output for the caller */
    char error[512];      /* Error description if exit_code != 0  */
} sm_cli_result_t;

/* Command type identifiers */
typedef enum {
    CLI_CMD_STATUS  = 1,   /* Get overall status        */
    CLI_CMD_LIST    = 2,   /* List all services         */
    CLI_CMD_START   = 3,   /* Start service             */
    CLI_CMD_STOP    = 4,   /* Stop service              */
    CLI_CMD_RESTART = 5,   /* Restart service           */
    CLI_CMD_LOGS    = 6,   /* Get service logs          */
    CLI_CMD_INFO    = 7,   /* Get service details       */
    CLI_CMD_MONITOR = 8,   /* Monitor stats in realtime */
    CLI_CMD_CONFIG  = 9,   /* Show/update configuration */
    CLI_CMD_HELP    = 10,  /* Help text                 */
} sm_cli_command_t;

/*
 * sm_cli_connect_to_manager() — Connect to a running Service Manager.
 *
 * PARAMETERS:
 *   socket_path  Unix socket path (e.g. "/run/servicemanager.sock")
 * RETURNS:
 *   0 on success, -1 if daemon is unreachable or permission denied.
 */
int sm_cli_connect_to_manager(const char* socket_path);

/*
 * sm_cli_execute_command() — Send a typed command and return the result.
 *
 * PARAMETERS:
 *   command  CLI_CMD_STATUS, CLI_CMD_LIST, etc.
 *   arg1     First argument (e.g. service name); NULL if unused.
 *   arg2     Second argument (e.g. parameter);  NULL if unused.
 * RETURNS:
 *   sm_cli_result_t with exit_code and formatted output string.
 */
sm_cli_result_t sm_cli_execute_command(sm_cli_command_t command,
                                        const char* arg1,
                                        const char* arg2);

/* servicemanagerctl status */
sm_cli_result_t sm_cli_status(void);

/* servicemanagerctl list */
sm_cli_result_t sm_cli_list_services(void);

/* servicemanagerctl start <service> */
sm_cli_result_t sm_cli_service_start(const char* service_name);

/* servicemanagerctl stop <service> */
sm_cli_result_t sm_cli_service_stop(const char* service_name);

/* servicemanagerctl restart <service> */
sm_cli_result_t sm_cli_service_restart(const char* service_name);

/*
 * servicemanagerctl logs <service> [lines]
 *
 * PARAMETERS:
 *   service_name  Service identifier.
 *   lines         Number of log lines to retrieve (default: 50).
 */
sm_cli_result_t sm_cli_service_logs(const char* service_name, int lines);

/* servicemanagerctl info <service> — detailed service information */
sm_cli_result_t sm_cli_service_info(const char* service_name);

/*
 * servicemanagerctl monitor — real-time statistics display.
 *
 * BUG FIXED: Previous while(1) with 60-second hard limit removed.
 * Now uses volatile sig_atomic_t + temporary SIGINT handler.
 * Ctrl+C stops immediately; original SIGINT handler restored on exit.
 */
sm_cli_result_t sm_cli_monitor_stats(void);

/*
 * servicemanagerctl help [command]
 *
 * PARAMETERS:
 *   command_name  Specific command for context-sensitive help,
 *                 or NULL for the full command list.
 */
sm_cli_result_t sm_cli_get_help(const char* command_name);

/*
 * sm_cli_disconnect() — Close connection to Service Manager.
 * RETURNS: 0 on success.
 */
int sm_cli_disconnect(void);

/* ══════════════════════════════════════════════════════════════════════════════
 * SECTION B — Bankai Interactive Terminal
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * bankai_run() — Launch the Bankai interactive terminal (blocking REPL).
 *
 * Features:
 *   register / lookup / heartbeat / unregister  — direct protocol socket
 *   logs / audit                                — live tail (Enter to stop)
 *   logdump / auditdump [N]                     — last N lines with colouring
 *   ping / socket                               — diagnostics
 *   help / clear / quit
 *
 * All 5 issues from the previous version are fixed:
 *   FIX 1  Protocol structs imported from sm_protocol.h — no duplication.
 *   FIX 2  All commands use the same raw_transact() path — no bespoke loops.
 *   FIX 3  One-time HMAC test-mode warning printed on first connect.
 *   FIX 4  Tail uses self-pipe + poll() — no getchar() race condition.
 *   FIX 5  sm_cli_tokenise() handles single/double quotes and backslash escapes.
 *
 * Socket auto-detection:
 *   Primary  : /run/servicemanager.sock
 *   Fallback : /tmp/servicemanager.sock
 *
 * Build standalone:
 *   cp ../infrastructure/sm_protocol.h .
 *   gcc -O2 -Wall -Wextra -DBANKAI_STANDALONE -o bankai sm_cli.c -lpthread
 *
 * Embed in another program:
 *   #include "sm_cli.h"
 *   bankai_run();   // blocks until user types quit
 *
 * RETURNS: 0 when the user exits.
 */
int bankai_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_CLI_H */