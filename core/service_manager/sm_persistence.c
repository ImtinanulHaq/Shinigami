#define _POSIX_C_SOURCE 200809L

/*
 * sm_persistence.c - Registry persistence
 */

#include "sm_persistence.h"
#include "sm_logging.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define DEFAULT_PERSISTENCE_FILE "/var/lib/servicemanager/registry.dat"
#define SM_REGISTRY_MAX 32  /* Must match sm_registry.c */

static pthread_t autosave_thread = 0;
static int autosave_running = 0;
static pthread_mutex_t autosave_mutex = PTHREAD_MUTEX_INITIALIZER;
static int autosave_interval = 60;  /* Default: save every 60 seconds */

/*
 * Background thread for periodic autosave
 */
static void* autosave_worker(void* arg)
{
    (void)arg;
    
    while (1) {
        /* Sleep for the configured interval */
        sleep((unsigned int)autosave_interval);
        
        /* Check if we should still be running */
        pthread_mutex_lock(&autosave_mutex);
        if (!autosave_running) {
            pthread_mutex_unlock(&autosave_mutex);
            break;
        }
        pthread_mutex_unlock(&autosave_mutex);
        
        /* Save the registry */
        if (sm_persistence_save(NULL) < 0) {
            sm_log(SM_LOG_WARN, "persistence: autosave failed");
        }
    }
    
    return NULL;
}

int sm_persistence_save(const char* filename)
{
    FILE* f;
    service_entry_t* services = NULL;
    int count = 0;
    
    if (!filename) filename = DEFAULT_PERSISTENCE_FILE;
    
    f = fopen(filename, "wb");
    if (!f) {
        sm_log(SM_LOG_WARN, "persistence: cannot open %s for writing", filename);
        return -1;
    }
    
    if (sm_registry_get_all(&services, &count) < 0) {
        fclose(f);
        return -1;
    }
    
    /* Write header */
    uint32_t magic = 0x534D5052;  /* SMPR */
    uint32_t version = 1;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&count, 4, 1, f);
    
    /* Write each service entry */
    for (int i = 0; i < count; i++) {
        fwrite(&services[i], sizeof(service_entry_t), 1, f);
    }
    
    fclose(f);
    sm_registry_free_copy(services);
    
    sm_log(SM_LOG_INFO, "persistence: saved %d services to %s", count, filename);
    return 0;
}

int sm_persistence_load(const char* filename)
{
    FILE* f;
    uint32_t magic, version, count;
    service_entry_t entry;
    
    if (!filename) filename = DEFAULT_PERSISTENCE_FILE;
    
    f = fopen(filename, "rb");
    if (!f) {
        sm_log(SM_LOG_INFO, "persistence: no saved state at %s", filename);
        return 0;  /* not an error, just no prior state */
    }
    
    if (fread(&magic, 4, 1, f) != 1 || magic != 0x534D5052) {
        sm_log(SM_LOG_WARN, "persistence: invalid magic");
        fclose(f);
        return -1;
    }
    
    if (fread(&version, 4, 1, f) != 1 || version != 1) {
        sm_log(SM_LOG_WARN, "persistence: unsupported version");
        fclose(f);
        return -1;
    }
    
    if (fread(&count, 4, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    
    /* Bounds check: prevent OOM and infinite loops from malformed files */
    /* Cast to int for comparison to handle the check properly */
    if ((int)count < 0 || (int)count > SM_REGISTRY_MAX) {
        sm_log(SM_LOG_ERROR, "persistence: invalid count %u (max %d)", count, SM_REGISTRY_MAX);
        fclose(f);
        return -1;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        if (fread(&entry, sizeof(service_entry_t), 1, f) != 1) {
            break;
        }
        /* Mark all loaded services as crashed - they need to re-register */
        entry.status = SERVICE_CRASHED;
        entry.restart_count = 0;
        sm_registry_add(&entry);
    }
    
    fclose(f);
    
    sm_log(SM_LOG_INFO, "persistence: loaded %d services from %s", count, filename);
    return 0;
}

const char* sm_persistence_get_default_path(void)
{
    return DEFAULT_PERSISTENCE_FILE;
}

void sm_persistence_enable_autosave(int interval_sec)
{
    pthread_mutex_lock(&autosave_mutex);
    
    if (autosave_running) {
        pthread_mutex_unlock(&autosave_mutex);
        sm_log(SM_LOG_INFO, "persistence: autosave already running");
        return;
    }
    
    if (interval_sec <= 0) interval_sec = 60;
    autosave_interval = interval_sec;
    autosave_running = 1;
    
    pthread_mutex_unlock(&autosave_mutex);
    
    /* Create the background autosave thread */
    if (pthread_create(&autosave_thread, NULL, autosave_worker, NULL) < 0) {
        sm_log(SM_LOG_ERROR, "persistence: failed to create autosave thread");
        pthread_mutex_lock(&autosave_mutex);
        autosave_running = 0;
        pthread_mutex_unlock(&autosave_mutex);
        return;
    }
    
    sm_log(SM_LOG_INFO, "persistence: autosave enabled (interval=%ds)", interval_sec);
}

void sm_persistence_cleanup(void)
{
    /* Signal autosave thread to stop */
    pthread_mutex_lock(&autosave_mutex);
    autosave_running = 0;
    pthread_mutex_unlock(&autosave_mutex);
    
    /* Wait for the thread to finish (with timeout) */
    if (autosave_thread > 0) {
        void* result;
        pthread_join(autosave_thread, &result);
        autosave_thread = 0;
    }
}
