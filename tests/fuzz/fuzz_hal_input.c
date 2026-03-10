/**
 * @file fuzz_hal_input.c
 * @brief Fuzzer for HAL construction with random device paths and configs.
 *
 * Exercises argument validation in all four HAL factories.
 * Both LibFuzzer and standalone modes supported (see fuzz_sm_protocol.c).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../../dev/hal/layers/audio/audio_hal.h"
#include "../../../dev/hal/layers/camera/camera_hal.h"
#include "../../../dev/hal/layers/sensors/sensor_hal.h"
#include "../../../dev/hal/layers/gpio/gpio_hal.h"

static uint32_t g_rng = 0xABCD1234;
static uint32_t xshift(void) { g_rng^=g_rng<<13; g_rng^=g_rng>>17; g_rng^=g_rng<<5; return g_rng; }

/* Extract a null-terminated string from fuzz data at offset *pos */
static void extract_str(const uint8_t *data, size_t size,
                        size_t *pos, char *out, size_t max)
{
    size_t i = 0;
    while (*pos < size && i < max - 1) {
        out[i++] = (char)data[(*pos)++];
        if (out[i-1] == '\0') return;
    }
    out[i] = '\0';
}

static void fuzz_one(const uint8_t *data, size_t size)
{
    if (size < 4) return;

    size_t pos = 0;
    char name[256], path[256];
    extract_str(data, size, &pos, name, sizeof(name));
    extract_str(data, size, &pos, path, sizeof(path));

    /* Audio HAL */
    {
        audio_config_t cfg;
        if (pos + sizeof(cfg) <= size) {
            memcpy(&cfg, data + pos, sizeof(cfg));
            pos += sizeof(cfg);
        } else {
            cfg = audio_hal_default_config(AUDIO_DIRECTION_CAPTURE);
        }
        hw_device_t *d = audio_hal_create(name, path, &cfg);
        if (d) audio_hal_destroy(d);
    }

    /* Camera HAL */
    {
        camera_config_t cfg;
        if (pos + sizeof(cfg) <= size) {
            memcpy(&cfg, data + pos, sizeof(cfg));
            pos += sizeof(cfg);
        } else {
            cfg = camera_hal_default_config();
        }
        hw_device_t *d = camera_hal_create(name, path, &cfg);
        if (d) camera_hal_destroy(d);
    }

    /* Sensor HAL */
    {
        sensor_config_t cfg;
        if (pos + sizeof(cfg) <= size) {
            memcpy(&cfg, data + pos, sizeof(cfg));
            pos += sizeof(cfg);
        } else {
            cfg = sensor_hal_default_config(SENSOR_TYPE_ACCEL);
        }
        hw_device_t *d = sensor_hal_create(name, path, &cfg);
        if (d) sensor_hal_destroy(d);
    }

    /* GPIO HAL */
    {
        gpio_config_t cfg;
        uint32_t pin = 0;
        if (pos + sizeof(cfg) + sizeof(pin) <= size) {
            memcpy(&cfg, data + pos, sizeof(cfg)); pos += sizeof(cfg);
            memcpy(&pin, data + pos, sizeof(pin)); pos += sizeof(pin);
        } else {
            cfg = gpio_hal_default_config(0);
        }
        hw_device_t *d = gpio_hal_create(name, &cfg);
        if (d) gpio_hal_destroy(d);
    }

    /* camera_hal_validate_device_path with fuzz path */
    camera_hal_validate_device_path(path);
}

#ifndef FUZZ_STANDALONE

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
    long iter = FUZZ_ITERATIONS;
    const char *env = getenv("FUZZ_ITERATIONS");
    if (env) iter = atol(env);

    printf("[fuzz_hal_input] running %ld iterations\n", iter);

    uint8_t buf[512];
    for (long i = 0; i < iter; i++) {
        size_t len = (xshift() % sizeof(buf)) + 1;
        for (size_t j = 0; j < len; j++) buf[j] = (uint8_t)xshift();
        fuzz_one(buf, len);
    }

    printf("[fuzz_hal_input] done — no crashes\n");
    return 0;
}

#endif
