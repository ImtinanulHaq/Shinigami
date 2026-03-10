/**
 * @file    collector_security.h
 * @brief   Collector for security metrics (seccomp, hmac, replay).
 */
#pragma once
#include "collector_base.h"

int collector_security_register(void);