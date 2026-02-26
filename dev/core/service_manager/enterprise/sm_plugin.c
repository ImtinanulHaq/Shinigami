#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/*
 * sm_plugin.c - Plugin system implementation
 *
 * Script and library plugin execution with timeouts
 */

#include "../enterprise/sm_plugin.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <dlfcn.h>
#include <time.h>

#define MAX_PLUGINS 64
#define DEFAULT_PLUGIN_TIMEOUT 30

typedef struct {
    char name[64];
    char path[256];
    sm_plugin_type_t type;
    int is_script;
    int enabled;
    uint64_t execution_count;
    uint64_t failure_count;
    void* library_handle;
    void* function_ptr;
} plugin_entry_t;

typedef struct {
    plugin_entry_t plugins[MAX_PLUGINS];
    int plugin_count;
    
    char plugin_dir[256];
    int timeout_seconds;
    
    pthread_mutex_t lock;
    int initialized;
} plugin_system_t;

static plugin_system_t g_plugin_system = {0};

int sm_plugin_init(const char* plugin_directory)
{
    if (g_plugin_system.initialized) {
        sm_log(SM_LOG_WARN, "plugin: already initialized");
        return 0;
    }
    
    if (!plugin_directory) {
        sm_log(SM_LOG_ERROR, "plugin: directory is NULL");
        return -1;
    }
    
    memset(&g_plugin_system, 0, sizeof(g_plugin_system));
    
    if (pthread_mutex_init(&g_plugin_system.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "plugin: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }
    
    /* Verify directory exists */
    struct stat st;
    if (stat(plugin_directory, &st) != 0 || !S_ISDIR(st.st_mode)) {
        sm_log(SM_LOG_WARN, "plugin: directory not found or not accessible: %s", plugin_directory);
    }
    
    strncpy(g_plugin_system.plugin_dir, plugin_directory, sizeof(g_plugin_system.plugin_dir) - 1);
    g_plugin_system.timeout_seconds = DEFAULT_PLUGIN_TIMEOUT;
    g_plugin_system.initialized = 1;
    
    sm_log(SM_LOG_INFO, "plugin: initialized (dir=%s)", plugin_directory);
    return 0;
}

int sm_plugin_register_script(const char* name, const char* script_path, sm_plugin_type_t plugin_type)
{
    if (!g_plugin_system.initialized) {
        sm_log(SM_LOG_ERROR, "plugin: not initialized");
        return -1;
    }
    
    if (!name || !script_path) {
        sm_log(SM_LOG_ERROR, "plugin: invalid parameters");
        return -1;
    }
    
    /* Verify script exists and is executable */
    if (access(script_path, X_OK) != 0) {
        sm_log(SM_LOG_ERROR, "plugin: script not executable: %s (%s)",
               script_path, strerror(errno));
        return -1;
    }
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    if (g_plugin_system.plugin_count >= MAX_PLUGINS) {
        pthread_mutex_unlock(&g_plugin_system.lock);
        sm_log(SM_LOG_ERROR, "plugin: max plugins reached");
        return -1;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (strcmp(g_plugin_system.plugins[i].name, name) == 0) {
            pthread_mutex_unlock(&g_plugin_system.lock);
            sm_log(SM_LOG_WARN, "plugin: duplicate registration '%s'", name);
            return -1;
        }
    }
    
    plugin_entry_t* entry = &g_plugin_system.plugins[g_plugin_system.plugin_count++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    strncpy(entry->path, script_path, sizeof(entry->path) - 1);
    entry->type = plugin_type;
    entry->is_script = 1;
    entry->enabled = 1;
    
    pthread_mutex_unlock(&g_plugin_system.lock);
    
    sm_log(SM_LOG_INFO, "plugin: registered script '%s' (type=%d, path=%s)",
           name, plugin_type, script_path);
    return 0;
}

int sm_plugin_register_library(const char* name, const char* library_path,
                               const char* entry_point_function)
{
    if (!g_plugin_system.initialized) {
        sm_log(SM_LOG_ERROR, "plugin: not initialized");
        return -1;
    }
    
    if (!name || !library_path || !entry_point_function) {
        sm_log(SM_LOG_ERROR, "plugin: invalid parameters");
        return -1;
    }
    
    /* Try to load library */
    void* handle = dlopen(library_path, RTLD_LAZY);
    if (!handle) {
        sm_log(SM_LOG_ERROR, "plugin: dlopen failed: %s", dlerror());
        return -1;
    }
    
    /* Look up entry point function */
    void* func = dlsym(handle, entry_point_function);
    if (!func) {
        sm_log(SM_LOG_ERROR, "plugin: symbol not found: %s", entry_point_function);
        dlclose(handle);
        return -1;
    }
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    if (g_plugin_system.plugin_count >= MAX_PLUGINS) {
        pthread_mutex_unlock(&g_plugin_system.lock);
        dlclose(handle);
        sm_log(SM_LOG_ERROR, "plugin: max plugins reached");
        return -1;
    }
    
    plugin_entry_t* entry = &g_plugin_system.plugins[g_plugin_system.plugin_count++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    strncpy(entry->path, library_path, sizeof(entry->path) - 1);
    entry->is_script = 0;
    entry->enabled = 1;
    entry->library_handle = handle;
    entry->function_ptr = func;
    
    pthread_mutex_unlock(&g_plugin_system.lock);
    
    sm_log(SM_LOG_INFO, "plugin: registered library '%s' (entry=%s, path=%s)",
           name, entry_point_function, library_path);
    return 0;
}

int sm_plugin_execute(const char* plugin_name, const char* argument,
                      char* output_buffer, int output_size)
{
    /* Mark unused parameters for future implementation */
    (void)output_buffer;
    (void)output_size;

    if (!g_plugin_system.initialized || !plugin_name) {
        return -1;
    }

    /* timeout value lock ke andar copy karo */
    pthread_mutex_lock(&g_plugin_system.lock);
    int timeout_seconds = g_plugin_system.timeout_seconds;

    plugin_entry_t* entry = NULL;
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (strcmp(g_plugin_system.plugins[i].name, plugin_name) == 0) {
            entry = &g_plugin_system.plugins[i];
            break;
        }
    }
    
    if (!entry || !entry->enabled) {
        pthread_mutex_unlock(&g_plugin_system.lock);
        sm_log(SM_LOG_WARN, "plugin: '%s' not found or disabled", plugin_name);
        return -1;
    }

    /* entry ka path copy karo — lock ke bahar use karenge */
    char path_copy[256];
    strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
    path_copy[255] = '\0';

    pthread_mutex_unlock(&g_plugin_system.lock);
    
    int exit_code = -1;
    
    if (entry->is_script) {
        pid_t pid = fork();
        if (pid < 0) {
            sm_log(SM_LOG_ERROR, "plugin: fork failed: %s", strerror(errno));
            return -1;
        }
        
        if (pid == 0) {
            /* Child process */
            execl(path_copy, path_copy, argument, NULL);
            exit(127);
        }

        /* FIX: Parent — timeout ke saath wait karo
         * Pehle WNOHANG loop chalao deadline tak
         * Deadline guzri? SIGKILL bhejo child ko */
        time_t deadline = time(NULL) + timeout_seconds;
        int status = 0;
        int waited = 0;

        while (time(NULL) < deadline) {
            pid_t result = waitpid(pid, &status, WNOHANG);

            if (result == pid) {
                /* Child khatam ho gaya */
                waited = 1;
                break;
            } else if (result < 0) {
                sm_log(SM_LOG_ERROR, "plugin: waitpid failed: %s", strerror(errno));
                break;
            }

            /* Child abhi chal raha hai — 100ms ruko phir dobara check */
            struct timespec ts = {0, 100000000L};
            nanosleep(&ts, NULL);
        }

        if (!waited) {
            /* Timeout! Child ko force kill karo */
            sm_log(SM_LOG_WARN, "plugin: '%s' timed out (%d sec), killing child pid=%d",
                   plugin_name, timeout_seconds, (int)pid);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0); /* zombie saaf karo */
            exit_code = -1;
        } else if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
        } else {
            exit_code = -1;
        }

    } else {
        /* Library plugin */
        sm_log(SM_LOG_DEBUG, "plugin: library execution stub for '%s'", plugin_name);
        exit_code = 0;
    }
    
    /* Stats update karo */
    pthread_mutex_lock(&g_plugin_system.lock);
    /* entry pointer dobara dhundho — lock ke bahar stale ho sakta tha */
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (strcmp(g_plugin_system.plugins[i].name, plugin_name) == 0) {
            g_plugin_system.plugins[i].execution_count++;
            if (exit_code != 0) {
                g_plugin_system.plugins[i].failure_count++;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_plugin_system.lock);
    
    sm_log(SM_LOG_DEBUG, "plugin: '%s' executed (exit_code=%d)", plugin_name, exit_code);
    return exit_code;
}

int sm_plugin_execute_async(const char* plugin_name, const char* argument,
                           sm_plugin_callback_t callback, void* userdata)
{
    (void)callback;
    (void)userdata;
    
    if (!g_plugin_system.initialized || !plugin_name) {
        return -1;
    }
    
    /* For now, execute synchronously (in production, would use thread pool) */
    char output_buf[256];
    int ret = sm_plugin_execute(plugin_name, argument, output_buf, sizeof(output_buf));
    
    if (callback) {
        callback(plugin_name, ret, userdata);
    }
    
    return 0;
}

int sm_plugin_set_execution_timeout(int timeout_seconds)
{
    if (timeout_seconds <= 0 || timeout_seconds > 300) {
        sm_log(SM_LOG_ERROR, "plugin: invalid timeout %d", timeout_seconds);
        return -1;
    }
    
    pthread_mutex_lock(&g_plugin_system.lock);
    g_plugin_system.timeout_seconds = timeout_seconds;
    pthread_mutex_unlock(&g_plugin_system.lock);
    
    sm_log(SM_LOG_INFO, "plugin: timeout set to %d seconds", timeout_seconds);
    return 0;
}

int sm_plugin_enable(const char* plugin_name)
{
    if (!plugin_name) return -1;
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (strcmp(g_plugin_system.plugins[i].name, plugin_name) == 0) {
            g_plugin_system.plugins[i].enabled = 1;
            pthread_mutex_unlock(&g_plugin_system.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_plugin_system.lock);
    return -1;
}

int sm_plugin_disable(const char* plugin_name)
{
    if (!plugin_name) return -1;
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (strcmp(g_plugin_system.plugins[i].name, plugin_name) == 0) {
            g_plugin_system.plugins[i].enabled = 0;
            pthread_mutex_unlock(&g_plugin_system.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_plugin_system.lock);
    return -1;
}

sm_plugin_info_t sm_plugin_get_info(const char* plugin_name)
{
    sm_plugin_info_t info = {0};
    
    if (!plugin_name) return info;
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (strcmp(g_plugin_system.plugins[i].name, plugin_name) == 0) {
            strncpy(info.name, g_plugin_system.plugins[i].name, sizeof(info.name) - 1);
            strncpy(info.path, g_plugin_system.plugins[i].path, sizeof(info.path) - 1);
            info.type = g_plugin_system.plugins[i].type;
            info.is_script = g_plugin_system.plugins[i].is_script;
            info.enabled = g_plugin_system.plugins[i].enabled;
            info.execution_count = g_plugin_system.plugins[i].execution_count;
            info.failure_count = g_plugin_system.plugins[i].failure_count;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_plugin_system.lock);
    return info;
}

int sm_plugin_list(char* buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) return -1;
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    int offset = 0;
    int count = 0;
    
    for (int i = 0; i < g_plugin_system.plugin_count && offset < buffer_size - 1; i++) {
        int len = snprintf(buffer + offset, buffer_size - offset, "%s%s",
                          count > 0 ? "," : "", g_plugin_system.plugins[i].name);
        if (len < 0) break;
        offset += len;
        count++;
    }
    
    pthread_mutex_unlock(&g_plugin_system.lock);
    
    return count;
}

int sm_plugin_cleanup(void)
{
    if (!g_plugin_system.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_plugin_system.lock);
    
    /* Unload all libraries */
    for (int i = 0; i < g_plugin_system.plugin_count; i++) {
        if (!g_plugin_system.plugins[i].is_script && g_plugin_system.plugins[i].library_handle) {
            dlclose(g_plugin_system.plugins[i].library_handle);
        }
    }
    
    memset(&g_plugin_system, 0, sizeof(g_plugin_system));
    pthread_mutex_unlock(&g_plugin_system.lock);
    pthread_mutex_destroy(&g_plugin_system.lock);
    
    sm_log(SM_LOG_INFO, "plugin: cleanup complete");
    return 0;
}