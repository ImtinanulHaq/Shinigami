#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>

#define RING_BUFFER_MAX_CAPACITY  1024
#define RING_BUFFER_MAX_ITEM_SIZE 4096

#define RB_SUCCESS       0
#define RB_ERROR_FULL   -1
#define RB_ERROR_EMPTY  -2
#define RB_ERROR_INIT   -3

#define CACHE_LINE_SIZE  64
#define CACHE_PAD        (CACHE_LINE_SIZE - sizeof(_Atomic uint32_t))

// lives in SHARED MEMORY — both processes see this
typedef struct {
    _Atomic uint32_t head;
    uint8_t          _pad_head[CACHE_PAD];  // prevent false sharing
    _Atomic uint32_t tail;
    uint8_t          _pad_tail[CACHE_PAD];
    uint32_t         capacity;
    uint32_t         item_size;
    uint8_t          data[];                // actual message slots
} ring_buffer_t;

// lives in THIS PROCESS only (heap) — not shared
// notify_fd is process-local — file descriptors cannot be shared between processes
typedef struct {
    ring_buffer_t* rb;          // pointer into shared memory
    int            notify_fd;   // this process's own eventfd for wakeup
    size_t         total_size;  // needed for munmap
} rb_handle_t;

rb_handle_t* ring_buffer_create(const char* name, uint32_t capacity, uint32_t item_size);
rb_handle_t* ring_buffer_attach(const char* name);
int          ring_buffer_write(rb_handle_t* h, const void* item);
int          ring_buffer_read(rb_handle_t* h, void* item);
int          ring_buffer_is_full(const rb_handle_t* h);
int          ring_buffer_is_empty(const rb_handle_t* h);
uint32_t     ring_buffer_count(const rb_handle_t* h);
void         ring_buffer_detach(rb_handle_t* h);
void         ring_buffer_destroy(rb_handle_t* h, const char* name);

#endif