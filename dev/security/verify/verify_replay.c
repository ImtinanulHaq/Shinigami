#define _POSIX_C_SOURCE 200809L
#include "verify_replay.h"
#include <string.h>
#include <errno.h>
#include <syslog.h>

#define DEFAULT_TIMESTAMP_WINDOW 30

int replay_context_init(replay_context_t* ctx, uint32_t timestamp_window_sec)
{
    if (!ctx) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->timestamp_window_sec = (timestamp_window_sec > 0)
                                ? timestamp_window_sec
                                : DEFAULT_TIMESTAMP_WINDOW;

    ctx->strict_ordering = 1;
    ctx->enable_timestamp_check = 1;

    int ret = pthread_mutex_init(&ctx->lock, NULL);
    if (ret != 0) {
        syslog(LOG_ERR, "[replay] pthread_mutex_init failed: %s", strerror(ret));
        return -1;
    }

    syslog(LOG_INFO, "[replay] Initialized with %u second window",
           ctx->timestamp_window_sec);
    return 0;
}

uint64_t replay_get_monotonic_time(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        syslog(LOG_ERR, "[replay] clock_gettime(MONOTONIC) failed: %s",
               strerror(errno));
        return 0;
    }

    return (uint64_t)ts.tv_sec;
}

int replay_check(replay_context_t* ctx, uint64_t timestamp, uint32_t sequence)
{
    if (!ctx) {
        return -1;
    }

    pthread_mutex_lock(&ctx->lock);

    uint64_t now = replay_get_monotonic_time();
    if (now == 0) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    if (ctx->enable_timestamp_check) {

        if (now > timestamp && (now - timestamp) > ctx->timestamp_window_sec) {
            syslog(LOG_WARNING, "[replay] Message timestamp too old: %lu (current: %lu, window: %u)",
                   timestamp, now, ctx->timestamp_window_sec);
            pthread_mutex_unlock(&ctx->lock);
            return 0;
        }

        if (timestamp > now && (timestamp - now) > ctx->timestamp_window_sec) {
            syslog(LOG_WARNING, "[replay] Message timestamp in future: %lu (current: %lu)",
                   timestamp, now);
            pthread_mutex_unlock(&ctx->lock);
            return 0;
        }
    }

    if (ctx->strict_ordering && ctx->last_timestamp > 0) {

        if (sequence <= ctx->last_sequence) {
            syslog(LOG_WARNING, "[replay] Out-of-order message: seq %u (last: %u)",
                   sequence, ctx->last_sequence);
            pthread_mutex_unlock(&ctx->lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&ctx->lock);
    return 1;
}

void replay_update(replay_context_t* ctx, uint64_t timestamp, uint32_t sequence)
{
    if (!ctx) {
        return;
    }

    pthread_mutex_lock(&ctx->lock);

    ctx->last_timestamp = timestamp;
    ctx->last_sequence = sequence;

    pthread_mutex_unlock(&ctx->lock);
}

void replay_set_strict_ordering(replay_context_t* ctx, int enable)
{
    if (!ctx) {
        return;
    }

    pthread_mutex_lock(&ctx->lock);
    ctx->strict_ordering = enable;
    pthread_mutex_unlock(&ctx->lock);

    syslog(LOG_INFO, "[replay] Strict ordering %s",
           enable ? "enabled" : "disabled");
}

void replay_set_timestamp_check(replay_context_t* ctx, int enable)
{
    if (!ctx) {
        return;
    }

    pthread_mutex_lock(&ctx->lock);
    ctx->enable_timestamp_check = enable;
    pthread_mutex_unlock(&ctx->lock);

    syslog(LOG_INFO, "[replay] Timestamp check %s",
           enable ? "enabled" : "disabled");
}

void replay_context_cleanup(replay_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    pthread_mutex_destroy(&ctx->lock);

    memset(ctx, 0, sizeof(*ctx));
}
