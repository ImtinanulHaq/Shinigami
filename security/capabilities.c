#define _GNU_SOURCE
#include "capabilities.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>            // for setgroups
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/capability.h>
#include <errno.h>

// capability structures for kernel syscalls
struct cap_data {
    __u32 effective;
    __u32 permitted;
    __u32 inheritable;
};

struct cap_header {
    __u32 version;
    int   pid;
};

#define CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3

// Linux capability numbers
#define CAP_SETUID_NUM       7
#define CAP_NET_BIND_NUM    10
#define CAP_SYS_RAWIO_NUM   17
#define CAP_SYS_ADMIN_NUM   21

// convert flags to capability bits
static __u32 flags_to_bits(cap_flags_t flags)
{
    __u32 bits = 0;
    if (flags & CAP_SYS_RAWIO) bits |= (1U << CAP_SYS_RAWIO_NUM);
    if (flags & CAP_NET_BIND)  bits |= (1U << CAP_NET_BIND_NUM);
    if (flags & CAP_SYS_ADMIN) bits |= (1U << CAP_SYS_ADMIN_NUM);
    if (flags & CAP_SETUID)    bits |= (1U << CAP_SETUID_NUM);
    return bits;
}

// set process capabilities via capset syscall
static int set_caps(__u32 keep_bits)
{
    struct cap_header hdr;
    struct cap_data data[2];
    
    memset(&hdr, 0, sizeof(hdr));
    memset(data, 0, sizeof(data));
    
    hdr.version = CAPABILITY_VERSION;
    hdr.pid = 0;
    
    data[0].permitted   = keep_bits;
    data[0].effective   = keep_bits;
    data[0].inheritable = 0;
    data[1].permitted   = 0;
    data[1].effective   = 0;
    data[1].inheritable = 0;
    
    if (syscall(SYS_capset, &hdr, data) < 0) {
        perror("[cap] capset");
        return -1;
    }
    return 0;
}

int capabilities_drop_except(cap_flags_t keep_flags)
{
    // step 1: lock privilege escalation - critical security
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("[cap] PR_SET_NO_NEW_PRIVS");
        return -1;
    }
    printf("[cap] NO_NEW_PRIVS set - escalation locked\n");
    
    // step 2: drop to unprivileged user if root
    if (getuid() == 0) {
        uid_t uid = 1000, gid = 1000;
        
        setgroups(0, NULL);
        if (setgid(gid) < 0 || setuid(uid) < 0) {
            perror("[cap] drop root");
            return -1;
        }
        
        // verify cannot regain root
        if (setuid(0) == 0) {
            fprintf(stderr, "[cap] ERROR: regained root\n");
            return -1;
        }
        printf("[cap] dropped root → uid=%u\n", uid);
    }
    
    // step 3: set capabilities
    __u32 bits = flags_to_bits(keep_flags);
    if (set_caps(bits) < 0) return -1;
    
    printf("[cap] %s\n", keep_flags ? "kept specific caps" : "all caps dropped");
    return 0;
}

int capabilities_drop_all(void)
{
    return capabilities_drop_except(CAP_NONE);
}