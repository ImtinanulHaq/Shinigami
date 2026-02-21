#ifndef SM_PERSISTENCE_H
#define SM_PERSISTENCE_H

/*
 * sm_persistence.h - Service registry state persistence
 *
 * Save/restore registry across daemon restarts.
 * Services that were registered before restart can be detected.
 */

#include "sm_registry.h"

/* Save registry to disk */
int sm_persistence_save(const char* filename);

/* Load registry from disk */
int sm_persistence_load(const char* filename);

/* Get default persistence file path */
const char* sm_persistence_get_default_path(void);

/* Auto-save on interval */
void sm_persistence_enable_autosave(int interval_sec);

/* Cleanup */
void sm_persistence_cleanup(void);

#endif /* SM_PERSISTENCE_H */
