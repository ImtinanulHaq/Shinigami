/**
 * @file fuzz_sm_protocol.c
 * @brief Fuzzer for the Service Manager protocol parser.
 *
 * Compilation (LibFuzzer):
 *   clang -fsanitize=fuzzer,address,undefined -g -O1 \
 *         fuzz_sm_protocol.c <sm_protocol_objs> -o fuzz_sm_protocol
 *
 * Standalone (no LibFuzzer / GCC):
 *   Define FUZZ_STANDALONE and FUZZ_ITERATIONS (default 100000).
 *   gcc -DFUZZ_STANDALONE -fsanitize=address,undefined -g -O0 \
 *       fuzz_sm_protocol.c <sm_protocol_objs> -o fuzz_sm_protocol
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../../dev/core/service_manager/infrastructure/sm_protocol.h"

/* ── Fuzz target: feed arbitrary bytes to every protocol validator ── */

static void fuzz_one(const uint8_t *data, size_t size)
{
    if (size < sizeof(sm_message_header_t)) return;

    const sm_message_header_t *hdr = (const sm_message_header_t *)data;

    /* These must never crash regardless of input */
    sm_validate_header(hdr, size);

    if (size > sizeof(sm_message_header_t)) {
        /* Try treating the payload as a service name */
        char name[SM_MAX_SERVICE_NAME + 4];
        size_t copy_len = size - sizeof(sm_message_header_t);
        if (copy_len > sizeof(name) - 1) copy_len = sizeof(name) - 1;
        memcpy(name, data + sizeof(sm_message_header_t), copy_len);
        name[copy_len] = '\0';
        sm_validate_service_name(name);

        /* And as a socket path */
        char path[SM_MAX_SOCKET_PATH + 4];
        if (copy_len > sizeof(path) - 1) copy_len = sizeof(path) - 1;
        memcpy(path, data + sizeof(sm_message_header_t), copy_len);
        path[copy_len] = '\0';
        sm_validate_socket_path(path);

        /* And as a message size */
        uint32_t sz;
        if (size >= sizeof(sm_message_header_t) + sizeof(uint32_t))
            memcpy(&sz, data + sizeof(sm_message_header_t), sizeof(sz));
        else
            sz = (uint32_t)size;
        sm_validate_message_size(sz);
    }
}

/* ── LibFuzzer entry point ─────────────────────────────────────────── */

#ifndef FUZZ_STANDALONE

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_one(data, size);
    return 0;
}

#else /* FUZZ_STANDALONE */

#ifndef FUZZ_ITERATIONS
#define FUZZ_ITERATIONS 100000
#endif

static uint32_t xorshift(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

int main(void)
{
    uint32_t rng = 0xDEADBEEF;
    uint8_t  buf[SM_MAX_MESSAGE_SIZE + 16];
    long     iter = FUZZ_ITERATIONS;
    const char *env = getenv("FUZZ_ITERATIONS");
    if (env) iter = atol(env);

    printf("[fuzz_sm_protocol] running %ld iterations\n", iter);

    for (long i = 0; i < iter; i++) {
        size_t len = (xorshift(&rng) % (sizeof(buf))) + 1;
        for (size_t j = 0; j < len; j++) buf[j] = (uint8_t)xorshift(&rng);
        fuzz_one(buf, len);
    }

    printf("[fuzz_sm_protocol] done — no crashes\n");
    return 0;
}

#endif /* FUZZ_STANDALONE */
