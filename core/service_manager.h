#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <stdint.h>
#include <sys/types.h>

// ── CONSTANTS ──────────────────────────────────────────────────────────────────

#define SM_SOCKET_PATH      "/tmp/servicemanager.sock"  // unix socket path
#define SM_MAX_SERVICES     32                           // max services allowed
#define SM_MAX_NAME         64                           // service name max length
#define SM_MAX_PATH         128                          // path max length
#define SM_HEARTBEAT_TIMEOUT 10                          // seconds before restart

// ── MESSAGE TYPES (what client sends to service manager) ──────────────────────

#define SM_MSG_REGISTER     1   // service saying "i am alive, register me"
#define SM_MSG_LOOKUP       2   // app asking "where is audio service?"
#define SM_MSG_HEARTBEAT    3   // service saying "i am still alive"
#define SM_MSG_UNREGISTER   4   // service saying "i am shutting down"

// ── RESPONSE CODES ─────────────────────────────────────────────────────────────

#define SM_OK               0
#define SM_ERR_NOT_FOUND   -1
#define SM_ERR_FULL        -2
#define SM_ERR_EXISTS      -3
#define SM_ERR_INVALID     -4

// ── SERVICE STATUS ─────────────────────────────────────────────────────────────

typedef enum {
    SERVICE_RUNNING  = 0,
    SERVICE_STOPPED  = 1,
    SERVICE_CRASHED  = 2,
} service_status_t;

// ── SERVICE ENTRY — one record per registered service ─────────────────────────

typedef struct {
    char             name[SM_MAX_NAME];       // "audio", "sensor", "camera"
    char             socket_path[SM_MAX_PATH]; // "/tmp/audio.sock"
    char             ring_name[SM_MAX_PATH];   // "/ring_audio"
    pid_t            pid;                      // process id of service
    service_status_t status;
    time_t           last_heartbeat;           // timestamp of last heartbeat
} service_entry_t;

// ── MESSAGE — sent over unix socket between app/service and manager ───────────

typedef struct {
    int  type;                    // SM_MSG_REGISTER, LOOKUP, etc.
    char service_name[SM_MAX_NAME];
    char socket_path[SM_MAX_PATH];
    char ring_name[SM_MAX_PATH];
    int  pid;
    int  response_code;           // SM_OK or error code
} sm_message_t;

// ── FUNCTION DECLARATIONS ──────────────────────────────────────────────────────

// start the service manager daemon (blocks forever — runs the main loop)
int  sm_run(void);

// client-side: connect to service manager and register this service
int  sm_register(const char* name, const char* socket_path, const char* ring_name);

// client-side: lookup a service, fills socket_path and ring_name buffers
int  sm_lookup(const char* name, char* socket_path_out, char* ring_name_out);

// client-side: send heartbeat (call every few seconds from your service)
int  sm_heartbeat(const char* name);

// client-side: unregister before clean shutdown
int  sm_unregister(const char* name);

#endif // SERVICE_MANAGER_H