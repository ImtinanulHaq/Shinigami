#ifndef SM_SOCKET_H
#define SM_SOCKET_H

#include "sm_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>

// ── SOCKET CONFIGURATION ──────────────────────────────────────────────────────

#define SM_SOCKET_PATH      "/run/servicemanager.sock"
#define SM_SOCKET_MODE      0660
#define SM_BACKLOG          32

// ── FUNCTIONS ──────────────────────────────────────────────────────────────────

// Setup server socket
int  sm_socket_setup(void);

// Close server socket
void sm_socket_cleanup(void);

// Get server file descriptor
int  sm_socket_get_fd(void);

// Validate socket permissions
int  sm_socket_validate_perms(void);

#endif // SM_SOCKET_H
