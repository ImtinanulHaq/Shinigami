/**
 * @file    trace_store.c
 * @brief   Circular buffer for storing completed trace spans.
 */
#include "trace_store.h"
#include <string.h>

int trace_store_init(trace_store_t *store)
{
    memset(store, 0, sizeof(*store));
    pthread_rwlock_init(&store->lock, NULL);
    return 0;
}

void trace_store_destroy(trace_store_t *store)
{
    pthread_rwlock_destroy(&store->lock);
}

void trace_store_add_trace(trace_store_t *store, const trace_record_t *trace)
{
    pthread_rwlock_wrlock(&store->lock);

    /* Circular buffer: add to head position */
    memcpy(&store->traces[store->head], trace, sizeof(trace_record_t));
    store->head = (store->head + 1) % TRACE_STORE_MAX;
    
    if (store->count < TRACE_STORE_MAX)
        store->count++;

    pthread_rwlock_unlock(&store->lock);
}

uint32_t trace_store_get_recent(trace_store_t *store, trace_record_t *out,
                                 uint32_t max)
{
    pthread_rwlock_rdlock(&store->lock);

    uint32_t n = (store->count < max) ? store->count : max;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (store->head + TRACE_STORE_MAX - n + i) % TRACE_STORE_MAX;
        memcpy(&out[i], &store->traces[idx], sizeof(trace_record_t));
    }

    pthread_rwlock_unlock(&store->lock);
    return n;
}

int trace_store_find(trace_store_t *store, uint32_t trace_id, trace_record_t *out)
{
    pthread_rwlock_rdlock(&store->lock);

    for (uint32_t i = 0; i < store->count; i++) {
        if (store->traces[i].trace_id == trace_id) {
            memcpy(out, &store->traces[i], sizeof(trace_record_t));
            pthread_rwlock_unlock(&store->lock);
            return 1;
        }
    }

    pthread_rwlock_unlock(&store->lock);
    return 0;
}
