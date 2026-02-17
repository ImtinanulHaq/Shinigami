#define _POSIX_C_SOURCE 200809L

#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <errno.h>

static size_t _total_size(uint32_t capacity, uint32_t item_size)
{
    return sizeof(ring_buffer_t) + ((size_t)capacity * item_size);
}
//create a newbuffer in shared memory
ring_buffer_t* ring_buffer_create(const char* name,
                                   uint32_t    capacity,
                                   uint32_t    item_size)
{
    if (name == NULL) {
        fprintf(stderr, "[RingBuffer] ERROR: name NULL nahi ho sakta\n");
        return NULL;
    }
    if (capacity == 0 || capacity > RING_BUFFER_MAX_CAPACITY) {
        fprintf(stderr, "[RingBuffer] ERROR: capacity 1 aur %d ke beech honi chahiye\n",
                RING_BUFFER_MAX_CAPACITY);
        return NULL;
    }
    if (item_size == 0 || item_size > RING_BUFFER_MAX_ITEM_SIZE) {
        fprintf(stderr, "[RingBuffer] ERROR: item_size 1 aur %d ke beech hona chahiye\n",
                RING_BUFFER_MAX_ITEM_SIZE);
        return NULL;
    }

    size_t total = _total_size(capacity, item_size);

    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "[RingBuffer] Pehle se exist karta hai, hata ke dobara bana raha hoon: %s\n", name);
            shm_unlink(name);
            fd = shm_open(name, O_CREAT | O_RDWR, 0600);
        }
        if (fd == -1) {
            perror("[RingBuffer] shm_open() fail");
            return NULL;
        }
    }

    if (ftruncate(fd, (off_t)total) == -1) {
        perror("[RingBuffer] ftruncate() fail");
        close(fd);
        shm_unlink(name);
        return NULL;
    }

    ring_buffer_t* rb = mmap(NULL, total,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED,
                              fd, 0);
    if (rb == MAP_FAILED) {
        perror("[RingBuffer] mmap() fail");
        close(fd);
        shm_unlink(name);
        return NULL;
    }

    close(fd);

    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
    rb->capacity  = capacity;
    rb->item_size = item_size;

    rb->notify_fd = eventfd(0, EFD_NONBLOCK);
    if (rb->notify_fd == -1) {
        perror("[RingBuffer] eventfd() fail");
        munmap(rb, total);
        shm_unlink(name);
        return NULL;
    }

    printf("[RingBuffer] Bana diya: %s | capacity=%u | item_size=%u bytes | total=%.1f KB\n",
           name, capacity, item_size, (double)total / 1024.0);

    return rb;
}

ring_buffer_t* ring_buffer_attach(const char* name)
{
    int fd = shm_open(name, O_RDWR, 0);
    if (fd == -1) {
        fprintf(stderr, "[RingBuffer] '%s' nahi mila — kya Service Manager chalu hai?\n", name);
        return NULL;
    }

    ring_buffer_t* temp = mmap(NULL, sizeof(ring_buffer_t),
                                PROT_READ, MAP_SHARED, fd, 0);
    if (temp == MAP_FAILED) {
        perror("[RingBuffer] pehla mmap() fail");
        close(fd);
        return NULL;
    }

    uint32_t capacity  = temp->capacity;
    uint32_t item_size = temp->item_size;

    munmap(temp, sizeof(ring_buffer_t));

    size_t total = _total_size(capacity, item_size);

    ring_buffer_t* rb = mmap(NULL, total,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED,
                              fd, 0);
    close(fd);

    if (rb == MAP_FAILED) {
        perror("[RingBuffer] doosra mmap() fail");
        return NULL;
    }

    printf("[RingBuffer] Connect ho gaya: %s | capacity=%u | item_size=%u\n",
           name, capacity, item_size);

    return rb;
}

int ring_buffer_write(ring_buffer_t* rb, const void* item)
{
    if (rb == NULL || item == NULL) return RB_ERROR_INIT;

    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    uint32_t next_head = (head + 1) % rb->capacity;

    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    if (next_head == tail) {
        return RB_ERROR_FULL;
    }

    void* dest = rb->data + ((size_t)head * rb->item_size);
    memcpy(dest, item, rb->item_size);

    atomic_store_explicit(&rb->head, next_head, memory_order_release);

    uint64_t one = 1;
    write(rb->notify_fd, &one, sizeof(one));

    return RB_SUCCESS;
}

int ring_buffer_read(ring_buffer_t* rb, void* item)
{
    if (rb == NULL || item == NULL) return RB_ERROR_INIT;

    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    if (tail == head) {
        return RB_ERROR_EMPTY;
    }

    const void* src = rb->data + ((size_t)tail * rb->item_size);
    memcpy(item, src, rb->item_size);

    uint32_t next_tail = (tail + 1) % rb->capacity;
    atomic_store_explicit(&rb->tail, next_tail, memory_order_release);

    return RB_SUCCESS;
}

int ring_buffer_is_full(const ring_buffer_t* rb)
{
    if (rb == NULL) return 1;

    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    return ((head + 1) % rb->capacity) == tail;
}

int ring_buffer_is_empty(const ring_buffer_t* rb)
{
    if (rb == NULL) return 1;

    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    return head == tail;
}

uint32_t ring_buffer_count(const ring_buffer_t* rb)
{
    if (rb == NULL) return 0;

    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if (head >= tail) {
        return head - tail;
    } else {
        return rb->capacity - tail + head;
    }
}

void ring_buffer_detach(ring_buffer_t* rb)
{
    if (rb == NULL) return;

    size_t total = _total_size(rb->capacity, rb->item_size);

    munmap(rb, total);

    printf("[RingBuffer] Detach ho gaya\n");
}

void ring_buffer_destroy(ring_buffer_t* rb, const char* name)
{
    if (rb == NULL) return;

    size_t total = _total_size(rb->capacity, rb->item_size);

    if (rb->notify_fd != -1) {
        close(rb->notify_fd);
    }

    munmap(rb, total);

    if (name != NULL) {
        shm_unlink(name);
        printf("[RingBuffer] Delete ho gaya: %s\n", name);
    }
}
