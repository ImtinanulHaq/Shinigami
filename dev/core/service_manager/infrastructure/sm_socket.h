#ifndef SM_SOCKET_H
#define SM_SOCKET_H

#include "../infrastructure/sm_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>

/* Well-known socket path - must be on a filesystem that supports Unix sockets */
#define SM_SOCKET_PATH  "/run/middleware/servicemanager.sock"

/* 0660: owner and group can connect; others cannot */
#define SM_SOCKET_MODE  0660

/* Listen backlog - kernel queue depth for unaccepted connections */
#define SM_BACKLOG      32

/* Create the server socket, bind, chmod, listen. Returns 0 on success. */
int  sm_socket_setup(void);

/* Get the actual socket path being used (might be /tmp fallback) */
const char* sm_socket_get_path(void);

/* Close server fd and unlink the socket file */
void sm_socket_cleanup(void);

/* Return the server file descriptor */
int  sm_socket_get_fd(void);

/* Verify and correct socket file permissions */
int  sm_socket_validate_perms(void);

#endif /* SM_SOCKET_H */