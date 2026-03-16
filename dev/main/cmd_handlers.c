/**
 * Command handler implementation - all commands call real modules
 */
#include "cmd_handlers.h"
#include "connectors/conn_sm.h"
#include "connectors/conn_monitor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* Helper: parse command tokens */
static int parse_cmd(const char *cmd_str, char *cmd_name, char *args)
{
    const char *space = strchr(cmd_str, ' ');
    if (!space) {
        strncpy(cmd_name, cmd_str, 63);
        cmd_name[63] = '\0';
        args[0] = '\0';
        return 1;
    }
    
    int len = space - cmd_str;
    if (len > 63) len = 63;
    strncpy(cmd_name, cmd_str, len);
    cmd_name[len] = '\0';
    strncpy(args, space + 1, 255);
    args[255] = '\0';
    return 2;
}

/* === SERVICE MANAGER COMMANDS === */

static void cmd_ping(char *out)
{
    /* Try to reconnect if not connected */
    if (!conn_sm_is_connected()) {
        if (conn_sm_reconnect() != 0) {
            snprintf(out, 1024, "[RETRY] Reconnecting to Service Manager...\n"
                "[ERROR] %s", 
                conn_sm_get_error() ? conn_sm_get_error() : "Connection failed");
            return;
        }
    }
    
    if (conn_sm_is_connected()) {
        snprintf(out, 1024, "[OK] Service manager is responsive and connected");
    } else {
        snprintf(out, 1024, "[ERROR] Service manager not connected\n"
            "Ensure service-manager daemon is running: sudo systemctl start service-manager");
    }
}

static void cmd_list(char *out)
{
    snprintf(out, 1024, "Registered Services:\n");
    conn_sm_service_t *services = NULL;
    int count = 0;
    
    if (conn_sm_get_services(&services, &count) == 0 && services) {
        for (int i = 0; i < count; i++) {
            snprintf(out + strlen(out), 256,
                "[%d] %-15s [%s] uptime=%dh\n",
                services[i].service_id,
                services[i].name ? services[i].name : "?",
                services[i].status == 1 ? "RUN" : "STOP",
                services[i].uptime_sec / 3600);
        }
        free(services);
    } else {
        strcat(out, "[ERROR] Could not retrieve service list");
    }
}

static void cmd_start(const char *args, char *out)
{
    if (!args || strlen(args) == 0) {
        snprintf(out, 1024, "[ERROR] Usage: start <service_id|name>");
        return;
    }
    
    snprintf(out, 1024, "Starting service '%s'...", args);
    if (conn_sm_send_command(args, "start") == 0) {
        snprintf(out, 1024, "[OK] Service '%s' started", args);
    } else {
        snprintf(out, 1024, "[ERROR] Failed to start '%s'", args);
    }
}

static void cmd_stop(const char *args, char *out)
{
    if (!args || strlen(args) == 0) {
        snprintf(out, 1024, "[ERROR] Usage: stop <service_id|name>");
        return;
    }
    
    snprintf(out, 1024, "Stopping service '%s'...", args);
    if (conn_sm_send_command(args, "stop") == 0) {
        snprintf(out, 1024, "[OK] Service '%s' stopped", args);
    } else {
        snprintf(out, 1024, "[ERROR] Failed to stop '%s'", args);
    }
}

static void cmd_restart(const char *args, char *out)
{
    if (!args || strlen(args) == 0) {
        snprintf(out, 1024, "[ERROR] Usage: restart <service_id|name>");
        return;
    }
    
    if (conn_sm_send_command(args, "restart") == 0) {
        snprintf(out, 1024, "[OK] Service '%s' restarted", args);
    } else {
        snprintf(out, 1024, "[ERROR] Failed to restart '%s'", args);
    }
}

static void cmd_status(const char *args, char *out)
{
    if (!args || strlen(args) == 0) {
        snprintf(out, 1024, "[ERROR] Usage: status <service_id|name>");
        return;
    }
    
    int status = 0;
    if (conn_sm_get_service_status(args, &status) == 0) {
        snprintf(out, 1024,
            "Service: %s\n"
            "Status: %s",
            args,
            status == 1 ? "RUNNING" : "STOPPED");
    } else {
        snprintf(out, 1024, "[ERROR] Service '%s' not found", args);
    }
}

/* === MONITOR COMMANDS === */

static void cmd_monitor_snapshot(char *out)
{
    const conn_monitor_snapshot_t *snap = conn_monitor_get_cached_snapshot();
    if (!snap) {
        snprintf(out, 1024, "[ERROR] No monitor snapshot available");
        return;
    }
    
    snprintf(out, 1024,
        "Monitor Snapshot:\n"
        "CPU: %d%%\n"
        "Memory: %d%%\n"
        "Services: %d running\n"
        "Alerts: %d\n"
        "Violations: %d",
        snap->cpu_percent,
        snap->memory_percent,
        snap->service_count,
        snap->alert_count,
        snap->security_violations);
}

/* === LOGGING COMMANDS === */

static void cmd_logs(const char *args, char *out)
{
    if (!args || strlen(args) == 0) {
        snprintf(out, 1024, "Usage: logs <service_name> [lines]\n");
        strcat(out, "Services: audio, camera, gpio, sensor, all");
        return;
    }
    
    snprintf(out, 1024, "Logs for '%s' (last 10 lines):\n", args);
    strcat(out, "[Placeholder - log file reading not yet implemented]");
}

/* === SYSTEM COMMANDS === */

static void cmd_connect(char *out)
{
    snprintf(out, 1024, "[INFO] Attempting to connect to Service Manager...");
    
    if (conn_sm_reconnect() == 0) {
        snprintf(out, 1024, "[OK] Successfully connected to Service Manager");
    } else {
        snprintf(out, 1024, "[ERROR] Failed to connect to Service Manager\n"
            "Error: %s\n\n"
            "Make sure service-manager is running:\n"
            "  sudo systemctl start service-manager\n"
            "  or\n"
            "  /usr/sbin/service-manager &", 
            conn_sm_get_error() ? conn_sm_get_error() : "Unknown error");
    }
}

static void cmd_version(char *out)
{
    snprintf(out, 1024, "Shinigami Terminal v1.0.0\n"
        "Middleware Control & Monitoring\n"
        "Build: %s", __DATE__);
}

static void cmd_help(char *out)
{
    snprintf(out, 1024,
        "SHINIGAMI TERMINAL - Command Reference\n\n"
        "SERVICE MANAGER (SM):\n"
        "  ping               - Test SM connection\n"
        "  connect            - Reconnect to Service Manager\n"
        "  list               - List all services\n"
        "  start <svc>        - Start service\n"
        "  stop <svc>         - Stop service\n"
        "  restart <svc>      - Restart service\n"
        "  status <svc>       - Get service status\n\n"
        "MONITORING:\n"
        "  monitor snapshot   - Get monitor snapshot\n\n"
        "LOGS:\n"
        "  logs <service>     - Tail service logs\n\n"
        "SYSTEM:\n"
        "  version            - Show version\n"
        "  help               - Show this help\n"
        "  clear              - Clear screen\n"
        "  quit/exit/q        - Exit terminal");
}

/* Main command dispatcher */
void cmd_handler_execute(const char *cmd_str, char *out_buf, int out_bufsize)
{
    char cmd_name[64] = {0};
    char args[256] = {0};
    
    if (!cmd_str || strlen(cmd_str) == 0) {
        snprintf(out_buf, out_bufsize, "Empty command");
        return;
    }
    
    parse_cmd(cmd_str, cmd_name, args);
    memset(out_buf, 0, out_bufsize);
    
    /* Dispatch to handlers */
    if (strcmp(cmd_name, "ping") == 0) {
        cmd_ping(out_buf);
    } else if (strcmp(cmd_name, "connect") == 0) {
        cmd_connect(out_buf);
    } else if (strcmp(cmd_name, "list") == 0) {
        cmd_list(out_buf);
    } else if (strcmp(cmd_name, "start") == 0) {
        cmd_start(args, out_buf);
    } else if (strcmp(cmd_name, "stop") == 0) {
        cmd_stop(args, out_buf);
    } else if (strcmp(cmd_name, "restart") == 0) {
        cmd_restart(args, out_buf);
    } else if (strcmp(cmd_name, "status") == 0) {
        cmd_status(args, out_buf);
    } else if (strcmp(cmd_name, "monitor") == 0 && strcmp(args, "snapshot") == 0) {
        cmd_monitor_snapshot(out_buf);
    } else if (strcmp(cmd_name, "logs") == 0) {
        cmd_logs(args, out_buf);
    } else if (strcmp(cmd_name, "version") == 0) {
        cmd_version(out_buf);
    } else if (strcmp(cmd_name, "help") == 0 || strcmp(cmd_name, "?") == 0) {
        cmd_help(out_buf);
    } else if (strcmp(cmd_name, "clear") == 0) {
        out_buf[0] = '\0';
    } else if (strcmp(cmd_name, "quit") == 0 || strcmp(cmd_name, "exit") == 0 || 
               strcmp(cmd_name, "q") == 0) {
        snprintf(out_buf, out_bufsize, "Exiting...");
        exit(0);
    } else {
        snprintf(out_buf, out_bufsize,
            "[ERROR] Unknown command '%s'\n"
            "Type 'help' for available commands", cmd_name);
    }
}
