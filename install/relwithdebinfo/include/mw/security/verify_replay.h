#ifndef VERIFY_REPLAY_H
#define VERIFY_REPLAY_H

#include <stdint.h>
#include <pthread.h>
#include <time.h>

typedef struct {
    uint64_t last_timestamp;
    uint32_t last_sequence;
    uint32_t timestamp_window_sec;
    int      strict_ordering;
    int      enable_timestamp_check;
    pthread_mutex_t lock;
} replay_context_t;

int replay_context_init(replay_context_t* ctx, uint32_t timestamp_window_sec);

int replay_check(replay_context_t* ctx, uint64_t timestamp, uint32_t sequence);

void replay_update(replay_context_t* ctx, uint64_t timestamp, uint32_t sequence);

uint64_t replay_get_monotonic_time(void);

void replay_set_strict_ordering(replay_context_t* ctx, int enable);

void replay_set_timestamp_check(replay_context_t* ctx, int enable);

void replay_context_cleanup(replay_context_t* ctx);

#endif
