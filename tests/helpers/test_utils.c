/**
 * @file test_utils.c
 * @brief Implementation of shared test utilities.
 */
#include "test_utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdatomic.h>

/* ── Timing ───────────────────────────────────────────────────────── */

uint64_t tu_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t tu_now_ms(void)
{
    return tu_now_ns() / 1000000ULL;
}

void tu_sleep_ms(unsigned int ms)
{
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}

/* ── Socket helpers ───────────────────────────────────────────────── */

int tu_socketpair(int fds[2])
{
    return socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
}

int tu_unix_listen(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int tu_unix_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void tu_unix_cleanup(const char *path, int listen_fd)
{
    if (listen_fd >= 0) close(listen_fd);
    if (path) unlink(path);
}

int tu_pipe_nonblock(int fds[2])
{
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) < 0) return -1;
    return 0;
}

/* ── Random data ──────────────────────────────────────────────────── */

uint32_t tu_rand_u32(uint32_t *state)
{
    /* Xorshift32 — deterministic, fast, good distribution for tests */
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x <<  5;
    *state = x;
    return x;
}

void tu_rand_fill(void *buf, size_t len, uint32_t seed)
{
    uint8_t *p = (uint8_t *)buf;
    uint32_t state = seed ? seed : 0xDEADBEEFu;
    for (size_t i = 0; i < len; i++) {
        if ((i & 3) == 0) tu_rand_u32(&state);
        p[i] = (uint8_t)(state >> (8 * (i & 3)));
    }
}

/* ── Process spawning ─────────────────────────────────────────────── */

pid_t tu_spawn(const char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    return pid;
}

int tu_kill_wait(pid_t pid, unsigned int timeout_ms)
{
    kill(pid, SIGTERM);
    uint64_t deadline = tu_now_ms() + timeout_ms;
    while (tu_now_ms() < deadline) {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        tu_sleep_ms(10);
    }
    kill(pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);
    return -1;
}

/* ── Thread barrier ───────────────────────────────────────────────── */

int tu_barrier_init(tu_barrier_t *b, int n)
{
    b->count      = 0;
    b->total      = n;
    b->generation = 0;
    if (pthread_mutex_init(&b->mtx, NULL) != 0) return -1;
    if (pthread_cond_init(&b->cond, NULL) != 0) {
        pthread_mutex_destroy(&b->mtx);
        return -1;
    }
    return 0;
}

void tu_barrier_wait(tu_barrier_t *b)
{
    pthread_mutex_lock(&b->mtx);
    int gen = b->generation;
    b->count++;
    if (b->count == b->total) {
        b->count = 0;
        b->generation++;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (b->generation == gen)
            pthread_cond_wait(&b->cond, &b->mtx);
    }
    pthread_mutex_unlock(&b->mtx);
}

void tu_barrier_destroy(tu_barrier_t *b)
{
    pthread_cond_destroy(&b->cond);
    pthread_mutex_destroy(&b->mtx);
}

/* ── Temp files ───────────────────────────────────────────────────── */

void tu_tmp_socket_path(char *out, size_t len, const char *prefix)
{
    snprintf(out, len, "/tmp/%s_%d_%llu",
             prefix ? prefix : "test",
             (int)getpid(),
             (unsigned long long)tu_now_ns());
}

void tu_tmp_shm_name(char *out, size_t len, const char *prefix)
{
    snprintf(out, len, "/%s_%d_%llu",
             prefix ? prefix : "test",
             (int)getpid(),
             (unsigned long long)tu_now_ns());
}

void tu_shm_unlink(const char *name)
{
    if (name) shm_unlink(name);
}
