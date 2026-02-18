#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <sys/types.h>

// well-known Linux capabilities that services might need
typedef enum {
    CAP_NONE           = 0,
    CAP_SYS_RAWIO      = 1 << 0,  // direct hardware I/O (audio, camera)
    CAP_NET_BIND       = 1 << 1,  // bind to ports < 1024
    CAP_SYS_ADMIN      = 1 << 2,  // various admin operations (use sparingly)
    CAP_SETUID         = 1 << 3,  // change uid/gid
} cap_flags_t;

// drop ALL capabilities except those in keep_flags
// also sets PR_SET_NO_NEW_PRIVS so process can never gain privileges
// returns 0 on success, -1 on failure
int capabilities_drop_except(cap_flags_t keep_flags);

// drop all capabilities completely - service runs with zero privileges
int capabilities_drop_all(void);

#endif