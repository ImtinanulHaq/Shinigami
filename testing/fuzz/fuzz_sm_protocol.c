/**
 * @file fuzz_sm_protocol.c
 * @brief Fuzzer for the Service Manager protocol parser.
 *
 * Standalone (gcc): -DFUZZ_STANDALONE -DFUZZ_ITERATIONS=1000000
 * LibFuzzer (clang): -fsanitize=fuzzer,address,undefined
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../../dev/core/service_manager/infrastructure/sm_protocol.h"

#ifndef FUZZ_ITERATIONS
# define FUZZ_ITERATIONS 1000000
#endif

static void fuzz_one(const uint8_t *data, size_t size)
{
    if (size < sizeof(sm_hdr_t)) return;

    const sm_hdr_t *hdr = (const sm_hdr_t *)data;

    /* Validation functions must never crash regardless of input */
    sm_validate_header(hdr, size);

    if (size > sizeof(sm_hdr_t)) {
        size_t pay_len = size - sizeof(sm_hdr_t);

        /* Try as service name */
        char name[SM_MAX_NAME + 4];
        size_t copy = pay_len < sizeof(name)-1 ? pay_len : sizeof(name)-1;
        memcpy(name, data + sizeof(sm_hdr_t), copy);
        name[copy] = '\0';
        sm_validate_service_name(name);

        /* Try as socket path */
        char path[SM_MAX_PATH + 4];
        copy = pay_len < sizeof(path)-1 ? pay_len : sizeof(path)-1;
        memcpy(path, data + sizeof(sm_hdr_t), copy);
        path[copy] = '\0';
        sm_validate_socket_path(path);

        /* Try as message-size check with each known message type */
        uint32_t sz;
        if (size >= sizeof(sm_hdr_t) + sizeof(uint32_t))
            memcpy(&sz, data + sizeof(sm_hdr_t), sizeof(sz));
        else
            sz = (uint32_t)size;

        sm_validate_message_size(sz, SM_MSG_REGISTER);
        sm_validate_message_size(sz, SM_MSG_LOOKUP);
        sm_validate_message_size(sz, SM_MSG_HEARTBEAT);
        sm_validate_message_size(sz, hdr->type);
    }
}

#ifdef FUZZ_STANDALONE
int main(void)
{
    srand(42);
    unsigned long long count = 0;
    printf("fuzz_sm_protocol: starting %d iterations\n", FUZZ_ITERATIONS);
    for (int i = 0; i < FUZZ_ITERATIONS; i++) {
        size_t sz = (size_t)(rand() % 512);
        uint8_t *buf = (uint8_t *)malloc(sz + 1);
        if (!buf) continue;
        for (size_t j = 0; j < sz; j++) buf[j] = (uint8_t)(rand() & 0xFF);
        fuzz_one(buf, sz);
        free(buf);
        count++;
    }
    printf("fuzz_sm_protocol: PASSED  iterations=%llu  no crashes\n", count);
    return 0;
}
#else
/* LibFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_one(data, size);
    return 0;
}
#endif
