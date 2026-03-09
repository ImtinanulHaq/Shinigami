/**
 * @file fuzz_ipc_messages.c
 * @brief Fuzzer — malformed IPC messages fed to SM registry + protocol.
 *
 * Simulates what happens when a bug or malicious client sends garbage
 * bytes to the server's message processing pipeline.  All validators
 * and registry lookups are exercised; any crash/assertion failure
 * is caught by ASAN/UBSan.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../../dev/core/service_manager/infrastructure/sm_protocol.h"

static uint32_t g_rng = 0xFEEDFACE;
static uint32_t xshift(void) { g_rng^=g_rng<<13; g_rng^=g_rng>>17; g_rng^=g_rng<<5; return g_rng; }

/* Feed arbitrary bytes through every entry-point that touches IPC data */
static void fuzz_one(const uint8_t *data, size_t size)
{
    /* --- Protocol validators ---------------------------------------- */
    if (size >= sizeof(sm_message_header_t)) {
        const sm_message_header_t *hdr = (const sm_message_header_t *)data;
        sm_validate_header(hdr, size);
    }

    /* Treat data as a name string */
    {
        char name[SM_MAX_SERVICE_NAME + 4];
        size_t n = size < sizeof(name) - 1 ? size : sizeof(name) - 1;
        memcpy(name, data, n); name[n] = '\0';
        sm_validate_service_name(name);
    }

    /* Treat data as a socket path */
    {
        char path[SM_MAX_SOCKET_PATH + 4];
        size_t n = size < sizeof(path) - 1 ? size : sizeof(path) - 1;
        memcpy(path, data, n); path[n] = '\0';
        sm_validate_socket_path(path);
    }

    /* Treat first 4 bytes as a message size */
    if (size >= 4) {
        uint32_t sz; memcpy(&sz, data, sizeof(sz));
        sm_validate_message_size(sz);
    }

    /* --- Registry: attempt add with garbage entry ------------------- */
    {
        service_entry_t e;
        memset(&e, 0, sizeof(e));
        /* Populate name from fuzz data (bounded) */
        size_t nlen = size < sizeof(e.name) - 1 ? size : sizeof(e.name) - 1;
        memcpy(e.name, data, nlen); e.name[nlen] = '\0';

        /* Only attempt if name is non-empty and printable-ish */
        if (e.name[0] != '\0') {
            size_t poff = nlen < size ? nlen : 0;
            size_t plen = (size - poff);
            if (plen > sizeof(e.socket_path) - 1) plen = sizeof(e.socket_path) - 1;
            memcpy(e.socket_path, data + poff, plen);
            e.socket_path[plen] = '\0';

            if (size >= sizeof(e.pid))
                memcpy(&e.pid, data + (size - sizeof(e.pid)), sizeof(e.pid));

            sm_registry_add(&e);      /* may fail gracefully — that's fine */
            sm_registry_find_copy(e.name, &e); /* lookup of maybe-added entry */
            sm_registry_remove(e.name);
        }
    }
}

#ifndef FUZZ_STANDALONE

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    sm_registry_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_one(data, size);
    return 0;
}

#else

#ifndef FUZZ_ITERATIONS
#define FUZZ_ITERATIONS 100000
#endif

int main(void)
{
    sm_registry_init();

    long iter = FUZZ_ITERATIONS;
    const char *env = getenv("FUZZ_ITERATIONS");
    if (env) iter = atol(env);

    printf("[fuzz_ipc_messages] running %ld iterations\n", iter);

    uint8_t buf[SM_MAX_MESSAGE_SIZE + 32];
    for (long i = 0; i < iter; i++) {
        size_t len = (xshift() % sizeof(buf)) + 1;
        for (size_t j = 0; j < len; j++) buf[j] = (uint8_t)xshift();
        fuzz_one(buf, len);
    }

    sm_registry_cleanup();
    printf("[fuzz_ipc_messages] done — no crashes\n");
    return 0;
}

#endif
