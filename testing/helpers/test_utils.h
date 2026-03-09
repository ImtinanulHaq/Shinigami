/**
 * @file test_utils.h
 * @brief Shared utilities for middleware test suites.
 *
 * Provides timing helpers, socket/pipe factories, process spawning,
 * random data generation, and synchronisation primitives used across
 * unit, integration, and stress tests.
 */
#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Timing ───────────────────────────────────────────────────────── */

/** Monotonic timestamp in nanoseconds. */
uint64_t tu_now_ns(void);

/** Monotonic timestamp in milliseconds. */
uint64_t tu_now_ms(void);

/** Sleep for the given number of milliseconds. */
void tu_sleep_ms(unsigned int ms);

/** Elapsed milliseconds between two tu_now_ms() samples. */
static inline long tu_elapsed_ms(uint64_t start_ms, uint64_t end_ms)
{
    return (long)(end_ms - start_ms);
}

/* ── Socket helpers ───────────────────────────────────────────────── */

/**
 * Create a connected UNIX domain socket pair (AF_UNIX, SOCK_STREAM).
 * fds[0] = server side, fds[1] = client side.
 * Returns 0 on success, -1 on error.
 */
int tu_socketpair(int fds[2]);

/**
 * Create a UNIX domain socket listening at @p path.
 * Returns the listener fd, or -1 on error.
 */
int tu_unix_listen(const char *path);

/**
 * Connect to a UNIX domain socket at @p path.
 * Returns the connected fd, or -1 on error.
 */
int tu_unix_connect(const char *path);

/** Close and remove a UNIX domain socket created with tu_unix_listen(). */
void tu_unix_cleanup(const char *path, int listen_fd);

/* ── Pipe helpers ─────────────────────────────────────────────────── */

/**
 * Create a non-blocking pipe pair.
 * fds[0] = read end, fds[1] = write end.
 */
int tu_pipe_nonblock(int fds[2]);

/* ── Random data ──────────────────────────────────────────────────── */

/** Fill @p buf with pseudo-random bytes seeded by @p seed. */
void tu_rand_fill(void *buf, size_t len, uint32_t seed);

/** Return a pseudo-random uint32 (LCG). */
uint32_t tu_rand_u32(uint32_t *state);

/* ── Process spawning ─────────────────────────────────────────────── */

/**
 * Fork a child that exec()s @p argv[0] with @p argv.
 * Returns child PID, or -1 on error.
 */
pid_t tu_spawn(const char *const argv[]);

/** Send SIGTERM to @p pid and wait up to @p timeout_ms ms. Returns exit status. */
int tu_kill_wait(pid_t pid, unsigned int timeout_ms);

/* ── Thread barrier ───────────────────────────────────────────────── */

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
    int             count;
    int             total;
    int             generation;
} tu_barrier_t;

int  tu_barrier_init(tu_barrier_t *b, int n);
void tu_barrier_wait(tu_barrier_t *b);
void tu_barrier_destroy(tu_barrier_t *b);

/* ── Temp files ───────────────────────────────────────────────────── */

/**
 * Create a uniquely named temporary socket path under /tmp.
 * Writes at most @p len bytes (including NUL) into @p out.
 */
void tu_tmp_socket_path(char *out, size_t len, const char *prefix);

/**
 * Create a uniquely named shared memory ring buffer name.
 * Writes at most @p len bytes into @p out (must start with '/').
 */
void tu_tmp_shm_name(char *out, size_t len, const char *prefix);

/** Remove the shared memory object with the given name (shm_unlink wrapper). */
void tu_shm_unlink(const char *name);

/* ── Atomic counter (for stress tests) ───────────────────────────── */

typedef struct { _Atomic long value; } tu_counter_t;

static inline void tu_counter_init(tu_counter_t *c) { atomic_init(&c->value, 0); }
static inline void tu_counter_inc(tu_counter_t *c)  { atomic_fetch_add(&c->value, 1); }
static inline long tu_counter_get(const tu_counter_t *c) { return atomic_load(&c->value); }

#ifdef __cplusplus
}
#endif

#endif /* TEST_UTILS_H */
