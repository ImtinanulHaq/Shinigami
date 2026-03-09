/**
 * @file test_sm_hal_integration.c
 * @brief Integration: Service Manager + HAL layer.
 *
 * Tests that the SM registry correctly tracks HAL-backed services,
 * and that HAL lifecycle (init→open→close→destroy) interacts cleanly
 * with SM registration without data corruption or leaks.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hal.h"

#include "../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>
#include <pthread.h>

static mock_hal_state_t g_hal;

TEST_GROUP(SM_HAL_Integration);

TEST_SETUP(SM_HAL_Integration)
{
    sm_registry_init();
    mock_hal_reset(&g_hal);
    g_hal.open_ret  = HAL_SUCCESS;
    g_hal.close_ret = HAL_SUCCESS;
    g_hal.read_ret  = HAL_SUCCESS;
}

TEST_TEAR_DOWN(SM_HAL_Integration)
{
    sm_registry_cleanup();
}

/* ── Scenario 1: HAL device opens → SM registration succeeds ──────── */

TEST(SM_HAL_Integration, OpenHAL_ThenRegister_EntryPresent)
{
    /* Simulate a service: open HAL device */
    int r = mock_hal_open(&g_hal, "audio_service");
    TEST_ASSERT_EQUAL_INT(HAL_SUCCESS, r);
    TEST_ASSERT_EQUAL_INT(1, g_hal.open_count);

    /* Register the service in SM */
    service_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name,        "audio_service", sizeof(e.name) - 1);
    strncpy(e.socket_path, "/tmp/audio.sock", sizeof(e.socket_path) - 1);
    e.pid    = 1001;
    e.status = SERVICE_STATUS_RUNNING;
    r = sm_registry_add(&e);
    TEST_ASSERT_EQUAL_INT(0, r);

    /* Verify */
    service_entry_t out;
    r = sm_registry_find_copy("audio_service", &out);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(SERVICE_STATUS_RUNNING, (int)out.status);
}

/* ── Scenario 2: HAL close → SM deregistration ──────────────────────*/

TEST(SM_HAL_Integration, CloseHAL_ThenDeregister_EntryGone)
{
    mock_hal_open(&g_hal, "sensor_service");

    service_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name,        "sensor_service", sizeof(e.name) - 1);
    strncpy(e.socket_path, "/tmp/sensor.sock", sizeof(e.socket_path) - 1);
    e.pid    = 1002;
    e.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&e);

    /* Service stops: close HAL then remove from SM */
    int r = mock_hal_close(&g_hal);
    TEST_ASSERT_EQUAL_INT(HAL_SUCCESS, r);

    r = sm_registry_remove("sensor_service");
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());
}

/* ── Scenario 3: HAL read error → SM status update ──────────────────*/

TEST(SM_HAL_Integration, HAL_ReadError_UpdateSMStatus_Stopped)
{
    mock_hal_open(&g_hal, "camera_service");

    service_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name,        "camera_service",  sizeof(e.name) - 1);
    strncpy(e.socket_path, "/tmp/camera.sock", sizeof(e.socket_path) - 1);
    e.pid    = 1003;
    e.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&e);

    /* Simulate HAL read failure → service decides to stop */
    g_hal.read_ret = -1;
    uint8_t buf[64];
    int r = mock_hal_read(&g_hal, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);

    /* Service marks itself stopped in SM */
    sm_registry_update_status("camera_service", SERVICE_STATUS_STOPPED);

    service_entry_t out;
    sm_registry_find_copy("camera_service", &out);
    TEST_ASSERT_EQUAL_INT(SERVICE_STATUS_STOPPED, (int)out.status);
}

/* ── Scenario 4: Multi-service HAL + SM concurrent registration ──── */

#define N_HAL_SERVICES 8

typedef struct {
    int           id;
    mock_hal_state_t hal;
} hal_svc_arg_t;

static void *concurrent_hal_register(void *arg)
{
    hal_svc_arg_t *a = (hal_svc_arg_t *)arg;
    char name[64]; snprintf(name, sizeof(name), "hal_svc_%d", a->id);

    mock_hal_reset(&a->hal);
    a->hal.open_ret = HAL_SUCCESS;
    mock_hal_open(&a->hal, name);

    service_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, name, sizeof(e.name) - 1);
    snprintf(e.socket_path, sizeof(e.socket_path), "/tmp/hal%d.sock", a->id);
    e.pid    = (pid_t)(2000 + a->id);
    e.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&e);

    mock_hal_close(&a->hal);
    return NULL;
}

TEST(SM_HAL_Integration, ConcurrentHALServices_AllRegistered)
{
    pthread_t threads[N_HAL_SERVICES];
    hal_svc_arg_t args[N_HAL_SERVICES];

    for (int i = 0; i < N_HAL_SERVICES; i++) {
        args[i].id = i;
        pthread_create(&threads[i], NULL, concurrent_hal_register, &args[i]);
    }
    for (int i = 0; i < N_HAL_SERVICES; i++) pthread_join(threads[i], NULL);

    TEST_ASSERT_EQUAL_INT(N_HAL_SERVICES, sm_registry_count());
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(SM_HAL_Integration)
{
    RUN_TEST_CASE(SM_HAL_Integration, OpenHAL_ThenRegister_EntryPresent);
    RUN_TEST_CASE(SM_HAL_Integration, CloseHAL_ThenDeregister_EntryGone);
    RUN_TEST_CASE(SM_HAL_Integration, HAL_ReadError_UpdateSMStatus_Stopped);
    RUN_TEST_CASE(SM_HAL_Integration, ConcurrentHALServices_AllRegistered);
}

static void run_all_groups(void) { RUN_TEST_GROUP(SM_HAL_Integration); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
