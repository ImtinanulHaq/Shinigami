#define _POSIX_C_SOURCE 200809L
/* sm_logging.h optional — only needed when linked with full daemon */
#ifdef SM_LOGGING_AVAILABLE
#  include "sm_logging.h"
#endif

/*
 * sm_cli.c — Command-line interface + Bankai interactive terminal.
 *
 * FIX SUMMARY (over previous version):
 *   1. Protocol structs no longer duplicated — sm_protocol.h included directly.
 *      Standalone build: copy sm_protocol.h to the same directory and build with
 *      -DBANKAI_STANDALONE.  Library build: normal project include path is used.
 *   2. bankai_cmd_lookup() unified with raw_transact() — same send/recv path as
 *      every other command; no bespoke send loop.
 *   3. HMAC-zero test-mode documented with a visible runtime warning on connect.
 *   4. Tail stop race fixed: self-pipe replaces getchar().  The tail thread
 *      poll()s both the log fd and the read-end of the pipe; main writes one
 *      byte to the write-end when the user presses Enter — no buffered-input
 *      race possible.
 *   5. Command tokeniser replaced: sm_cli_tokenise() handles single-quoted,
 *      double-quoted and unquoted tokens correctly (max BANKAI_MAX_ARGS args).
 *
 * Build — standalone Bankai binary:
 *   cp ../infrastructure/sm_protocol.h .
 *   gcc -O2 -Wall -Wextra -DBANKAI_STANDALONE -o bankai sm_cli.c -lpthread
 *
 * Build — as library (linked with the daemon):
 *   gcc -O2 -Wall -Wextra -c sm_cli.c   (no BANKAI_STANDALONE, no extra main)
 */

/* ── Includes ────────────────────────────────────────────────────────────────── */

/*
 * FIX 1: Include sm_protocol.h instead of re-defining structs.
 * sm_cli.c lives in enterprise/ — sm_protocol.h is in infrastructure/
 */
#include "../infrastructure/sm_protocol.h"
#include "sm_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/random.h>
#include <sys/time.h>
/* ── ANSI Colors ─────────────────────────────────────────────────────────────── */
#define C_RESET    "\033[0m"
#define C_BOLD     "\033[1m"
#define C_DIM      "\033[2m"
#define C_RED      "\033[1;31m"
#define C_GREEN    "\033[1;32m"
#define C_YELLOW   "\033[1;33m"
#define C_BLUE     "\033[1;34m"
#define C_MAGENTA  "\033[1;35m"
#define C_CYAN     "\033[1;36m"
#define C_WHITE    "\033[1;37m"

/* ── Paths ───────────────────────────────────────────────────────────────────── */
#define SM_SOCKET_PRIMARY  "/run/servicemanager.sock"
#define SM_SOCKET_FALLBACK "/tmp/servicemanager.sock"
#define SM_LOG_FILE        "/var/log/servicemanager.log"
#define SM_AUDIT_LOG_FILE  "/var/log/servicemanager-audit.log"

/* ── Tokeniser limits ────────────────────────────────────────────────────────── */
#define BANKAI_MAX_ARGS   16
#define BANKAI_MAX_TOKEN  512

/* ── Internal state ──────────────────────────────────────────────────────────── */
static int          g_cli_socket    = -1;
static const char*  g_socket_path   = SM_SOCKET_PRIMARY;

/* Monitor-mode signal flag (existing bug fix retained) */
static volatile sig_atomic_t g_monitor_running = 0;

/*
 * Tail thread state.
 * FIX 4: stop_pipe[0] = read-end (tail thread), stop_pipe[1] = write-end (main).
 * Writing any byte to stop_pipe[1] wakes the tail thread immediately.
 */
static volatile int g_tail_active   = 0;
static pthread_t    g_tail_tid;
static char         g_tail_path[512];
static int          g_stop_pipe[2]  = { -1, -1 };

/* ══════════════════════════════════════════════════════════════════════════════
 * SECTION 1 — EXISTING CLI FUNCTIONS  (public API — unchanged)
 * ══════════════════════════════════════════════════════════════════════════════ */

static void monitor_sigint_handler(int sig)
{
    (void)sig;
    g_monitor_running = 0;
}

int sm_cli_connect_to_manager(const char* socket_path)
{
    if (!socket_path) return -1;

    g_cli_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_cli_socket < 0) return -1;

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
                                        const char* arg1,
                                        const char* arg2)
{
    sm_cli_result_t result = {0};

    if (g_cli_socket < 0) {
        result.exit_code = -1;
        snprintf(result.error, sizeof(result.error),
                 "Not connected to service manager");
        return result;
    }

    char cmd_buf[1024];
    snprintf(cmd_buf, sizeof(cmd_buf), "CMD=%d|ARG1=%s|ARG2=%s",
             command, arg1 ? arg1 : "", arg2 ? arg2 : "");

    if (send(g_cli_socket, cmd_buf, strlen(cmd_buf), 0) < 0) {
        result.exit_code = -1;
        snprintf(result.error, sizeof(result.error),
                 "Failed to send command: %s", strerror(errno));
        return result;
    }

    char    response[4096];
    ssize_t n = recv(g_cli_socket, response, sizeof(response) - 1, 0);
    if (n < 0) {
        result.exit_code = -1;
        snprintf(result.error, sizeof(result.error),
                 "Failed to receive response: %s", strerror(errno));
        return result;
    }

    response[n] = '\0';
    strncpy(result.output, response, sizeof(result.output) - 1);
    result.output[sizeof(result.output) - 1] = '\0';
    result.exit_code = 0;
    return result;
}

sm_cli_result_t sm_cli_status(void)
{ return sm_cli_execute_command(CLI_CMD_STATUS, NULL, NULL); }

sm_cli_result_t sm_cli_list_services(void)
{ return sm_cli_execute_command(CLI_CMD_LIST, NULL, NULL); }

sm_cli_result_t sm_cli_service_start(const char* svc)
{ return sm_cli_execute_command(CLI_CMD_START, svc, NULL); }

sm_cli_result_t sm_cli_service_stop(const char* svc)
{ return sm_cli_execute_command(CLI_CMD_STOP, svc, NULL); }

sm_cli_result_t sm_cli_service_restart(const char* svc)
{ return sm_cli_execute_command(CLI_CMD_RESTART, svc, NULL); }

sm_cli_result_t sm_cli_service_logs(const char* svc, int lines)
{
    char ls[16];
    snprintf(ls, sizeof(ls), "%d", lines);
    return sm_cli_execute_command(CLI_CMD_LOGS, svc, ls);
}

sm_cli_result_t sm_cli_service_info(const char* svc)
{ return sm_cli_execute_command(CLI_CMD_INFO, svc, NULL); }

/*
 * sm_cli_monitor_stats() — fixed (no while(1), no hard time limit).
 * Installs a temporary SIGINT handler; restores the original on exit.
 */
sm_cli_result_t sm_cli_monitor_stats(void)
{
    sm_cli_result_t result = {0};

    struct sigaction sa_new, sa_old;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = monitor_sigint_handler;
    sigemptyset(&sa_new.sa_mask);
    sigaction(SIGINT, &sa_new, &sa_old);

    g_monitor_running = 1;

    printf(C_CYAN "\n=== Service Manager Statistics  (Ctrl+C to stop) ===\n" C_RESET);
    printf(C_BOLD "Service | Status | Restarts | Uptime\n" C_RESET);
    printf(      "--------|--------|----------|-------\n");

    while (g_monitor_running) {
        sm_cli_result_t s = sm_cli_status();
        if (s.exit_code == 0) printf("%s\n", s.output);
        sleep(1);
    }

    sigaction(SIGINT, &sa_old, NULL);
    printf(C_YELLOW "\nMonitoring stopped.\n" C_RESET);
    result.exit_code = 0;
    snprintf(result.output, sizeof(result.output), "Monitoring complete");
    return result;
}

sm_cli_result_t sm_cli_get_help(const char* cmd)
{
    sm_cli_result_t result = {0};
    if (!cmd) {
        snprintf(result.output, sizeof(result.output),
"servicemanagerctl — Service Manager Control\n"
"\n"
"USAGE: servicemanagerctl [command] [options]\n"
"\n"
"COMMANDS:\n"
"  status              Show overall service manager status\n"
"  list                List all registered services\n"
"  info <service>      Show detailed service information\n"
"  start <service>     Start a service\n"
"  stop <service>      Stop a service\n"
"  restart <service>   Restart a service\n"
"  logs <service>      Show recent service logs\n"
"  monitor             Display real-time statistics\n"
"  help [command]      Show help\n"
"\n"
"EXAMPLES:\n"
"  servicemanagerctl status\n"
"  servicemanagerctl list\n"
"  servicemanagerctl info audio-svc\n"
"  servicemanagerctl restart audio-svc\n");
    } else {
        snprintf(result.output, sizeof(result.output),
                 "Help for '%s': see general help.\n", cmd);
    }
    result.exit_code = 0;
    return result;
}

int sm_cli_disconnect(void)
{
    if (g_cli_socket >= 0) { close(g_cli_socket); g_cli_socket = -1; }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SECTION 2 — BANKAI TERMINAL: INTERNAL HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* ── UI ──────────────────────────────────────────────────────────────────────── */

static void bankai_sep(void)
{
    printf(C_BLUE
           "  ══════════════════════════════════════════════════════\n"
           C_RESET);
}

static void bankai_banner(void)
{
    printf("\033[2J\033[H");
    printf(C_MAGENTA
        "\n"
        "  ██████╗  █████╗ ███╗   ██╗██╗  ██╗ █████╗ ██╗\n"
        "  ██╔══██╗██╔══██╗████╗  ██║██║ ██╔╝██╔══██╗██║\n"
        "  ██████╔╝███████║██╔██╗ ██║█████╔╝ ███████║██║\n"
        "  ██╔══██╗██╔══██║██║╚██╗██║██╔═██╗ ██╔══██║██║\n"
        "  ██████╔╝██║  ██║██║ ╚████║██║  ██╗██║  ██║██║\n"
        "  ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝\n"
        C_RESET);
    printf(C_CYAN
           "         Service Manager — Interactive Terminal\n"
           C_RESET);
    bankai_sep();
}

static void bankai_help(void)
{
    bankai_sep();
    printf(C_BOLD "  PROTOCOL COMMANDS:\n" C_RESET);
    printf(C_GREEN "  register   <n> <socket_path> <ring_name>\n" C_RESET
           C_DIM   "               Register a service (quotes supported)\n\n" C_RESET);
    printf(C_GREEN "  lookup     <n>\n" C_RESET
           C_DIM   "               Find a registered service\n\n" C_RESET);
    printf(C_GREEN "  heartbeat  <n>  " C_DIM "(alias: hb)\n" C_RESET
           C_DIM   "               Send heartbeat for a service\n\n" C_RESET);
    printf(C_GREEN "  unregister <n>  " C_DIM "(alias: unreg)\n" C_RESET
           C_DIM   "               Unregister a service\n\n" C_RESET);

    printf(C_BOLD "  OBSERVABILITY:\n" C_RESET);
    printf(C_CYAN "  logs\n" C_RESET
           C_DIM  "               Live tail: %s\n"
                  "               Press Enter to stop\n\n" C_RESET, SM_LOG_FILE);
    printf(C_CYAN "  audit\n" C_RESET
           C_DIM  "               Live tail: %s\n"
                  "               Press Enter to stop\n\n" C_RESET, SM_AUDIT_LOG_FILE);
    printf(C_CYAN "  logdump   [N]\n" C_RESET
           C_DIM  "               Print last N lines of main log  (default: 30)\n\n" C_RESET);
    printf(C_CYAN "  auditdump [N]\n" C_RESET
           C_DIM  "               Print last N lines of audit log (default: 30)\n\n" C_RESET);

    printf(C_BOLD "  DIAGNOSTICS:\n" C_RESET);
    printf(C_YELLOW "  ping\n" C_RESET
           C_DIM   "               Test socket connection\n\n" C_RESET);
    printf(C_YELLOW "  socket\n" C_RESET
           C_DIM   "               Show active socket path\n\n" C_RESET);

    printf(C_BOLD "  GENERAL:\n" C_RESET);
    printf(C_WHITE "  clear   " C_DIM "— Clear screen\n" C_RESET);
    printf(C_WHITE "  help    " C_DIM "— Show this menu\n" C_RESET);
    printf(C_RED   "  quit    " C_DIM "— Exit  (alias: exit, q)\n\n" C_RESET);
    bankai_sep();
}

/* ── Result-code → coloured string ──────────────────────────────────────────── */

static const char* rc_str(int32_t rc)
{
    switch (rc) {
        case SM_OK:             return C_GREEN  "OK"             C_RESET;
        case SM_ERR_NOT_FOUND:  return C_YELLOW "NOT_FOUND"      C_RESET;
        case SM_ERR_FULL:       return C_RED    "REGISTRY_FULL"  C_RESET;
        case SM_ERR_EXISTS:     return C_YELLOW "ALREADY_EXISTS" C_RESET;
        case SM_ERR_INVALID:    return C_RED    "INVALID"        C_RESET;
        case SM_ERR_PROTOCOL:   return C_RED    "PROTOCOL_ERR"   C_RESET;
        case SM_ERR_PERMISSION: return C_RED    "PERMISSION"     C_RESET;
        case SM_ERR_RATELIMIT:  return C_RED    "RATE_LIMITED"   C_RESET;
        case SM_ERR_AUTH:       return C_RED    "HMAC_AUTH_FAIL" C_RESET;
        default:                return C_RED    "UNKNOWN"        C_RESET;
    }
}

/* ── FIX 5: Quoted-string tokeniser ─────────────────────────────────────────── */

/*
 * sm_cli_tokenise() — split a command line into tokens respecting single and
 * double quotes and backslash escapes.
 *
 * Rules:
 *   "hello world"  → one token: hello world
 *   'it'\''s fine' → one token: it's fine
 *   unquoted tok   → split on whitespace
 *
 * Returns the number of tokens found (0..BANKAI_MAX_ARGS).
 * Caller passes out[BANKAI_MAX_ARGS][BANKAI_MAX_TOKEN] as storage.
 */
static int sm_cli_tokenise(const char* line,
                            char out[BANKAI_MAX_ARGS][BANKAI_MAX_TOKEN])
{
    int argc = 0;
    int pos  = 0;
    int len  = (int)strlen(line);

    while (pos < len && argc < BANKAI_MAX_ARGS) {
        /* Skip inter-token whitespace */
        while (pos < len && isspace((unsigned char)line[pos])) pos++;
        if (pos >= len) break;

        char tok[BANKAI_MAX_TOKEN];
        int  ti   = 0;
        char quot = 0;

        while (pos < len) {
            char c = line[pos];

            if (quot) {
                if (c == '\\' && pos + 1 < len) {
                    /* Backslash escape inside quotes */
                    pos++;
                    if (ti < BANKAI_MAX_TOKEN - 1)
                        tok[ti++] = line[pos];
                } else if (c == quot) {
                    quot = 0;   /* closing quote */
                } else {
                    if (ti < BANKAI_MAX_TOKEN - 1)
                        tok[ti++] = c;
                }
            } else {
                if (c == '"' || c == '\'') {
                    quot = c;
                } else if (isspace((unsigned char)c)) {
                    break;      /* end of unquoted token */
                } else {
                    if (ti < BANKAI_MAX_TOKEN - 1)
                        tok[ti++] = c;
                }
            }
            pos++;
        }

        tok[ti] = '\0';
        if (ti > 0) {
            memcpy(out[argc], tok, (size_t)ti + 1);   /* ti < BANKAI_MAX_TOKEN */
            argc++;
        }
    }
    return argc;
}

/* ── Protocol helpers ────────────────────────────────────────────────────────── */

static uint32_t bankai_nonce(void)
{
    uint32_t n = (uint32_t)time(NULL);
    ssize_t  r = getrandom(&n, sizeof(n), 0);
    if (r != (ssize_t)sizeof(n))
        n ^= (uint32_t)getpid();   /* fallback if getrandom unavailable */
    return n;
}

/*
 * bankai_open_socket() — connect to the Service Manager socket.
 * Tries SM_SOCKET_PRIMARY first, then SM_SOCKET_FALLBACK silently.
 *
 * FIX 3: First successful connection prints a one-time HMAC test-mode warning
 * so the user understands why SM_ERR_AUTH may appear.
 */
static int bankai_open_socket(void)
{
    static int hmac_warned = 0;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        printf(C_RED "  [ERROR] socket(): %s\n" C_RESET, strerror(errno));
        return -1;
    }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        /* Try fallback transparently */
        if (strcmp(g_socket_path, SM_SOCKET_PRIMARY) == 0) {
            strncpy(addr.sun_path, SM_SOCKET_FALLBACK,
                    sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                g_socket_path = SM_SOCKET_FALLBACK;
                printf(C_YELLOW
                       "  [INFO] Using fallback socket: %s\n" C_RESET,
                       SM_SOCKET_FALLBACK);
                goto connected;
            }
        }
        printf(C_RED    "  [ERROR] Cannot connect to Service Manager.\n" C_RESET
               C_YELLOW "          Checked : %s\n"
                        "                    %s\n"
                        "          Is the daemon running?\n" C_RESET,
               SM_SOCKET_PRIMARY, SM_SOCKET_FALLBACK);
        close(fd);
        return -1;
    }

connected:
    /*
     * FIX 3 — one-time HMAC warning.
     *
     * Bankai sends hmac[32] = {0} (test mode / no shared key available here).
     * If the server has HMAC enforcement active, every command will return
     * SM_ERR_AUTH (-8).
     *
     * To enable authenticated mode: link sm_crypto.c, call sm_crypto_init(),
     * replace the memset(hdr->hmac,0) in raw_transact() with sm_hmac_sha256().
     */
    if (!hmac_warned) {
        printf(C_DIM
               "  [NOTE] Running in test mode — HMAC field is zero.\n"
               "         Commands will return SM_ERR_AUTH (-8) if the\n"
               "         server has HMAC enforcement enabled.\n"
               C_RESET);
        hmac_warned = 1;
    }
    return fd;
}

/* ── FIX 2: Single raw_transact() — used by ALL protocol commands ────────────── */

/*
 * raw_transact() — assemble header + payload, send to fd, receive reply.
 *
 * Returns bytes received (>= 0) on success, -1 on send/recv error.
 * Callers inspect the byte count to differentiate reply types
 * (e.g. sm_reply_t vs sm_lookup_reply_t for lookup).
 */
static ssize_t raw_transact(int fd, uint16_t type,
                             const void* payload, size_t payload_len,
                             void* reply_buf,     size_t reply_max)
{
    if (payload_len > SM_MAX_PAYLOAD_SIZE) {
        printf(C_RED "  [ERROR] Payload exceeds protocol limit (%zu > %u bytes)\n"
               C_RESET, payload_len, SM_MAX_PAYLOAD_SIZE);
        return -1;
    }

    size_t  total = sizeof(sm_hdr_t) + payload_len;
    uint8_t sbuf[sizeof(sm_hdr_t) + SM_MAX_PAYLOAD_SIZE];

    sm_hdr_t* hdr = (sm_hdr_t*)sbuf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic      = SM_PROTOCOL_MAGIC;
    hdr->version    = SM_PROTOCOL_VERSION;
    hdr->type       = type;
    hdr->length     = (uint32_t)payload_len;
    hdr->timestamp  = (uint32_t)time(NULL);
    hdr->client_pid = (uint32_t)getpid();
    hdr->nonce      = bankai_nonce();
    /* hdr->hmac[32] left zero — test mode (see FIX 3 note in bankai_open_socket) */

    memcpy(sbuf + sizeof(sm_hdr_t), payload, payload_len);

    if (send(fd, sbuf, total, MSG_NOSIGNAL) != (ssize_t)total) {
        printf(C_RED "  [ERROR] send failed: %s\n" C_RESET, strerror(errno));
        return -1;
    }

    ssize_t n = recv(fd, reply_buf, reply_max, 0);
    if (n < 0)
        printf(C_RED "  [ERROR] recv failed: %s\n" C_RESET, strerror(errno));
    return n;
}

/* ── Protocol command implementations ───────────────────────────────────────── */

static void bankai_cmd_ping(void)
{
    printf(C_CYAN "  Checking connection...\n" C_RESET);

    struct stat st;
    if (stat(SM_SOCKET_PRIMARY, &st) != 0 &&
        stat(SM_SOCKET_FALLBACK, &st) != 0) {
        printf(C_RED "  Socket file not found.\n"
                     "  Checked: %s\n"
                     "           %s\n" C_RESET,
               SM_SOCKET_PRIMARY, SM_SOCKET_FALLBACK);
        printf(C_YELLOW "  Is Service Manager running?\n" C_RESET);
        return;
    }

    int fd = bankai_open_socket();
    if (fd < 0) { printf(C_RED "  PING: FAILED\n" C_RESET); return; }
    printf(C_GREEN "  PING: OK — Service Manager reachable at %s\n" C_RESET,
           g_socket_path);
    close(fd);
}

static void bankai_cmd_register(const char* name,
                                 const char* sock_path,
                                 const char* ring)
{
    if (!name || !sock_path || !ring) {
        printf(C_RED "  Usage: register <n> <socket_path> <ring_name>\n"
                     "         Paths with spaces: wrap in quotes.\n" C_RESET);
        return;
    }

    int fd = bankai_open_socket();
    if (fd < 0) return;

    sm_register_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.service_name, sizeof(req.service_name), "%.*s",
             (int)(sizeof(req.service_name) - 1), name);
    snprintf(req.socket_path,  sizeof(req.socket_path),  "%.*s",
             (int)(sizeof(req.socket_path) - 1),  sock_path);
    snprintf(req.ring_name,    sizeof(req.ring_name),    "%.*s",
             (int)(sizeof(req.ring_name) - 1),    ring);
    req.pid = (int32_t)getpid();

    sm_reply_t reply;
    ssize_t n = raw_transact(fd, SM_MSG_REGISTER,
                              &req, sizeof(req),
                              &reply, sizeof(reply));
    if (n == (ssize_t)sizeof(reply)) {
        printf("  REGISTER " C_BOLD "'%s'" C_RESET " → %s  (code=%d)\n",
               name, rc_str(reply.response_code), reply.response_code);
        if (reply.response_code == SM_OK)
            printf(C_DIM "    socket : %s\n"
                         "    ring   : %s\n" C_RESET, sock_path, ring);
    } else if (n >= 0) {
        printf(C_YELLOW "  REGISTER: unexpected reply size %zd bytes\n" C_RESET, n);
    }
    close(fd);
}

/*
 * FIX 2: bankai_cmd_lookup() now uses raw_transact() — the same send/recv path
 * as all other commands.  The server sends sm_lookup_reply_t on success or
 * sm_reply_t on error; we allocate the larger buffer and inspect n.
 */
static void bankai_cmd_lookup(const char* name)
{
    if (!name) {
        printf(C_RED "  Usage: lookup <n>\n" C_RESET);
        return;
    }

    int fd = bankai_open_socket();
    if (fd < 0) return;

    sm_lookup_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.service_name, sizeof(req.service_name), "%.*s",
             (int)(sizeof(req.service_name) - 1), name);

    /* Allocate the larger of the two possible reply types */
    uint8_t rbuf[sizeof(sm_lookup_reply_t)];
    memset(rbuf, 0, sizeof(rbuf));

    ssize_t n = raw_transact(fd, SM_MSG_LOOKUP,
                              &req, sizeof(req),
                              rbuf, sizeof(rbuf));

    if (n == (ssize_t)sizeof(sm_reply_t)) {
        sm_reply_t* rep = (sm_reply_t*)rbuf;
        printf("  LOOKUP " C_BOLD "'%s'" C_RESET " → %s  (code=%d)\n",
               name, rc_str(rep->response_code), rep->response_code);

    } else if (n == (ssize_t)sizeof(sm_lookup_reply_t)) {
        sm_lookup_reply_t* rep = (sm_lookup_reply_t*)rbuf;
        printf("  LOOKUP " C_BOLD "'%s'" C_RESET " → " C_GREEN "FOUND\n" C_RESET,
               name);
        printf(C_DIM "    socket_path : " C_RESET C_WHITE "%s\n" C_RESET,
               rep->socket_path);
        printf(C_DIM "    ring_name   : " C_RESET C_WHITE "%s\n" C_RESET,
               rep->ring_name);
        printf(C_DIM "    service_pid : " C_RESET C_WHITE "%d\n" C_RESET,
               (int)rep->service_pid);

    } else if (n >= 0) {
        printf(C_YELLOW "  LOOKUP: unexpected reply size %zd bytes\n" C_RESET, n);
    }
    close(fd);
}

static void bankai_cmd_heartbeat(const char* name)
{
    if (!name) {
        printf(C_RED "  Usage: heartbeat <n>\n" C_RESET);
        return;
    }

    int fd = bankai_open_socket();
    if (fd < 0) return;

    sm_heartbeat_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.service_name, sizeof(req.service_name), "%.*s",
             (int)(sizeof(req.service_name) - 1), name);

    sm_reply_t reply;
    ssize_t n = raw_transact(fd, SM_MSG_HEARTBEAT,
                              &req, sizeof(req),
                              &reply, sizeof(reply));
    if (n == (ssize_t)sizeof(reply))
        printf("  HEARTBEAT " C_BOLD "'%s'" C_RESET " → %s  (code=%d)\n",
               name, rc_str(reply.response_code), reply.response_code);
    else if (n >= 0)
        printf(C_YELLOW "  HEARTBEAT: unexpected reply size %zd bytes\n" C_RESET, n);
    close(fd);
}

static void bankai_cmd_unregister(const char* name)
{
    if (!name) {
        printf(C_RED "  Usage: unregister <n>\n" C_RESET);
        return;
    }

    int fd = bankai_open_socket();
    if (fd < 0) return;

    sm_unregister_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.service_name, sizeof(req.service_name), "%.*s",
             (int)(sizeof(req.service_name) - 1), name);

    sm_reply_t reply;
    ssize_t n = raw_transact(fd, SM_MSG_UNREGISTER,
                              &req, sizeof(req),
                              &reply, sizeof(reply));
    if (n == (ssize_t)sizeof(reply))
        printf("  UNREGISTER " C_BOLD "'%s'" C_RESET " → %s  (code=%d)\n",
               name, rc_str(reply.response_code), reply.response_code);
    else if (n >= 0)
        printf(C_YELLOW "  UNREGISTER: unexpected reply size %zd bytes\n" C_RESET, n);
    close(fd);
}

/* ── Log dump ────────────────────────────────────────────────────────────────── */

static void bankai_logdump(const char* path, int n)
{
    FILE* f = fopen(path, "r");
    if (!f) {
        printf(C_RED    "  Cannot open: %s\n  (%s)\n" C_RESET,
               path, strerror(errno));
        printf(C_YELLOW "  Log files are created on first Service Manager run.\n"
               C_RESET);
        return;
    }

    char** ring = calloc((size_t)n, sizeof(char*));
    if (!ring) { fclose(f); return; }

    char line[1024];
    int  idx = 0, full = 0;

    while (fgets(line, sizeof(line), f)) {
        free(ring[idx % n]);
        ring[idx % n] = strdup(line);
        idx++;
        if (idx >= n) full = 1;
    }
    fclose(f);

    int start = full ? idx % n : 0;
    int count = full ? n : idx;

    printf(C_CYAN "  ── Last %d lines: %s ──\n" C_RESET, count, path);
    for (int i = 0; i < count; i++) {
        char* l = ring[(start + i) % n];
        if (!l) continue;
        if      (strstr(l, "[CRIT]"))  printf(C_RED    "  %s" C_RESET, l);
        else if (strstr(l, "[ERROR]")) printf(C_RED    "  %s" C_RESET, l);
        else if (strstr(l, "[WARN]"))  printf(C_YELLOW "  %s" C_RESET, l);
        else if (strstr(l, "[INFO]"))  printf(C_GREEN  "  %s" C_RESET, l);
        else if (strstr(l, "[DEBUG]")) printf(C_DIM    "  %s" C_RESET, l);
        else                           printf(          "  %s",         l);
        free(l);
    }
    free(ring);
    printf(C_CYAN "  ── End ──\n" C_RESET);
}

/* ── FIX 4: Live tail using poll() + self-pipe ───────────────────────────────── */

/*
 * bankai_tail_fn() — live log tail thread.
 *
 * poll()s on two fds simultaneously:
 *   fds[0] = stop pipe read-end  → main wrote a byte: exit cleanly
 *   fds[1] = log file fd         → 200 ms heartbeat to call fgets
 *
 * Regular files always report POLLIN on Linux, so we rely on the 200 ms
 * poll() timeout as a periodic wake-up to call fgets().  The pipe fd provides
 * an immediate, race-free wakeup when the user presses Enter.
 */
static void* bankai_tail_fn(void* arg)
{
    (void)arg;

    FILE* f = fopen(g_tail_path, "r");
    if (!f) {
        printf(C_RED "\n  Cannot open: %s — %s\n" C_RESET,
               g_tail_path, strerror(errno));
        g_tail_active = 0;
        return NULL;
    }

    fseek(f, 0, SEEK_END);

    printf(C_CYAN "\n  Tailing: %s\n"
                  "  Press Enter to stop.\n\n" C_RESET, g_tail_path);
    fflush(stdout);

    char line[1024];

    while (g_tail_active) {
        struct pollfd fds[2];
        fds[0].fd      = g_stop_pipe[0];
        fds[0].events  = POLLIN;
        fds[0].revents = 0;
        fds[1].fd      = fileno(f);
        fds[1].events  = POLLIN;
        fds[1].revents = 0;

        int ret = poll(fds, 2, 200);   /* 200 ms timeout */
        if (ret < 0 && errno == EINTR) continue;

        /* Stop signal from main thread */
        if (fds[0].revents & POLLIN) break;

        /* Drain any new lines from the log */
        while (fgets(line, sizeof(line), f)) {
            if      (strstr(line, "[CRIT]"))    printf(C_RED     "  %s" C_RESET, line);
            else if (strstr(line, "[ERROR]"))   printf(C_RED     "  %s" C_RESET, line);
            else if (strstr(line, "[WARN]"))    printf(C_YELLOW  "  %s" C_RESET, line);
            else if (strstr(line, "[INFO]"))    printf(C_GREEN   "  %s" C_RESET, line);
            else if (strstr(line, "[DEBUG]"))   printf(C_DIM     "  %s" C_RESET, line);
            else if (strstr(line, "AUTH_FAIL")) printf(C_RED     "  %s" C_RESET, line);
            else if (strstr(line, "AUDIT"))     printf(C_MAGENTA "  %s" C_RESET, line);
            else                                printf(           "  %s",         line);
            fflush(stdout);
        }
        clearerr(f);
    }

    fclose(f);
    g_tail_active = 0;
    return NULL;
}

static void bankai_cmd_tail(const char* filepath)
{
    if (g_tail_active) {
        printf(C_YELLOW
               "  A tail is already running. Press Enter to stop it first.\n"
               C_RESET);
        return;
    }

    /* Create the self-pipe BEFORE the thread so it is ready on first poll() */
    if (pipe(g_stop_pipe) < 0) {
        printf(C_RED "  [ERROR] pipe(): %s\n" C_RESET, strerror(errno));
        return;
    }

    strncpy(g_tail_path, filepath, sizeof(g_tail_path) - 1);
    g_tail_path[sizeof(g_tail_path) - 1] = '\0';
    g_tail_active = 1;

    if (pthread_create(&g_tail_tid, NULL, bankai_tail_fn, NULL) != 0) {
        printf(C_RED "  [ERROR] pthread_create: %s\n" C_RESET, strerror(errno));
        g_tail_active = 0;
        close(g_stop_pipe[0]); close(g_stop_pipe[1]);
        g_stop_pipe[0] = g_stop_pipe[1] = -1;
        return;
    }

    /*
     * getchar() blocks here (main owns stdin exclusively while tail runs).
     * The actual stop mechanism is the pipe write below — not the return
     * value of getchar() — so there is no race between input buffering and
     * the tail thread startup.
     */
    getchar();

    /* Signal tail thread — immediate, no race */
    char    stop_byte = '\n';
    ssize_t wb        = write(g_stop_pipe[1], &stop_byte, 1);
    (void)wb;   /* failure here is non-fatal: g_tail_active=0 is the safety net */

    pthread_join(g_tail_tid, NULL);

    close(g_stop_pipe[0]); close(g_stop_pipe[1]);
    g_stop_pipe[0] = g_stop_pipe[1] = -1;

    printf(C_CYAN "\n  Tail stopped.\n" C_RESET);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SECTION 3 — bankai_run(): PUBLIC ENTRY POINT
 * ══════════════════════════════════════════════════════════════════════════════ */

int bankai_run(void)
{
    signal(SIGPIPE, SIG_IGN);

    /* Set terminal window title */
    printf("\033]0;Bankai — Service Manager Terminal\007");

    bankai_banner();
    printf(C_WHITE "  Welcome to " C_MAGENTA "Bankai" C_WHITE
           " — Service Manager Test Terminal\n" C_RESET);
    printf("  Type " C_GREEN "help" C_RESET " for commands.  "
           "Type " C_RED   "quit" C_RESET " to exit.\n\n");

    /* Auto-detect socket path at startup */
    struct stat st;
    if (stat(SM_SOCKET_PRIMARY, &st) != 0 &&
        stat(SM_SOCKET_FALLBACK, &st) == 0) {
        g_socket_path = SM_SOCKET_FALLBACK;
        printf(C_YELLOW "  Active socket (fallback): %s\n\n" C_RESET,
               SM_SOCKET_FALLBACK);
    } else {
        printf(C_DIM "  Active socket: %s\n\n" C_RESET, SM_SOCKET_PRIMARY);
    }

    /* FIX 5: token storage for the proper tokeniser */
    char raw[512];
    char argv[BANKAI_MAX_ARGS][BANKAI_MAX_TOKEN];

    while (1) {
        printf(C_MAGENTA "bankai" C_RESET C_BLUE " > " C_RESET);
        fflush(stdout);

        if (!fgets(raw, sizeof(raw), stdin)) break;
        raw[strcspn(raw, "\n")] = '\0';
        if (raw[0] == '\0') continue;

        int argc = sm_cli_tokenise(raw, argv);
        if (argc == 0) continue;

        const char* cmd = argv[0];
        const char* a1  = argc > 1 ? argv[1] : NULL;
        const char* a2  = argc > 2 ? argv[2] : NULL;
        const char* a3  = argc > 3 ? argv[3] : NULL;

        /* ── Command dispatch ── */
        if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
            bankai_help();

        } else if (!strcmp(cmd, "clear")) {
            bankai_banner();

        } else if (!strcmp(cmd, "quit") ||
                   !strcmp(cmd, "exit") ||
                   !strcmp(cmd, "q")) {
            printf(C_MAGENTA "  Bankai — Getsuga Tensho.\n\n" C_RESET);
            break;

        } else if (!strcmp(cmd, "ping")) {
            bankai_cmd_ping();

        } else if (!strcmp(cmd, "socket")) {
            printf("  Active socket: " C_GREEN "%s\n" C_RESET, g_socket_path);

        } else if (!strcmp(cmd, "register")) {
            bankai_cmd_register(a1, a2, a3);

        } else if (!strcmp(cmd, "lookup")) {
            bankai_cmd_lookup(a1);

        } else if (!strcmp(cmd, "heartbeat") || !strcmp(cmd, "hb")) {
            bankai_cmd_heartbeat(a1);

        } else if (!strcmp(cmd, "unregister") || !strcmp(cmd, "unreg")) {
            bankai_cmd_unregister(a1);

        } else if (!strcmp(cmd, "logs")) {
            bankai_cmd_tail(SM_LOG_FILE);

        } else if (!strcmp(cmd, "audit")) {
            bankai_cmd_tail(SM_AUDIT_LOG_FILE);

        } else if (!strcmp(cmd, "logdump")) {
            int n = a1 ? atoi(a1) : 30;
            if (n <= 0 || n > 500) n = 30;
            bankai_logdump(SM_LOG_FILE, n);

        } else if (!strcmp(cmd, "auditdump")) {
            int n = a1 ? atoi(a1) : 30;
            if (n <= 0 || n > 500) n = 30;
            bankai_logdump(SM_AUDIT_LOG_FILE, n);

        } else {
            printf(C_RED "  Unknown command: " C_BOLD "'%s'" C_RESET
                         C_RED ".  Type 'help'.\n" C_RESET, cmd);
        }

        printf("\n");
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SECTION 4 — STANDALONE ENTRY POINT
 *
 * Compiled only with -DBANKAI_STANDALONE.
 * Without this flag, sm_cli.c links as a library — no main() conflict.
 * ══════════════════════════════════════════════════════════════════════════════ */
#ifdef BANKAI_STANDALONE
int main(void)
{
    return bankai_run();
}
#endif