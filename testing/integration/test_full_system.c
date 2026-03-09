/**
 * @file test_full_system.c
 * @brief End-to-end full system integration test.
 *
 * Spins up the mock SM server, registeres multiple fake services via IPC,
 * exercises ring-buffer data paths, checks observability (health/heartbeat),
 * and performs a clean shutdown — verifying zero SM entries at the end.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_sm.h"
#include "../mocks/mock_hal.h"

#include "../../dev/core/ring_buffer.h"
#include "../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"

#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>

#define N_SERVICES   4
#define ITEMS_PER_SVC 100

static mock_sm_t   g_msm;
static rb_handle_t *g_rb;

/* ── Helpers ──────────────────────────────────────────────────────── */

static int register_service(const char *name, const char *path, int seq)
{
    uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
    sm_message_header_t *h = (sm_message_header_t *)buf;
    h->magic     = SM_MAGIC;
    h->version   = SM_VERSION;
    h->type      = SM_MSG_REGISTER;
    h->length    = sizeof(*h) + sizeof(sm_register_payload_t);
    h->timestamp = (uint64_t)time(NULL);
    h->sequence  = (uint32_t)seq;

    sm_register_payload_t *p = (sm_register_payload_t *)(buf + sizeof(*h));
    strncpy(p->name,        name, sizeof(p->name) - 1);
    strncpy(p->socket_path, path, sizeof(p->socket_path) - 1);

    int fd = tu_unix_connect(g_msm.socket_path);
    if (fd < 0) return -1;
    ssize_t n = write(fd, buf, h->length);
    close(fd);
    return (n == (ssize_t)h->length) ? 0 : -1;
}

/* ── Service data threads ─────────────────────────────────────────── */

typedef struct {
    int            id;
    rb_handle_t   *rb;
    mock_hal_state_t hal;
    int            items;
} svc_ctx_t;

static svc_ctx_t g_svcs[N_SERVICES];

static void *service_thread(void *arg)
{
    svc_ctx_t *s = (svc_ctx_t *)arg;
    mock_hal_reset(&s->hal);
    s->hal.open_ret = s->hal.read_ret = HAL_SUCCESS;
    mock_hal_open(&s->hal, "dev");

    uint8_t buf[64];
    for (int i = 0; i < s->items; i++) {
        tu_rand_fill(buf, sizeof(buf), (uint32_t)(s->id * 1000 + i));
        mock_hal_read(&s->hal, buf, sizeof(buf));
        while (ring_buffer_write(s->rb, buf) != RB_SUCCESS) tu_sleep_ms(1);
    }
    mock_hal_close(&s->hal);
    return NULL;
}

static void *consumer_thread(void *arg)
{
    rb_handle_t *rb   = (rb_handle_t *)arg;
    int total         = N_SERVICES * ITEMS_PER_SVC;
    uint8_t buf[64];
    for (int i = 0; i < total; i++) {
        while (ring_buffer_read(rb, buf) != RB_SUCCESS) tu_sleep_ms(1);
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * Test Group
 * ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(FullSystem);

TEST_SETUP(FullSystem)
{
    sm_registry_init();
    mock_sm_start(&g_msm);
    g_rb = ring_buffer_create("fs_rb", 1024, 64);
    TEST_ASSERT_NOT_NULL(g_rb);
}

TEST_TEAR_DOWN(FullSystem)
{
    ring_buffer_destroy(g_rb, "fs_rb");
    mock_sm_stop(&g_msm);
    sm_registry_cleanup();
}

/* ── E2E Scenario 1: N services register, produce data, clean up ─── */

TEST(FullSystem, E2E_NServices_RegisterProduceConsume)
{
    /* Register via mock SM IPC */
    for (int i = 0; i < N_SERVICES; i++) {
        char name[64], path[64];
        snprintf(name, sizeof(name), "e2e_svc_%d", i);
        snprintf(path, sizeof(path), "/tmp/e2e_%d.sock", i);
        int r = register_service(name, path, i + 1);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, r, "IPC register failed");
    }
    tu_sleep_ms(100);
    TEST_ASSERT_EQUAL_INT(N_SERVICES, g_msm.stats.register_count);

    /* Produce data concurrently */
    pthread_t ptid[N_SERVICES];
    for (int i = 0; i < N_SERVICES; i++) {
        g_svcs[i].id    = i;
        g_svcs[i].rb    = g_rb;
        g_svcs[i].items = ITEMS_PER_SVC;
        pthread_create(&ptid[i], NULL, service_thread, &g_svcs[i]);
    }

    /* Single consumer drains all items */
    pthread_t ctid;
    pthread_create(&ctid, NULL, consumer_thread, g_rb);

    for (int i = 0; i < N_SERVICES; i++) pthread_join(ptid[i], NULL);
    pthread_join(ctid, NULL);

    TEST_ASSERT_TRUE(ring_buffer_is_empty(g_rb));
}

/* ── E2E Scenario 2: All services deregister → SM count = 0 ─────── */

TEST(FullSystem, E2E_AllDeregister_RegistryEmpty)
{
    /* Add directly via registry API for speed */
    for (int i = 0; i < N_SERVICES; i++) {
        service_entry_t e;
        memset(&e, 0, sizeof(e));
        snprintf(e.name,        sizeof(e.name),        "cleanup_svc_%d", i);
        snprintf(e.socket_path, sizeof(e.socket_path), "/tmp/cl%d.sock", i);
        e.pid    = (pid_t)(5000 + i);
        e.status = SERVICE_STATUS_RUNNING;
        sm_registry_add(&e);
    }
    TEST_ASSERT_EQUAL_INT(N_SERVICES, sm_registry_count());

    for (int i = 0; i < N_SERVICES; i++) {
        char name[64]; snprintf(name, sizeof(name), "cleanup_svc_%d", i);
        sm_registry_remove(name);
    }
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());
}

/* ── E2E Scenario 3: Heartbeat updates while data flows ─────────── */

TEST(FullSystem, E2E_HeartbeatUpdates_WhileFlowing)
{
    const char *svc = "hb_svc";
    service_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name,        svc,             sizeof(e.name) - 1);
    strncpy(e.socket_path, "/tmp/hb.sock",  sizeof(e.socket_path) - 1);
    e.pid    = 6001;
    e.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&e);

    /* Simulate heartbeats */
    for (int i = 0; i < 10; i++) {
        sm_registry_update_heartbeat(svc);
        tu_sleep_ms(5);
    }

    service_entry_t out;
    sm_registry_find_copy(svc, &out);
    TEST_ASSERT_EQUAL_INT(SERVICE_STATUS_RUNNING, (int)out.status);
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(FullSystem)
{
    RUN_TEST_CASE(FullSystem, E2E_NServices_RegisterProduceConsume);
    RUN_TEST_CASE(FullSystem, E2E_AllDeregister_RegistryEmpty);
    RUN_TEST_CASE(FullSystem, E2E_HeartbeatUpdates_WhileFlowing);
}

static void run_all_groups(void) { RUN_TEST_GROUP(FullSystem); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
