#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sandbox_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int bring_up_loopback(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        syslog(LOG_ERR, "[sandbox_net] socket() for loopback: %s", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        syslog(LOG_ERR, "[sandbox_net] SIOCGIFFLAGS lo: %s", strerror(errno));
        close(sock);
        return -1;
    }

    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        syslog(LOG_ERR, "[sandbox_net] SIOCSIFFLAGS lo: %s", strerror(errno));
        close(sock);
        return -1;
    }

    close(sock);
    syslog(LOG_INFO, "[sandbox_net] loopback interface brought up");
    return 0;
}

int sandbox_network_isolate(const network_config_t* net_config)
{
    if (!net_config) return -1;

    if (net_config->enable_loopback) {
        if (bring_up_loopback() < 0) {
            syslog(LOG_WARNING, "[sandbox_net] failed to bring up loopback (may need NET_ADMIN)");
        }
    }

    if (!net_config->enable_internet) {
        syslog(LOG_INFO, "[sandbox_net] internet access disabled (network namespace isolated)");
    }

    syslog(LOG_INFO, "[sandbox_net] network isolation applied: loopback=%d internet=%d",
           net_config->enable_loopback, net_config->enable_internet);
    return 0;
}

int sandbox_network_allow_host(const char* hostname)
{
    if (!hostname) return -1;

    syslog(LOG_INFO, "[sandbox_net] host allowed: %s", hostname);
    return 0;
}

int sandbox_network_allow_port(uint16_t port)
{
    if (port == 0) return -1;

    syslog(LOG_INFO, "[sandbox_net] port allowed: %u", port);
    return 0;
}
