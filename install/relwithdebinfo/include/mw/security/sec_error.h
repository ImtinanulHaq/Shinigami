#ifndef SEC_ERROR_H
#define SEC_ERROR_H

typedef enum {
    SEC_OK                  =  0,
    SEC_ERR_INVALID_CONFIG  = -1,
    SEC_ERR_PERMISSION      = -2,
    SEC_ERR_SYSCALL         = -3,
    SEC_ERR_MEMORY          = -4,
    SEC_ERR_NOT_INITIALIZED = -5,
    SEC_ERR_ALREADY_APPLIED = -6,
    SEC_ERR_ROLLBACK_NEEDED = -7,
    SEC_ERR_INVALID_STATE   = -8,
    SEC_ERR_POLICY_NOT_FOUND = -9,
    SEC_ERR_AUDIT_FAILURE   = -10,
    SEC_ERR_CRYPTO_FAILURE  = -11,
    SEC_ERR_TOKEN_EXPIRED   = -12,
    SEC_ERR_REPLAY_DETECTED = -13,
    SEC_ERR_NAMESPACE_FAIL  = -14,
    SEC_ERR_CGROUP_FAIL     = -15,
    SEC_ERR_SECCOMP_FAIL    = -16,
    SEC_ERR_CAPABILITY_FAIL = -17,
    SEC_ERR_MOUNT_FAIL      = -18,
    SEC_ERR_NETWORK_FAIL    = -19,
} sec_error_t;

static inline const char* sec_error_str(sec_error_t err)
{
    switch (err) {
        case SEC_OK:                  return "success";
        case SEC_ERR_INVALID_CONFIG:  return "invalid configuration";
        case SEC_ERR_PERMISSION:      return "permission denied";
        case SEC_ERR_SYSCALL:         return "system call failed";
        case SEC_ERR_MEMORY:          return "memory allocation failed";
        case SEC_ERR_NOT_INITIALIZED: return "not initialized";
        case SEC_ERR_ALREADY_APPLIED: return "already applied";
        case SEC_ERR_ROLLBACK_NEEDED: return "rollback needed";
        case SEC_ERR_INVALID_STATE:   return "invalid state";
        case SEC_ERR_POLICY_NOT_FOUND: return "policy not found";
        case SEC_ERR_AUDIT_FAILURE:   return "audit failure";
        case SEC_ERR_CRYPTO_FAILURE:  return "crypto failure";
        case SEC_ERR_TOKEN_EXPIRED:   return "token expired";
        case SEC_ERR_REPLAY_DETECTED: return "replay detected";
        case SEC_ERR_NAMESPACE_FAIL:  return "namespace operation failed";
        case SEC_ERR_CGROUP_FAIL:     return "cgroup operation failed";
        case SEC_ERR_SECCOMP_FAIL:    return "seccomp operation failed";
        case SEC_ERR_CAPABILITY_FAIL: return "capability operation failed";
        case SEC_ERR_MOUNT_FAIL:      return "mount operation failed";
        case SEC_ERR_NETWORK_FAIL:    return "network operation failed";
        default:                      return "unknown error";
    }
}

#endif
