#ifndef SANDBOX_MOUNT_H
#define SANDBOX_MOUNT_H

#include "sandbox.h"

int sandbox_mount_setup_filesystem(const char* root);
int sandbox_mount_add_binding(const filesystem_binding_t* binding);
int sandbox_mount_remove_binding(const char* container_path);

#endif
