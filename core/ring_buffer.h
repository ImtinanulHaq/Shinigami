#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>

#define RING_BUFFER_MAX_CAPACITY  1024
#define RING_BUFFER_MAX_ITEM_SIZE 4096
#define RING_BUFFER_SHM_NAME      "/ring_main"

#define RB_SUCCESS        0
#define RB_ERROR_FULL    -1
#define RB_ERROR_EMPTY   -2
#define RB_ERROR_INIT    -3
#define RB_ERROR_ATTACH  -4

// Cache line padding to avoid false sharing between head and tail
#define CACHE_LINE_SIZE   64
#define PADDING_SIZE      (CACHE_LINE_SIZE - sizeof(_Atomic uint32_t))

typedef struct {
    _Atomic uint32_t head;      // Writer pointer
    uint8_t _pad_head[PADDING_SIZE];

    _Atomic uint32_t tail;      // Reader pointer
    uint8_t _pad_tail[PADDING_SIZE];

    uint32_t capacity;          // Max items
    uint32_t item_size;         // Size per item
    int notify_fd;              // Notification fd for reader
    uint8_t data[];             // Flexible array for data

} ring_buffer_t;

// Create new ring buffer in shared memory
ring_buffer_t* ring_buffer_create(const char* name,
                                   uint32_t    capacity,
                                   uint32_t    item_size);

// Attach to existing ring buffer
ring_buffer_t* ring_buffer_attach(const char* name);

// Write item to buffer
int ring_buffer_write(ring_buffer_t* rb, const void* item);

// Read item from buffer
int ring_buffer_read(ring_buffer_t* rb, void* item);

// Destroy buffer and free shared memory
void ring_buffer_destroy(ring_buffer_t* rb, const char* name);

// Detach from buffer without deleting it
void ring_buffer_detach(ring_buffer_t* rb);

// Check if buffer is full
int ring_buffer_is_full(const ring_buffer_t* rb);

// Check if buffer is empty
int ring_buffer_is_empty(const ring_buffer_t* rb);

// Get current item count
uint32_t ring_buffer_count(const ring_buffer_t* rb);

#endif
