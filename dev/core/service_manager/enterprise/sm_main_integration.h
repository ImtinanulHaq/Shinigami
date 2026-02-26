/*
 * sm_main_integration.h - Integration point for all 10 enterprise features
 *
 * This file provides the initialization and integration of:
 * 1. Thread Pool (sm_threadpool.h)
 * 2. Service Discovery (sm_discovery.h)
 * 3. Watchdog Timer (sm_watchdog.h)
 * 4. Resource Monitoring (sm_monitoring.h)
 * 5. TLS Transport (sm_tls.h)
 * 6. Rolling Restart (sm_rolling_restart.h)
 * 7. Plugin System (sm_plugin.h)
 * 8. Container Support (sm_container.h)
 * 9. Event Bus (sm_eventbus.h)
 * 10. CLI Interface (sm_cli.h)
 *
 * Include this in main.c and call:
 *   sm_main_init_all_features()  // At startup
 *   sm_main_cleanup_all_features()  // At shutdown
 */

#ifndef SM_MAIN_INTEGRATION_H
#define SM_MAIN_INTEGRATION_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * sm_main_init_all_features()
 * 
 * Initialize all 10 enterprise features.
 * Call this once at service manager startup.
 * 
 * RETURNS:
 *   0 on success
 *   -1 if any feature fails to initialize (logs which ones failed)
 */
int sm_main_init_all_features(void);

/*
 * sm_main_cleanup_all_features()
 * 
 * Gracefully shutdown all active features.
 * Call this during service manager shutdown.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_main_cleanup_all_features(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_MAIN_INTEGRATION_H */
