#include "hal_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Global device registry (simple linked list)
typedef struct device_node {
    hw_device_t* device;
    struct device_node* next;
} device_node_t;

static device_node_t* g_device_list = NULL;
static pthread_mutex_t g_registry_lock = PTHREAD_MUTEX_INITIALIZER;

// ══════════════════════════════════════════════════════════════════════════════
// COMMON DEVICE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════════

int hal_device_init(hw_device_t* dev, const char* name, hal_device_type_t type)
{
    if (!dev || !name) return HAL_ERROR_INVALID;
    
    memset(dev, 0, sizeof(hw_device_t));
    
    strncpy(dev->name, name, HAL_MAX_NAME_LEN - 1);
    dev->name[HAL_MAX_NAME_LEN - 1] = '\0';
    
    dev->type = type;
    dev->version = (HAL_VERSION_MAJOR << 16) | (HAL_VERSION_MINOR << 8) | HAL_VERSION_PATCH;
    dev->state = HAL_STATE_CLOSED;
    dev->fd = -1;
    dev->ops = NULL;
    dev->priv = NULL;
    dev->ref_count = 1;  // Start with reference count of 1
    
    if (pthread_mutex_init(&dev->lock, NULL) != 0) {
        return HAL_ERROR_GENERIC;
    }
    
    return HAL_SUCCESS;
}

void hal_device_destroy(hw_device_t* dev)
{
    if (!dev) return;
    
    // Close device if still open
    if (dev->state != HAL_STATE_CLOSED && dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }
    
    // Free private data if exists
    if (dev->priv) {
        free(dev->priv);
        dev->priv = NULL;
    }
    
    pthread_mutex_destroy(&dev->lock);
}

void hal_device_ref(hw_device_t* dev)
{
    if (!dev) return;
    
    pthread_mutex_lock(&dev->lock);
    dev->ref_count++;
    pthread_mutex_unlock(&dev->lock);
}

void hal_device_unref(hw_device_t* dev)
{
    if (!dev) return;
    
    int should_destroy = 0;
    
    pthread_mutex_lock(&dev->lock);
    dev->ref_count--;
    if (dev->ref_count <= 0) {
        should_destroy = 1;
    }
    pthread_mutex_unlock(&dev->lock);
    
    if (should_destroy) {
        hal_device_destroy(dev);
        free(dev);
    }
}

void hal_device_lock(hw_device_t* dev)
{
    if (dev) {
        pthread_mutex_lock(&dev->lock);
    }
}

void hal_device_unlock(hw_device_t* dev)
{
    if (dev) {
        pthread_mutex_unlock(&dev->lock);
    }
}

const char* hal_error_string(hal_error_t error)
{
    switch (error) {
        case HAL_SUCCESS:          return "Success";
        case HAL_ERROR_GENERIC:    return "Generic error";
        case HAL_ERROR_NO_DEVICE:  return "Device not found";
        case HAL_ERROR_BUSY:       return "Device busy";
        case HAL_ERROR_IO:         return "I/O error";
        case HAL_ERROR_INVALID:    return "Invalid parameter";
        case HAL_ERROR_NO_MEMORY:  return "Out of memory";
        case HAL_ERROR_TIMEOUT:    return "Operation timeout";
        case HAL_ERROR_NOT_SUPPORT:return "Not supported";
        case HAL_ERROR_PERMISSION: return "Permission denied";
        default:                   return "Unknown error";
    }
}

void hal_get_version(int* major, int* minor, int* patch)
{
    if (major) *major = HAL_VERSION_MAJOR;
    if (minor) *minor = HAL_VERSION_MINOR;
    if (patch) *patch = HAL_VERSION_PATCH;
}

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

int hal_device_register(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    
    device_node_t* node = malloc(sizeof(device_node_t));
    if (!node) return HAL_ERROR_NO_MEMORY;
    
    node->device = dev;
    
    pthread_mutex_lock(&g_registry_lock);
    
    // Check for duplicate names
    device_node_t* current = g_device_list;
    while (current) {
        if (strcmp(current->device->name, dev->name) == 0) {
            pthread_mutex_unlock(&g_registry_lock);
            free(node);
            return HAL_ERROR_BUSY;  // Name already exists
        }
        current = current->next;
    }
    
    // Add to front of list
    node->next = g_device_list;
    g_device_list = node;
    
    hal_device_ref(dev);  // Increment reference count
    
    pthread_mutex_unlock(&g_registry_lock);
    
    return HAL_SUCCESS;
}

void hal_device_unregister(hw_device_t* dev)
{
    if (!dev) return;
    
    pthread_mutex_lock(&g_registry_lock);
    
    device_node_t** current = &g_device_list;
    while (*current) {
        if ((*current)->device == dev) {
            device_node_t* to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            hal_device_unref(dev);  // Decrement reference count
            break;
        }
        current = &(*current)->next;
    }
    
    pthread_mutex_unlock(&g_registry_lock);
}

hw_device_t* hal_device_find(const char* name)
{
    if (!name) return NULL;
    
    hw_device_t* found = NULL;
    
    pthread_mutex_lock(&g_registry_lock);
    
    device_node_t* current = g_device_list;
    while (current) {
        if (strcmp(current->device->name, name) == 0) {
            found = current->device;
            hal_device_ref(found);  // Increment reference count
            break;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&g_registry_lock);
    
    return found;
}

int hal_device_list(hw_device_t** devices, int max_count)
{
    if (!devices || max_count <= 0) return 0;
    
    int count = 0;
    
    pthread_mutex_lock(&g_registry_lock);
    
    device_node_t* current = g_device_list;
    while (current && count < max_count) {
        devices[count] = current->device;
        hal_device_ref(devices[count]);
        count++;
        current = current->next;
    }
    
    pthread_mutex_unlock(&g_registry_lock);
    
    return count;
}