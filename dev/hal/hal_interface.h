#ifndef HAL_INTERFACE_H
#define HAL_INTERFACE_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <pthread.h>

// ══════════════════════════════════════════════════════════════════════════════
// HAL (Hardware Abstraction Layer) - Common Interface
// 
// Purpose: Provides unified interface for all hardware devices
// Benefits:
// - Services don't need to know hardware specifics (ALSA vs OSS, V4L2 vs V4L1)
// - Easy to swap hardware implementations
// - Consistent error handling and lifecycle management
// ══════════════════════════════════════════════════════════════════════════════

// HAL version for compatibility checking
#define HAL_VERSION_MAJOR  1
#define HAL_VERSION_MINOR  0
#define HAL_VERSION_PATCH  0

// Maximum device name length
#define HAL_MAX_NAME_LEN   64

// Common error codes (negative values)
typedef enum {
    HAL_SUCCESS           =  0,   // Operation successful
    HAL_ERROR_GENERIC     = -1,   // Generic error
    HAL_ERROR_NO_DEVICE   = -2,   // Device not found
    HAL_ERROR_BUSY        = -3,   // Device busy
    HAL_ERROR_IO          = -4,   // I/O error
    HAL_ERROR_INVALID     = -5,   // Invalid parameter
    HAL_ERROR_NO_MEMORY   = -6,   // Out of memory
    HAL_ERROR_TIMEOUT     = -7,   // Operation timeout
    HAL_ERROR_NOT_SUPPORT = -8,   // Operation not supported
    HAL_ERROR_PERMISSION  = -9,   // Permission denied
} hal_error_t;

// Device types
typedef enum {
    HAL_DEVICE_TYPE_AUDIO   = 0x01,
    HAL_DEVICE_TYPE_SENSOR  = 0x02,
    HAL_DEVICE_TYPE_CAMERA  = 0x03,
    HAL_DEVICE_TYPE_GPIO    = 0x04,
    HAL_DEVICE_TYPE_DISPLAY = 0x05,
} hal_device_type_t;

// Device state
typedef enum {
    HAL_STATE_CLOSED   = 0,  // Device closed
    HAL_STATE_OPEN     = 1,  // Device open but not active
    HAL_STATE_ACTIVE   = 2,  // Device active (streaming, recording, etc)
    HAL_STATE_ERROR    = 3,  // Device in error state
} hal_device_state_t;

// Forward declaration
struct hw_device;

// Common device operations (function pointers)
typedef struct {
    // Open device
    // Returns: 0 on success, negative error code on failure
    int (*open)(struct hw_device* dev);
    
    // Close device
    // Returns: 0 on success, negative error code on failure
    int (*close)(struct hw_device* dev);
    
    // Start device operation (streaming, recording, etc)
    // Returns: 0 on success, negative error code on failure
    int (*start)(struct hw_device* dev);
    
    // Stop device operation
    // Returns: 0 on success, negative error code on failure
    int (*stop)(struct hw_device* dev);
    
    // Read data from device
    // buf: buffer to read into
    // size: buffer size
    // Returns: number of bytes read, or negative error code
    ssize_t (*read)(struct hw_device* dev, void* buf, size_t size);
    
    // Write data to device
    // buf: buffer to write from
    // size: data size
    // Returns: number of bytes written, or negative error code
    ssize_t (*write)(struct hw_device* dev, const void* buf, size_t size);
    
    // Device-specific control (ioctl-like)
    // cmd: command code
    // arg: command argument
    // Returns: 0 on success, negative error code on failure
    int (*control)(struct hw_device* dev, uint32_t cmd, void* arg);
    
    // Get device status/info
    // Returns: 0 on success, negative error code on failure
    int (*get_info)(struct hw_device* dev, void* info);
    
} hw_device_ops_t;

// Common hardware device structure
typedef struct hw_device {
    // Device metadata
    char                name[HAL_MAX_NAME_LEN];  // Device name
    hal_device_type_t   type;                    // Device type
    uint32_t            version;                 // HAL version
    hal_device_state_t  state;                   // Current state
    
    // Device file descriptor (if applicable)
    int                 fd;                      // -1 if not used
    
    // Thread safety
    pthread_mutex_t     lock;                    // Mutex for thread safety
    
    // Common operations
    const hw_device_ops_t* ops;                  // Function pointers
    
    // Private device-specific data
    void*               priv;                    // Implementation-specific data
    
    // Reference counting for proper cleanup
    int                 ref_count;               // Reference count
    
} hw_device_t;

// ══════════════════════════════════════════════════════════════════════════════
// COMMON DEVICE MANAGEMENT FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Initialize a hardware device structure
// dev: device structure to initialize
// name: device name
// type: device type
// Returns: 0 on success, negative error code on failure
int hal_device_init(hw_device_t* dev, const char* name, hal_device_type_t type);

// Destroy a hardware device structure (cleanup)
// dev: device to destroy
void hal_device_destroy(hw_device_t* dev);

// Increment reference count (thread-safe)
void hal_device_ref(hw_device_t* dev);

// Decrement reference count, destroy if reaches 0 (thread-safe)
void hal_device_unref(hw_device_t* dev);

// Lock device for exclusive access
void hal_device_lock(hw_device_t* dev);

// Unlock device
void hal_device_unlock(hw_device_t* dev);

// Convert error code to string
const char* hal_error_string(hal_error_t error);

// Get HAL version
void hal_get_version(int* major, int* minor, int* patch);

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY (optional - for managing multiple devices)
// ══════════════════════════════════════════════════════════════════════════════

// Register a device in the global registry
// dev: device to register
// Returns: 0 on success, negative error code on failure
int hal_device_register(hw_device_t* dev);

// Unregister a device from the global registry
// dev: device to unregister
void hal_device_unregister(hw_device_t* dev);

// Find a device by name
// name: device name
// Returns: device pointer or NULL if not found
hw_device_t* hal_device_find(const char* name);

// List all registered devices
// devices: array to fill with device pointers
// max_count: maximum number of devices to return
// Returns: number of devices found
int hal_device_list(hw_device_t** devices, int max_count);

#endif // HAL_INTERFACE_H