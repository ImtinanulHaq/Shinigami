#define _POSIX_C_SOURCE 200809L

/*
 * sm_request_id.c - Request ID tracking for distributed tracing
 */

#include "../infrastructure/sm_request_id.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/random.h>

/* Thread-local request ID storage */
static _Thread_local request_id_t g_thread_request_id = 0;

/* Global atomic counter for request ID generation */
static _Atomic(uint64_t) g_request_id_counter = 0;

/*
 * Request ID base: high 32 bits from process start, low 32 bits from counter.
 * Written exactly once by _do_init_request_id_base() via pthread_once — safe
 * to read afterwards without any lock or atomic because the pthread_once
 * barrier guarantees visibility to all subsequent callers.
 */
static uint32_t         g_request_id_base = 0;
static pthread_once_t   g_request_id_once = PTHREAD_ONCE_INIT;

static void _do_init_request_id_base(void)
{
    uint32_t random_val = 0;
    ssize_t s = getrandom(&random_val, sizeof(random_val), 0);
    if (s == sizeof(random_val)) {
        g_request_id_base = random_val;
    }
}

request_id_t sm_request_id_generate(void)
{
    /* No-op after first call; pthread_once provides the memory barrier. */
    pthread_once(&g_request_id_once, _do_init_request_id_base);

    /* Atomic increment of counter */
    uint64_t counter = atomic_fetch_add_explicit(&g_request_id_counter, 1,
                                                  memory_order_relaxed);
    
    /* Combine base with counter: high 32 bits = base, low 32 bits = counter */
    request_id_t req_id = ((uint64_t)g_request_id_base << 32) | (counter & 0xFFFFFFFFU);
    
    return req_id;
}

request_id_t sm_request_id_current(void)
{
    return g_thread_request_id;
}

void sm_request_id_set(request_id_t req_id)
{
    g_thread_request_id = req_id;
}

void sm_request_id_clear(void)
{
    g_thread_request_id = 0;
}

const char* sm_request_id_str(request_id_t req_id)
{
    /* Thread-safe buffer storage */
    static _Thread_local char buf[32];
    snprintf(buf, sizeof(buf), "%016lx", (unsigned long)req_id);
    return buf;
}
