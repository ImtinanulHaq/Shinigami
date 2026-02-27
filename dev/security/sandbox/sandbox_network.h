#ifndef SANDBOX_NETWORK_H
#define SANDBOX_NETWORK_H

#include "sandbox.h"

int sandbox_network_isolate(const network_config_t* net_config);
int sandbox_network_allow_host(const char* hostname);
int sandbox_network_allow_port(uint16_t port);

#endif
