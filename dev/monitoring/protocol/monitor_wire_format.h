/**
 * @file    monitor_wire_format.h
 * @brief   Inline serialisation helpers for the monitor IPC protocol.
 *
 * All functions operate on non-blocking file descriptors.
 * Framing: mon_msg_header_t followed by `length` payload bytes.
 * All fields are in host byte order (same-machine IPC, no network portability
 * requirement — both processes run on the same kernel).
 *
 * @thread_safety  These are stateless helpers; caller must ensure fd is
 *                 not concurrently written/read by multiple threads.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include "monitor_ipc_protocol.h"

/* ── Return codes ─────────────────────────────────────────────────────────── */

#define MON_WIRE_OK          0
#define MON_WIRE_ERR_IO     -1   /**< read()/write() returned error. */
#define MON_WIRE_ERR_EOF    -2   /**< Peer closed the connection. */
#define MON_WIRE_ERR_FRAME  -3   /**< Magic / version mismatch. */
#define MON_WIRE_ERR_SIZE   -4   /**< Payload larger than MON_MAX_PAYLOAD_BYTES. */
#define MON_WIRE_ERR_AGAIN  -5   /**< EAGAIN / EWOULDBLOCK — try again later. */

/* ── Low-level I/O helpers ────────────────────────────────────────────────── */

/**
 * @brief  Write exactly @p n bytes to @p fd, retrying on EINTR.
 * @return Number of bytes written, or -1 on error (errno set).
 */
static inline ssize_t mon_write_all(int fd, const void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = write(fd, (const char *)buf + done, n - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)r;
    }
    return (ssize_t)done;
}

/**
 * @brief  Read exactly @p n bytes from @p fd, retrying on EINTR and on
 *         EAGAIN/EWOULDBLOCK that occur after a partial read.
 *
 * When the socket has SO_RCVTIMEO set (rather than O_NONBLOCK), a timeout
 * returns EAGAIN.  If we already have some bytes we retry indefinitely so
 * that large payloads (e.g. multi-MB snapshot structs) arrive completely.
 * The first EAGAIN with zero bytes read is returned as MON_WIRE_ERR_AGAIN
 * so callers can distinguish "no data yet" from "mid-stream timeout".
 *
 * @return MON_WIRE_OK, MON_WIRE_ERR_EOF, MON_WIRE_ERR_IO, or MON_WIRE_ERR_AGAIN.
 */
static inline int mon_read_all(int fd, void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, (char *)buf + done, n - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (done == 0) return MON_WIRE_ERR_AGAIN; /* no data yet */
                /* partial read — wait 1 ms then retry */
                struct timespec sl = { 0, 1000000L };
                nanosleep(&sl, NULL);
                continue;
            }
            return MON_WIRE_ERR_IO;
        }
        if (r == 0) return MON_WIRE_ERR_EOF;
        done += (size_t)r;
    }
    return MON_WIRE_OK;
}

/* ── Frame send / receive ─────────────────────────────────────────────────── */

/**
 * @brief  Send a framed message.
 * @param  fd        Connected socket fd.
 * @param  type      mon_msg_type_t value.
 * @param  seq       Caller-managed sequence number.
 * @param  payload   Pointer to payload bytes (may be NULL if payload_len==0).
 * @param  payload_len  Number of payload bytes.
 * @return MON_WIRE_OK or MON_WIRE_ERR_IO.
 */
static inline int mon_send_msg(int fd, mon_msg_type_t type, uint32_t seq,
                                const void *payload, uint32_t payload_len)
{
    mon_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic   = MON_MSG_MAGIC;
    hdr.type    = (uint8_t)type;
    hdr.version = MON_PROTOCOL_VERSION;
    hdr.flags   = 0;
    hdr.length  = payload_len;
    hdr.seq     = seq;

    if (mon_write_all(fd, &hdr, sizeof(hdr)) < 0) return MON_WIRE_ERR_IO;
    if (payload_len > 0 && payload) {
        if (mon_write_all(fd, payload, payload_len) < 0) return MON_WIRE_ERR_IO;
    }
    return MON_WIRE_OK;
}

/**
 * @brief  Receive a framed message header, validate magic + version.
 * @param  fd      Connected socket fd.
 * @param  hdr_out Output header (caller provides storage).
 * @return MON_WIRE_OK, MON_WIRE_ERR_EOF, MON_WIRE_ERR_IO, MON_WIRE_ERR_FRAME,
 *         MON_WIRE_ERR_SIZE, or MON_WIRE_ERR_AGAIN.
 */
static inline int mon_recv_header(int fd, mon_msg_header_t *hdr_out)
{
    int rc = mon_read_all(fd, hdr_out, sizeof(*hdr_out));
    if (rc != MON_WIRE_OK) return rc;

    if (hdr_out->magic != MON_MSG_MAGIC)          return MON_WIRE_ERR_FRAME;
    if (hdr_out->version != MON_PROTOCOL_VERSION) return MON_WIRE_ERR_FRAME;
    if (hdr_out->length > MON_MAX_PAYLOAD_BYTES)  return MON_WIRE_ERR_SIZE;
    return MON_WIRE_OK;
}

/**
 * @brief  Receive header + payload into caller-supplied buffer.
 * @param  fd         Connected socket fd.
 * @param  hdr_out    Output header.
 * @param  buf        Caller buffer for payload.
 * @param  buf_size   Size of caller buffer.
 * @param  read_len   Set to actual bytes placed in buf.
 * @return MON_WIRE_OK, or error codes.
 */
static inline int mon_recv_msg(int fd, mon_msg_header_t *hdr_out,
                                void *buf, size_t buf_size, size_t *read_len)
{
    int rc = mon_recv_header(fd, hdr_out);
    if (rc != MON_WIRE_OK) return rc;

    *read_len = 0;
    if (hdr_out->length == 0) return MON_WIRE_OK;

    if (hdr_out->length > buf_size) return MON_WIRE_ERR_SIZE;

    rc = mon_read_all(fd, buf, hdr_out->length);
    if (rc != MON_WIRE_OK) return rc;

    *read_len = hdr_out->length;
    return MON_WIRE_OK;
}

/**
 * @brief  Discard the payload of a received message (skip over it).
 * @return MON_WIRE_OK or MON_WIRE_ERR_IO.
 */
static inline int mon_discard_payload(int fd, uint32_t length)
{
    char tmp[256];
    uint32_t remaining = length;
    while (remaining > 0) {
        uint32_t chunk = remaining < sizeof(tmp) ? remaining : (uint32_t)sizeof(tmp);
        int rc = mon_read_all(fd, tmp, chunk);
        if (rc != MON_WIRE_OK) return rc;
        remaining -= chunk;
    }
    return MON_WIRE_OK;
}

/**
 * @brief  Send a GOODBYE message and close the socket.
 * @param  fd   Socket to close.
 * @return MON_WIRE_OK.
 */
static inline int mon_send_goodbye(int fd, uint32_t seq)
{
    mon_send_msg(fd, MON_MSG_GOODBYE, seq, NULL, 0);
    close(fd);
    return MON_WIRE_OK;
}
