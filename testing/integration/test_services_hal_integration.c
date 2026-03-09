/**
 * @file test_services_hal_integration.c
 * @brief Integration: service-layer HAL abstraction + mock HAL.
 *
 * Tests the bridge between a logical service (consumer) and the HAL
 * abstraction layer (mock_hal). Verifies open → read → status-update →
 * close lifecycle from the service's perspective.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hal.h"

#include "../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>
#include <pthread.h>
#include <stdlib.h>

/* Simulated service context */
typedef struct {
    mock_hal_state_t hal;
    service_entry_t  entry;
    int              running;
    int              frames_read;
    pthread_t        thread;
} fake_service_t;

static fake_service_t g_svc;

static void fake_service_init(fake_service_t *s, const char *name,
                               const char *sock, pid_t pid)
{
    mock_hal_reset(&s->hal);
    s->hal.open_ret  = HAL_SUCCESS;
    s->hal.close_ret = HAL_SUCCESS;
    s->hal.read_ret  = HAL_SUCCESS;

    memset(&s->entry, 0, sizeof(s->entry));
    strncpy(s->entry.name,        name, sizeof(s->entry.name) - 1);
    strncpy(s->entry.socket_path, sock, sizeof(s->entry.socket_path) - 1);
    s->entry.pid    = pid;
    s->entry.status = SERVICE_STATUS_STOPPED;
    s->running      = 0;
    s->frames_read  = 0;
}

/* Service data-loop thread */
static void *service_loop(void *arg)
{
    fake_service_t *s = (fake_service_t *)arg;
    uint8_t buf[128];
    while (s->running) {
        int r = mock_hal_read(&s->hal, buf, sizeof(buf));
        if (r == HAL_SUCCESS) {
            s->frames_read++;
            sm_registry_update_heartbeat(s->entry.name);
        } else {
            s->running = 0;
            sm_registry_update_status(s->entry.name, SERVICE_STATUS_STOPPED);
            break;
        }
        tu_sleep_ms(5);
    }
    return NULL;
}

TEST_GROUP(Services_HAL_Integration);

TEST_SETUP(Services_HAL_Integration)
{
    sm_registry_init();
    fake_service_init(&g_svc, "data_svc", "/tmp/data.sock", 3001);
}

TEST_TEAR_DOWN(Services_HAL_Integration)
{
    g_svc.running = 0;
    sm_registry_cleanup();
}

/* ── Scenario 1: Service open HAL → register → loop → close ──────── */

TEST(Services_HAL_Integration, ServiceLifecycle_OpenRunClose)
{
    /* Open HAL */
    int r = mock_hal_open(&g_svc.hal, g_svc.entry.name);
    TEST_ASSERT_EQUAL_INT(HAL_SUCCESS, r);

    /* Register in SM */
    g_svc.entry.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&g_svc.entry);

    /* Run service loop for a short time */
    g_svc.running = 1;
    pthread_create(&g_svc.thread, NULL, service_loop, &g_svc);
    tu_sleep_ms(60);
    g_svc.running = 0;
    pthread_join(g_svc.thread, NULL);

    TEST_ASSERT_TRUE(g_svc.hal.read_count > 0);

    /* Close HAL */
    mock_hal_close(&g_svc.hal);
    TEST_ASSERT_EQUAL_INT(1, g_svc.hal.close_count);

    /* Cleanup SM */
    sm_registry_remove(g_svc.entry.name);
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());
}

/* ── Scenario 2: HAL error mid-run → service auto-stop in SM ─────── */

TEST(Services_HAL_Integration, HAL_ErrorMidRun_ServiceAutoStops)
{
    mock_hal_open(&g_svc.hal, g_svc.entry.name);
    g_svc.entry.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&g_svc.entry);

    g_svc.running = 1;
    pthread_create(&g_svc.thread, NULL, service_loop, &g_svc);

    /* Inject failure after a brief delay */
    tu_sleep_ms(30);
    g_svc.hal.read_ret = -1;

    pthread_join(g_svc.thread, NULL);

    /* Service should have updated SM status to STOPPED */
    service_entry_t out;
    sm_registry_find_copy(g_svc.entry.name, &out);
    TEST_ASSERT_EQUAL_INT(SERVICE_STATUS_STOPPED, (int)out.status);
}

/* ── Scenario 3: Two services on the same device ─────────────────── */

TEST(Services_HAL_Integration, TwoServices_IndependentHALInstances)
{
    fake_service_t svc2;
    fake_service_init(&svc2, "data_svc_2", "/tmp/data2.sock", 3002);

    mock_hal_open(&g_svc.hal, g_svc.entry.name);
    mock_hal_open(&svc2.hal,  svc2.entry.name);

    g_svc.entry.status = SERVICE_STATUS_RUNNING;
    svc2.entry.status  = SERVICE_STATUS_RUNNING;
    sm_registry_add(&g_svc.entry);
    sm_registry_add(&svc2.entry);

    TEST_ASSERT_EQUAL_INT(2, sm_registry_count());

    mock_hal_close(&g_svc.hal);
    sm_registry_remove(g_svc.entry.name);
    TEST_ASSERT_EQUAL_INT(1, sm_registry_count());

    mock_hal_close(&svc2.hal);
    sm_registry_remove(svc2.entry.name);
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(Services_HAL_Integration)
{
    RUN_TEST_CASE(Services_HAL_Integration, ServiceLifecycle_OpenRunClose);
    RUN_TEST_CASE(Services_HAL_Integration, HAL_ErrorMidRun_ServiceAutoStops);
    RUN_TEST_CASE(Services_HAL_Integration, TwoServices_IndependentHALInstances);
}

static void run_all_groups(void) { RUN_TEST_GROUP(Services_HAL_Integration); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
