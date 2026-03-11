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

static size_t total_size(uint32_t capacity, uint32_t item_size)
{
    return sizeof(ring_buffer_t) + ((size_t)capacity * item_size);
}

static rb_handle_t* make_handle(ring_buffer_t* rb, size_t sz)
{
    rb_handle_t* h = malloc(sizeof(rb_handle_t));
    if (!h) return NULL;

    h->rb         = rb;
    h->total_size = sz;
    h->notify_fd  = eventfd(0, EFD_NONBLOCK);

    if (h->notify_fd == -1) {
        perror("[RB] eventfd");
        free(h);
        return NULL;
    }
    return h;
}

rb_handle_t* ring_buffer_create(const char* name, uint32_t capacity, uint32_t item_size)
{
    if (!name || !capacity || !item_size)                    return NULL;
    if (capacity  > RING_BUFFER_MAX_CAPACITY)                return NULL;
    if (item_size > RING_BUFFER_MAX_ITEM_SIZE)               return NULL;

    size_t sz = total_size(capacity, item_size);

    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd == -1 && errno == EEXIST) {
        shm_unlink(name);
        fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    }
    if (fd == -1) { perror("[RB] shm_open"); return NULL; }

    if (ftruncate(fd, (off_t)sz) == -1) {
        perror("[RB] ftruncate");
        close(fd); shm_unlink(name);
        return NULL;
    }

    ring_buffer_t* rb = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (rb == MAP_FAILED) { perror("[RB] mmap"); shm_unlink(name); return NULL; }

    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
    rb->capacity  = capacity;
    rb->item_size = item_size;

    rb_handle_t* h = make_handle(rb, sz);
    if (!h) { munmap(rb, sz); shm_unlink(name); return NULL; }

    printf("[RB] created '%s' cap=%u item_size=%u\n", name, capacity, item_size);
    return h;
}

rb_handle_t* ring_buffer_attach(const char* name)
{
    int fd = shm_open(name, O_RDWR, 0);
    if (fd == -1) {
        fprintf(stderr, "[RB] '%s' not found\n", name);
        return NULL;
    }

    // read header first to get capacity and item_size
    ring_buffer_t* tmp = mmap(NULL, sizeof(ring_buffer_t), PROT_READ, MAP_SHARED, fd, 0);
    if (tmp == MAP_FAILED) { perror("[RB] mmap header"); close(fd); return NULL; }

    uint32_t capacity  = tmp->capacity;
    uint32_t item_size = tmp->item_size;
    munmap(tmp, sizeof(ring_buffer_t));

    size_t sz = total_size(capacity, item_size);
    ring_buffer_t* rb = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (rb == MAP_FAILED) { perror("[RB] mmap full"); return NULL; }

    rb_handle_t* h = make_handle(rb, sz);
    if (!h) { munmap(rb, sz); return NULL; }

    printf("[RB] attached to '%s'\n", name);
    return h;
}

int ring_buffer_write(rb_handle_t* h, const void* item)
{
    if (!h || !item) return RB_ERROR_INIT;

    ring_buffer_t* rb = h->rb;
    uint32_t head      = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t next_head = (head + 1) % rb->capacity;
    uint32_t tail      = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if (next_head == tail) return RB_ERROR_FULL;

    memcpy(rb->data + ((size_t)head * rb->item_size), item, rb->item_size);
    atomic_store_explicit(&rb->head, next_head, memory_order_release);

    uint64_t one = 1;
    ssize_t __attribute__((unused)) _ign = write(h->notify_fd, &one, sizeof(one));  // wake up reader
    return RB_SUCCESS;
}

int ring_buffer_read(rb_handle_t* h, void* item)
{
    if (!h || !item) return RB_ERROR_INIT;

    ring_buffer_t* rb = h->rb;
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (tail == head) return RB_ERROR_EMPTY;

    memcpy(item, rb->data + ((size_t)tail * rb->item_size), rb->item_size);
    atomic_store_explicit(&rb->tail, (tail + 1) % rb->capacity, memory_order_release);

    return RB_SUCCESS;
}

int ring_buffer_is_full(const rb_handle_t* h)
{
    if (!h) return 1;
    uint32_t head = atomic_load_explicit(&h->rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&h->rb->tail, memory_order_acquire);
    return ((head + 1) % h->rb->capacity) == tail;
}

int ring_buffer_is_empty(const rb_handle_t* h)
{
    if (!h) return 1;
    uint32_t head = atomic_load_explicit(&h->rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&h->rb->tail, memory_order_acquire);
    return head == tail;
}

uint32_t ring_buffer_count(const rb_handle_t* h)
{
    if (!h) return 0;
    uint32_t head = atomic_load_explicit(&h->rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&h->rb->tail, memory_order_acquire);
    return (head >= tail) ? (head - tail) : (h->rb->capacity - tail + head);
}

void ring_buffer_detach(rb_handle_t* h)
{
    if (!h) return;
    close(h->notify_fd);
    munmap(h->rb, h->total_size);
    free(h);
}

void ring_buffer_destroy(rb_handle_t* h, const char* name)
{
    if (!h) return;
    close(h->notify_fd);
    munmap(h->rb, h->total_size);
    free(h);
    if (name) {
        shm_unlink(name);
        printf("[RB] destroyed '%s'\n", name);
    }
}