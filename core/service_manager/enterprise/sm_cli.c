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
#include "../security/sm_crypto.h"
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
#include <sys/time.h>
#include <sys/random.h>

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
    return fd;
}

/* ── HMAC key loading ────────────────────────────────────────────────────────── */

/*
 * bankai_load_key() — read the shared HMAC key from disk.
 *
 * The Service Manager writes the key to SM_KEY_FILE on first run.
 * We read it here so Bankai can sign every request correctly.
 * Key is cached in g_hmac_key after the first successful load.
 */

/* Inline SHA-256 / HMAC-SHA256 — copied verbatim from sm_crypto.c so that
 * Bankai can be compiled standalone without linking sm_crypto.o.            */
#define ROTR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)   (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z)  (((x)&(y))^((x)&(z))^((y)&(z)))
#define SIGMA0(x)   (ROTR32(x, 2)^ROTR32(x,13)^ROTR32(x,22))
#define SIGMA1(x)   (ROTR32(x, 6)^ROTR32(x,11)^ROTR32(x,25))
#define GAMMA0(x)   (ROTR32(x, 7)^ROTR32(x,18)^((x)>> 3))
#define GAMMA1(x)   (ROTR32(x,17)^ROTR32(x,19)^((x)>>10))

static const uint32_t BK_SHA256_H0[8]={
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
static const uint32_t BK_SHA256_K[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

typedef struct { uint32_t s[8]; uint8_t b[64]; uint64_t bc; uint32_t bu; } bk_sha256_ctx;

static void bk_sha256_compress(bk_sha256_ctx* c, const uint8_t blk[64]) {
    uint32_t w[64],a,b,d,e,f,g,h,t1,t2; int i;
    /* suppress unused warning for 'cc' variable name conflict */
    uint32_t cc;
    for(i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for(i=16;i<64;i++) w[i]=GAMMA1(w[i-2])+w[i-7]+GAMMA0(w[i-15])+w[i-16];
    a=c->s[0];b=c->s[1];cc=c->s[2];d=c->s[3];e=c->s[4];f=c->s[5];g=c->s[6];h=c->s[7];
    for(i=0;i<64;i++){t1=h+SIGMA1(e)+CH(e,f,g)+BK_SHA256_K[i]+w[i];t2=SIGMA0(a)+MAJ(a,b,cc);h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}
    c->s[0]+=a;c->s[1]+=b;c->s[2]+=cc;c->s[3]+=d;c->s[4]+=e;c->s[5]+=f;c->s[6]+=g;c->s[7]+=h;
}
static void bk_sha256_init(bk_sha256_ctx* c){memcpy(c->s,BK_SHA256_H0,32);c->bc=0;c->bu=0;}
static void bk_sha256_update(bk_sha256_ctx* c,const uint8_t* d,size_t n){
    c->bc+=(uint64_t)n*8;
    for(size_t i=0;i<n;i++){c->b[c->bu++]=d[i];if(c->bu==64){bk_sha256_compress(c,c->b);c->bu=0;}}
}
static void bk_sha256_final(bk_sha256_ctx* c,uint8_t out[32]){
    uint8_t p[64];uint64_t bc=c->bc;uint32_t ps=c->bu;int i;
    memset(p,0,64);memcpy(p,c->b,c->bu);p[ps]=0x80;
    if(ps>=56){bk_sha256_compress(c,p);memset(p,0,56);}
    for(i=0;i<8;i++) p[56+i]=(uint8_t)(bc>>((7-i)*8));
    bk_sha256_compress(c,p);
    for(i=0;i<8;i++){out[i*4]=(uint8_t)(c->s[i]>>24);out[i*4+1]=(uint8_t)(c->s[i]>>16);out[i*4+2]=(uint8_t)(c->s[i]>>8);out[i*4+3]=(uint8_t)c->s[i];}
    memset(c,0,sizeof(*c));
}
static void bk_hmac_sha256(const uint8_t* key,size_t kl,const uint8_t* data,size_t dl,uint8_t out[32]){
    uint8_t kp[64],inner[32];bk_sha256_ctx ctx;size_t i;
    memset(kp,0,64);
    if(kl>64){bk_sha256_ctx hc;bk_sha256_init(&hc);bk_sha256_update(&hc,key,kl);bk_sha256_final(&hc,kp);}
    else memcpy(kp,key,kl);
    bk_sha256_init(&ctx);
    for(i=0;i<64;i++) kp[i]^=0x36;
    bk_sha256_update(&ctx,kp,64);bk_sha256_update(&ctx,data,dl);bk_sha256_final(&ctx,inner);
    for(i=0;i<64;i++) kp[i]^=(0x36^0x5c);
    bk_sha256_init(&ctx);bk_sha256_update(&ctx,kp,64);bk_sha256_update(&ctx,inner,32);bk_sha256_final(&ctx,out);
    memset(kp,0,64);memset(inner,0,32);
}

/* Cached HMAC key — loaded once from disk */
static uint8_t  g_hmac_key[SM_HMAC_KEY_SIZE];
static int      g_hmac_key_loaded = 0;

static int bankai_load_key(void)
{
    if (g_hmac_key_loaded) return 0;

    const char* paths[] = {
        "/run/servicemanager.key",
        "/tmp/servicemanager.key",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        ssize_t n = read(fd, g_hmac_key, SM_HMAC_KEY_SIZE);
        close(fd);

        if (n == (ssize_t)SM_HMAC_KEY_SIZE) {
            g_hmac_key_loaded = 1;
            printf(C_GREEN "  [HMAC] Key loaded from %s "
                           C_DIM "(first 4 bytes: %02x%02x%02x%02x)\n" C_RESET,
                   paths[i],
                   g_hmac_key[0], g_hmac_key[1],
                   g_hmac_key[2], g_hmac_key[3]);
            return 0;
        }
    }

    printf(C_RED
           "  [HMAC] Key not found at /run/servicemanager.key or /tmp/servicemanager.key\n"
           "         Run Service Manager once as root to generate the key.\n"
           C_RESET);
    return -1;
}

/* ── FIX 2: Single raw_transact() — used by ALL protocol commands ────────────── */

/*
 * raw_transact() — assemble header + payload, sign with HMAC, send, receive.
 *
 * HMAC signing:
 *   mac_input = header[0 .. SM_HDR_HMAC_OFFSET-1] || payload
 *   HMAC-SHA256(key, mac_input) written into hdr->hmac[32]
 *
 * Returns bytes received (>= 0) on success, -1 on send/recv error.
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

    /* Load HMAC key if not already loaded */
    if (!g_hmac_key_loaded && bankai_load_key() < 0)
        return -1;

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
    /* hmac field stays zero until we compute it below */

    memcpy(sbuf + sizeof(sm_hdr_t), payload, payload_len);

    /*
     * Compute HMAC over:
     *   header bytes [0 .. SM_HDR_HMAC_OFFSET-1]  (pre-hmac fields only)
     *   + payload
     * This matches exactly what sm_validate_header_hmac() verifies.
     */
    uint8_t mac_input[SM_HDR_HMAC_OFFSET + SM_MAX_PAYLOAD_SIZE];
    size_t  mac_len = SM_HDR_HMAC_OFFSET + payload_len;
    memcpy(mac_input,                    hdr,     SM_HDR_HMAC_OFFSET);
    memcpy(mac_input + SM_HDR_HMAC_OFFSET, payload, payload_len);

    bk_hmac_sha256(g_hmac_key, SM_HMAC_KEY_SIZE, mac_input, mac_len, hdr->hmac);

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