/**
 * @file    collector_sysinfo.h
 * @brief   Collector for system info metrics (CPU, RAM, swap, load).
 */
#pragma once
#include "collector_base.h"

int collector_sysinfo_register(void);