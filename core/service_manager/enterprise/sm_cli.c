#define _POSIX_C_SOURCE 200809L

/*
 * sm_cli.c - Command-line interface implementation
 *
 * Backend for servicemanagerctl tool
 */

#include "../enterprise/sm_cli.h"
#include "../observability/sm_logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

static int g_cli_socket = -1;

/* FIX: Signal handler ke liye volatile flag
 * Ctrl+C dabane par monitor loop turant band ho jaye */
static volatile sig_atomic_t g_monitor_running = 0;

static void monitor_sigint_handler(int sig)
{
    (void)sig;
    g_monitor_running = 0;  /* Loop band karo */
}

int sm_cli_connect_to_manager(const char* socket_path)
{
    if (!socket_path) {
        return -1;
    }
    
    /* Create Unix domain socket */
    g_cli_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_cli_socket < 0) {
        return -1;
    }
    
    /* Connect to service manager */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(g_cli_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_cli_socket);
        g_cli_socket = -1;
        return -1;
    }
    
    return 0;
}

sm_cli_result_t sm_cli_execute_command(sm_cli_command_t command,
                                       const char* arg1, const char* arg2)
{
    sm_cli_result_t result = {0};
    
    if (g_cli_socket < 0) {
        result.exit_code = -1;
        snprintf(result.error, sizeof(result.error),
                "Not connected to service manager");
        return result;
    }
    
    /* Build command string and send to service manager */
    char cmd_buf[1024];
    snprintf(cmd_buf, sizeof(cmd_buf), "CMD=%d|ARG1=%s|ARG2=%s",
            command, arg1 ? arg1 : "", arg2 ? arg2 : "");
    
    if (send(g_cli_socket, cmd_buf, strlen(cmd_buf), 0) < 0) {
        result.exit_code = -1;
        snprintf(result.error, sizeof(result.error),
                "Failed to send command: %s", strerror(errno));
        return result;
    }
    
    /* Receive response */
    char response[4096];
    int recv_len = recv(g_cli_socket, response, sizeof(response) - 1, 0);
    
    if (recv_len < 0) {
        result.exit_code = -1;
        snprintf(result.error, sizeof(result.error),
                "Failed to receive response: %s", strerror(errno));
        return result;
    }
    
    response[recv_len] = '\0';
    strncpy(result.output, response, sizeof(result.output) - 1);
    result.output[sizeof(result.output) - 1] = '\0';
    result.exit_code = 0;
    
    return result;
}

sm_cli_result_t sm_cli_status(void)
{
    return sm_cli_execute_command(CLI_CMD_STATUS, NULL, NULL);
}

sm_cli_result_t sm_cli_list_services(void)
{
    return sm_cli_execute_command(CLI_CMD_LIST, NULL, NULL);
}

sm_cli_result_t sm_cli_service_start(const char* service_name)
{
    return sm_cli_execute_command(CLI_CMD_START, service_name, NULL);
}

sm_cli_result_t sm_cli_service_stop(const char* service_name)
{
    return sm_cli_execute_command(CLI_CMD_STOP, service_name, NULL);
}

sm_cli_result_t sm_cli_service_restart(const char* service_name)
{
    return sm_cli_execute_command(CLI_CMD_RESTART, service_name, NULL);
}

sm_cli_result_t sm_cli_service_logs(const char* service_name, int lines)
{
    char lines_str[16];
    snprintf(lines_str, sizeof(lines_str), "%d", lines);
    return sm_cli_execute_command(CLI_CMD_LOGS, service_name, lines_str);
}

sm_cli_result_t sm_cli_service_info(const char* service_name)
{
    return sm_cli_execute_command(CLI_CMD_INFO, service_name, NULL);
}

sm_cli_result_t sm_cli_monitor_stats(void)
{
    sm_cli_result_t result = {0};

    /* FIX: Pehle purana SIGINT handler save karo
     * Monitor khatam hone ke baad restore kar denge */
    struct sigaction sa_new, sa_old;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = monitor_sigint_handler;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;
    sigaction(SIGINT, &sa_new, &sa_old);

    /* FIX: flag ON karo — loop chalega jab tak flag ON hai */
    g_monitor_running = 1;

    /* Display header */
    printf("\n=== Service Manager Statistics (Ctrl+C to stop) ===\n");
    printf("Service | Status | CPU | Memory | Restarts | Uptime\n");
    printf("--------|--------|-----|--------|----------|-------\n");

    /* FIX: while(1) hata diya — ab flag check hota hai
     * Ctrl+C dabao: signal handler flag = 0 karta hai, loop band */
    while (g_monitor_running) {
        sm_cli_result_t status = sm_cli_status();
        if (status.exit_code == 0) {
            printf("%s\n", status.output);
        }
        sleep(1);
    }

    /* FIX: Purana signal handler restore karo
     * Taake baaki program ka SIGINT behaviour theek rahe */
    sigaction(SIGINT, &sa_old, NULL);

    printf("\nMonitoring stopped.\n");
    result.exit_code = 0;
    snprintf(result.output, sizeof(result.output), "Monitoring complete");
    return result;
}

sm_cli_result_t sm_cli_get_help(const char* command_name)
{
    sm_cli_result_t result = {0};
    
    if (!command_name) {
        snprintf(result.output, sizeof(result.output),
"servicemanagerctl - Service Manager Control\n"
"\n"
"USAGE: servicemanagerctl [command] [options]\n"
"\n"
"COMMANDS:\n"
"  status              - Show overall service manager status\n"
"  list                - List all registered services\n"
"  info <service>      - Show detailed service information\n"
"  start <service>     - Start a service\n"
"  stop <service>      - Stop a service\n"
"  restart <service>   - Restart a service\n"
"  logs <service>      - Show recent service logs\n"
"  monitor             - Display real-time statistics\n"
"  help [command]      - Show help (optionally for specific command)\n"
"\n"
"EXAMPLES:\n"
"  servicemanagerctl status\n"
"  servicemanagerctl list\n"
"  servicemanagerctl info postgres\n"
"  servicemanagerctl restart myservice\n");
    } else {
        snprintf(result.output, sizeof(result.output),
"Help for command: %s\n"
"\n"
"Not yet implemented - see general help.\n", command_name);
    }
    
    result.exit_code = 0;
    return result;
}

int sm_cli_disconnect(void)
{
    if (g_cli_socket >= 0) {
        close(g_cli_socket);
        g_cli_socket = -1;
    }
    return 0;
}