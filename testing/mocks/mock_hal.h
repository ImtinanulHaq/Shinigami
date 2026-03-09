/**
 * @file mock_hal.h
 * @brief Controllable HAL stubs for service-level unit tests.
 *
 * Replaces real HAL operations with tracked function stubs.  Each
 * stub records call counts and last arguments so tests can assert
 * on call sequences without real hardware.
 */
#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>  /* ssize_t */
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Call record ──────────────────────────────────────────────────── */

typedef struct {
    int    open_calls;
    int    close_calls;
    int    start_calls;
    int    stop_calls;
    int    read_calls;
    int    write_calls;
    int    control_calls;

    /* Configurable return values */
    int    open_ret;
    int    close_ret;
    int    start_ret;
    int    stop_ret;
    ssize_t read_ret;   /**< Bytes to return from read (>=0) or error (<0) */
    int    write_ret;
    int    control_ret;

    /* Last read buffer contents injected by test */
    uint8_t read_data[4096];
    size_t  read_data_len;

    /* Last control command received */
    int     last_control_cmd;
} mock_hal_state_t;

/** Reset all counts and set all returns to 0 / success. */
void mock_hal_reset(mock_hal_state_t *s);

/** Get the global singleton state (used by mock function implementations). */
mock_hal_state_t *mock_hal_get_state(void);

/* ── Drop-in stubs ────────────────────────────────────────────────── */

/**
 * These functions have the same signature as the real HAL ops and
 * are registered into hw_device_ops_t during tests.
 */
int    mock_hal_open   (void *dev);
int    mock_hal_close  (void *dev);
int    mock_hal_start  (void *dev);
int    mock_hal_stop   (void *dev);
ssize_t mock_hal_read  (void *dev, void *buf, size_t len);
int    mock_hal_write  (void *dev, const void *buf, size_t len);
int    mock_hal_control(void *dev, int cmd, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_HAL_H */
