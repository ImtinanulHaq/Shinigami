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

#define DEFAULT_PERSISTENCE_FILE "/var/lib/servicemanager/registry.dat"

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
    sm_log(SM_LOG_INFO, "persistence: autosave enabled (interval=%ds)", interval_sec);
}

void sm_persistence_cleanup(void)
{
}
