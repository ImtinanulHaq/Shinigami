#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "capabilities_core.h"
#include "capabilities_audit.h"
#include <errno.h>
#include <grp.h>
#include <linux/capability.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <unistd.h>

struct cap_data {
  __u32 effective;
  __u32 permitted;
  __u32 inheritable;
};

struct cap_header {
  __u32 version;
  int pid;
};

#define CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3

#define CAP_KILL_NUM 5
#define CAP_SETUID_NUM 7
#define CAP_NET_BIND_NUM 10
#define CAP_NET_RAW_NUM 13
#define CAP_SYS_RAWIO_NUM 17
#define CAP_SYS_ADMIN_NUM 21
#define CAP_LAST_CAP_NUM 40

static int set_caps_full(const capabilities_config_t *config);
static int set_capability_bounding_set(cap_flags_t bounding);
static __u32 flags_to_bits(cap_flags_t flags);

const char *capabilities_core_name(cap_flags_t cap) {
  switch (cap) {
  case MCAP_SYS_RAWIO:
    return "SYS_RAWIO";
  case MCAP_NET_BIND:
    return "NET_BIND_SERVICE";
  case MCAP_SYS_ADMIN:
    return "SYS_ADMIN";
  case MCAP_SETUID:
    return "SETUID";
  case MCAP_KILL:
    return "KILL";
  case MCAP_NET_RAW:
    return "NET_RAW";
  case MCAP_NONE:
    return "NONE";
  default:
    return "UNKNOWN";
  }
}

static __u32 flags_to_bits(cap_flags_t flags) {
  __u32 bits = 0;
  if (flags & MCAP_SYS_RAWIO)
    bits |= (1U << CAP_SYS_RAWIO_NUM);
  if (flags & MCAP_NET_BIND)
    bits |= (1U << CAP_NET_BIND_NUM);
  if (flags & MCAP_SYS_ADMIN)
    bits |= (1U << CAP_SYS_ADMIN_NUM);
  if (flags & MCAP_SETUID)
    bits |= (1U << CAP_SETUID_NUM);
  if (flags & MCAP_KILL)
    bits |= (1U << CAP_KILL_NUM);
  if (flags & MCAP_NET_RAW)
    bits |= (1U << CAP_NET_RAW_NUM);
  return bits;
}

static int set_caps_full(const capabilities_config_t *config) {
  struct cap_header hdr;
  struct cap_data data[2];

  memset(&hdr, 0, sizeof(hdr));
  memset(data, 0, sizeof(data));

  hdr.version = CAPABILITY_VERSION;
  hdr.pid = 0;

  __u32 eff = flags_to_bits(config->effective);
  __u32 perm = flags_to_bits(config->permitted);
  __u32 inh = flags_to_bits(config->inheritable);

  data[0].effective = eff;
  data[0].permitted = perm;
  data[0].inheritable = inh;
  data[1].effective = 0;
  data[1].permitted = 0;
  data[1].inheritable = 0;

  if (syscall(SYS_capset, &hdr, data) < 0) {
    syslog(LOG_ERR, "[cap_core] capset failed: %s", strerror(errno));
    return -1;
  }

  capabilities_audit_log("SET", config->effective, "applied configuration");
  return 0;
}

static int set_capability_bounding_set(cap_flags_t bounding) {
  __u32 bounding_bits = flags_to_bits(bounding);

  for (int cap = 0; cap <= CAP_LAST_CAP_NUM; cap++) {
    __u32 cap_bit = (1U << (cap % 32));
    __u32 word = (cap < 32) ? bounding_bits : 0;

    if (!(word & cap_bit)) {
      if (prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) < 0) {
        if (errno != EINVAL && errno != EPERM) {
          syslog(LOG_WARNING, "[cap_core] bounding set drop cap %d: %s", cap,
                 strerror(errno));
        }
      }
    }
  }

  capabilities_audit_log("BOUND", bounding, "bounding set applied");
  return 0;
}

int capabilities_core_check(cap_flags_t cap) {
  struct cap_header hdr;
  struct cap_data data[2];

  memset(&hdr, 0, sizeof(hdr));
  hdr.version = CAPABILITY_VERSION;
  hdr.pid = 0;

  if (syscall(SYS_capget, &hdr, data) < 0) {
    return 0;
  }

  __u32 effective = data[0].effective;
  __u32 check_bits = flags_to_bits(cap);

  return (effective & check_bits) == check_bits;
}

int capabilities_core_apply_config(const capabilities_config_t *config) {
  if (!config) {
    syslog(LOG_ERR, "[cap_core] NULL configuration");
    return -1;
  }

  capabilities_audit_log("START", MCAP_NONE,
                         "beginning capability configuration");

  if (set_capability_bounding_set(config->bounding) < 0) {
    syslog(LOG_ERR, "[cap_core] bounding set configuration failed");
    return -1;
  }

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
    syslog(LOG_ERR, "[cap_core] PR_SET_NO_NEW_PRIVS failed: %s",
           strerror(errno));
    return -1;
  }
  capabilities_audit_log("LOCKED", MCAP_NONE, "privilege escalation disabled");

  if (config->target_uid > 0 && config->target_gid > 0) {
    uid_t current_uid = getuid();

    if (current_uid == 0) {

      if (setgroups(0, NULL) < 0) {
        syslog(LOG_ERR, "[cap_core] setgroups failed: %s", strerror(errno));
        return -1;
      }

      if (setgid(config->target_gid) < 0) {
        syslog(LOG_ERR, "[cap_core] setgid(%u) failed: %s", config->target_gid,
               strerror(errno));
        return -1;
      }

      /* TELL THE KERNEL NOT TO WIPE CAPABILITIES ON SETUID */
      if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) < 0) {
        syslog(LOG_ERR, "[cap_core] PR_SET_KEEPCAPS failed: %s",
               strerror(errno));
        return -1;
      }

      if (setuid(config->target_uid) < 0) {
        syslog(LOG_ERR, "[cap_core] setuid(%u) failed: %s", config->target_uid,
               strerror(errno));
        return -1;
      }

      /* Turn KEEPCAPS back off after the transition */
      prctl(PR_SET_KEEPCAPS, 0, 0, 0, 0);

      if (setuid(0) == 0) {
        syslog(LOG_CRIT,
               "[cap_core] SECURITY FAILURE: regained root privileges!");
        return -1;
      }

      char details[128];
      snprintf(details, sizeof(details), "changed to uid=%u gid=%u",
               config->target_uid, config->target_gid);
      capabilities_audit_log("USER", MCAP_NONE, details);

    } else if (current_uid != config->target_uid) {

      syslog(LOG_WARNING,
             "[cap_core] Cannot change from uid %u to %u (not root)",
             current_uid, config->target_uid);
    }
  }

  if (set_caps_full(config) < 0) {
    return -1;
  }

  syslog(LOG_INFO,
         "[cap_core] Configuration applied: effective=%s(0x%x) "
         "permitted=%s(0x%x) bounding=%s(0x%x) uid=%u gid=%u",
         capabilities_core_name(config->effective), config->effective,
         capabilities_core_name(config->permitted), config->permitted,
         capabilities_core_name(config->bounding), config->bounding, getuid(),
         getgid());

  capabilities_audit_log("COMPLETE", config->effective,
                         "configuration successful");
  return 0;
}

static int set_caps_legacy(uint32_t keep_bits) {
  struct cap_header hdr;
  struct cap_data data[2];

  memset(&hdr, 0, sizeof(hdr));
  memset(data, 0, sizeof(data));

  hdr.version = CAPABILITY_VERSION;
  hdr.pid = 0;

  data[0].permitted = keep_bits;
  data[0].effective = keep_bits;
  data[0].inheritable = 0;
  data[1].permitted = 0;
  data[1].effective = 0;
  data[1].inheritable = 0;

  if (syscall(SYS_capset, &hdr, data) < 0) {
    syslog(LOG_ERR, "[cap_core] legacy capset failed: %s", strerror(errno));
    return -1;
  }
  return 0;
}

int capabilities_core_drop_except(cap_flags_t keep_flags) {
  syslog(LOG_WARNING,
         "[cap_core] Using deprecated capabilities_core_drop_except - migrate "
         "to capabilities_core_apply_config");

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
    syslog(LOG_ERR, "[cap_core] PR_SET_NO_NEW_PRIVS failed: %s",
           strerror(errno));
    return -1;
  }
  syslog(LOG_INFO, "[cap_core] NO_NEW_PRIVS set - escalation locked");

  if (getuid() == 0) {
    uid_t uid = 1000, gid = 1000;

    if (setgroups(0, NULL) < 0) {
      syslog(LOG_ERR, "[cap_core] setgroups failed: %s", strerror(errno));
      return -1;
    }

    if (setgid(gid) < 0 || setuid(uid) < 0) {
      syslog(LOG_ERR, "[cap_core] privilege drop failed: %s", strerror(errno));
      return -1;
    }

    if (setuid(0) == 0) {
      syslog(LOG_CRIT, "[cap_core] SECURITY FAILURE: regained root");
      return -1;
    }

    syslog(LOG_INFO, "[cap_core] dropped root → uid=%u", uid);
  }

  __u32 bits = flags_to_bits(keep_flags);
  if (set_caps_legacy(bits) < 0) {
    return -1;
  }

  syslog(LOG_INFO, "[cap_core] %s",
         keep_flags ? "kept specific capabilities"
                    : "all capabilities dropped");
  capabilities_audit_log("DROP_EXCEPT", keep_flags, "legacy interface used");
  return 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
int capabilities_core_drop_all(void) {
  return capabilities_core_drop_except(MCAP_NONE);
}
#pragma GCC diagnostic pop

#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_RAISE 2
#define PR_CAP_AMBIENT_LOWER 3
#define PR_CAP_AMBIENT_CLEAR_ALL 4
#endif

int capabilities_core_set_ambient(cap_flags_t caps) {
  if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0) {
    syslog(LOG_WARNING, "[cap_core] ambient clear failed: %s", strerror(errno));
  }

  static const struct {
    cap_flags_t flag;
    int cap_num;
  } cap_map[] = {{MCAP_SYS_RAWIO, CAP_SYS_RAWIO_NUM},
                 {MCAP_NET_BIND, CAP_NET_BIND_NUM},
                 {MCAP_SYS_ADMIN, CAP_SYS_ADMIN_NUM},
                 {MCAP_SETUID, CAP_SETUID_NUM},
                 {MCAP_KILL, CAP_KILL_NUM},
                 {MCAP_NET_RAW, CAP_NET_RAW_NUM},
                 {0, 0}};

  for (int i = 0; cap_map[i].flag; i++) {
    if (caps & cap_map[i].flag) {
      if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap_map[i].cap_num, 0,
                0) < 0) {
        syslog(LOG_ERR, "[cap_core] ambient raise cap %d failed: %s",
               cap_map[i].cap_num, strerror(errno));
        return -1;
      }
    }
  }

  capabilities_audit_log("AMBIENT", caps, "ambient capabilities set");
  return 0;
}
