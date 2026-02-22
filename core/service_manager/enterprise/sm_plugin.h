/*
 * sm_plugin.h - Plugin/hook system for extensibility
 *
 * Load external scripts and shared libraries for:
 *   - Custom health checks
 *   - Custom restart logic
 *   - Notifications (Slack, email, webhooks)
 *   - Pre/post-restart hooks
 * 
 * USAGE:
 *   sm_plugin_init(plugin_dir);
 *   sm_plugin_load_script("health_check", "/path/to/health_check.sh");
 *   sm_plugin_load_library("notify", "/path/to/libnotify.so");
 *   
 *   sm_plugin_execute("health_check", "myservice", output_buffer, sizeof(output_buffer));
 *   sm_plugin_cleanup();
 */

#ifndef SM_PLUGIN_H
#define SM_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plugin hook types */
typedef enum {
    PLUGIN_HEALTH_CHECK = 1,      /* Custom health check */
    PLUGIN_PRE_RESTART = 2,       /* Before service restart */
    PLUGIN_POST_RESTART = 3,      /* After service restart */
    PLUGIN_NOTIFY = 4,            /* Send notifications */
    PLUGIN_ON_CRASH = 5,          /* Service crashed event */
    PLUGIN_ON_STARTUP = 6,        /* Service started event */
    PLUGIN_CUSTOM = 7,            /* User-defined */
} sm_plugin_type_t;

/* Plugin info */
typedef struct {
    char name[64];
    char path[256];
    sm_plugin_type_t type;
    int is_script;  /* 1 if script, 0 if shared library */
    int enabled;
    uint64_t execution_count;
    uint64_t failure_count;
} sm_plugin_info_t;

/* Plugin execution callback for async runs */
typedef void (*sm_plugin_callback_t)(const char* plugin_name, int exit_code, void* userdata);

/*
 * sm_plugin_init(plugin_directory)
 * 
 * Initialize plugin system and scan plugin directory.
 * 
 * PARAMETERS:
 *   plugin_directory - path to directory containing plugins (e.g., /etc/servicemanager/plugins/)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (directory not found, permission denied, etc)
 */
int sm_plugin_init(const char* plugin_directory);

/*
 * sm_plugin_register_script(name, script_path, plugin_type)
 * 
 * Register an external script plugin.
 * 
 * PARAMETERS:
 *   name - plugin identifier
 *   script_path - path to executable script
 *   plugin_type - PLUGIN_HEALTH_CHECK, PLUGIN_NOTIFY, etc
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (file not found, not executable)
 */
int sm_plugin_register_script(const char* name, const char* script_path, sm_plugin_type_t plugin_type);

/*
 * sm_plugin_register_library(name, library_path, entry_point_function)
 * 
 * Register a shared library plugin.
 * 
 * PARAMETERS:
 *   name - plugin identifier
 *   library_path - path to .so file
 *   entry_point_function - function name to call (e.g., "notify_handler")
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (not found, cannot dlopen, symbol not found)
 */
int sm_plugin_register_library(const char* name, const char* library_path,
                               const char* entry_point_function);

/*
 * sm_plugin_execute(plugin_name, argument, output_buffer, output_size)
 * 
 * Synchronously execute a plugin and get output.
 * 
 * PARAMETERS:
 *   plugin_name - registered plugin name
 *   argument - argument to pass (e.g., service name)
 *   output_buffer - where to store output
 *   output_size - max output size
 * 
 * RETURNS:
 *   Exit code from plugin (0 = success)
 *   -1 on error (not found, execution failed)
 * 
 * NOTES:
 *   - Blocks until plugin completes (with timeout)
 *   - Plugin runs with timeout limit to prevent hangs
 */
int sm_plugin_execute(const char* plugin_name, const char* argument,
                      char* output_buffer, int output_size);

/*
 * sm_plugin_execute_async(plugin_name, argument, callback, userdata)
 * 
 * Asynchronously execute a plugin with callback on completion.
 * 
 * PARAMETERS:
 *   plugin_name - registered plugin name
 *   argument - argument to pass
 *   callback - function to call when complete (NULL for fire-and-forget)
 *   userdata - context to pass to callback
 * 
 * RETURNS:
 *   0 on success (started async)
 *   -1 on error
 */
int sm_plugin_execute_async(const char* plugin_name, const char* argument,
                           sm_plugin_callback_t callback, void* userdata);

/*
 * sm_plugin_set_execution_timeout(timeout_seconds)
 * 
 * Set maximum time allowed for any plugin execution.
 * 
 * PARAMETERS:
 *   timeout_seconds - max execution time (default 30, max 300)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on invalid input
 */
int sm_plugin_set_execution_timeout(int timeout_seconds);

/*
 * sm_plugin_enable(plugin_name)
 * 
 * Enable a registered plugin.
 * 
 * RETURNS:
 *   0 on success
 *   -1 if not found
 */
int sm_plugin_enable(const char* plugin_name);

/*
 * sm_plugin_disable(plugin_name)
 * 
 * Disable a registered plugin (won't execute).
 * 
 * RETURNS:
 *   0 on success
 *   -1 if not found
 */
int sm_plugin_disable(const char* plugin_name);

/*
 * sm_plugin_get_info(plugin_name)
 * 
 * Get plugin metadata and statistics.
 * 
 * RETURNS:
 *   Plugin info (or zero-filled if not found)
 */
sm_plugin_info_t sm_plugin_get_info(const char* plugin_name);

/*
 * sm_plugin_list(buffer, buffer_size)
 * 
 * Get list of all registered plugins.
 * 
 * PARAMETERS:
 *   buffer - where to store plugin names (JSON format)
 *   buffer_size - max size
 * 
 * RETURNS:
 *   Number of plugins found
 */
int sm_plugin_list(char* buffer, int buffer_size);

/*
 * sm_plugin_cleanup()
 * 
 * Shutdown plugin system and unload all plugins.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_plugin_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_PLUGIN_H */
