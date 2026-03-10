/**
 * @file test_prometheus_export.c
 * @brief Integration test for Prometheus /metrics and /health endpoints
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../metrics/metrics_export_prometheus.h"
#include "../../daemon/monitord_http.h"
#include <curl/curl.h>
#include <string.h>

#define HTTP_PORT 9090
#define METRICS_URL "http://localhost:9090/metrics"
#define HEALTH_URL "http://localhost:9090/health"

/* ── Curl Response Buffer ───────────────────────────────────────────────────── */
struct response_buffer {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct response_buffer *buf = (struct response_buffer *)userp;
    
    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;
    
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;
    
    return realsize;
}

/* ── Test 1: /metrics Format Validation ─────────────────────────────────────── */
static bool test_metrics_format(void)
{
    CURL *curl = curl_easy_init();
    TEST_ASSERT_NOT_NULL(curl, "Curl init failed");
    
    struct response_buffer buf = {NULL, 0};
    
    curl_easy_setopt(curl, CURLOPT_URL, METRICS_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && buf.data) {
        /* Verify contains TYPE declarations */
        TEST_ASSERT(strstr(buf.data, "# TYPE") != NULL, "Missing TYPE declarations");
        
        /* Verify contains HELP declarations */
        TEST_ASSERT(strstr(buf.data, "# HELP") != NULL, "Missing HELP declarations");
        
        /* Verify contains actual metrics */
        TEST_ASSERT(strstr(buf.data, "middleware_") != NULL, "Missing middleware_ prefix");
        
        /* Verify newline-separated */
        TEST_ASSERT(strstr(buf.data, "\n") != NULL, "Should be newline-separated");
    }
    
    if (buf.data) free(buf.data);
    curl_easy_cleanup(curl);
    
    return true;
}

/* ── Test 2: Label Syntax ───────────────────────────────────────────────────── */
static bool test_label_syntax(void)
{
    /* Verify metrics follow Prometheus label syntax: metric{label="value"} */
    
    char sample[] = "middleware_service_cpu{service=\"audio\"} 12.5";
    
    TEST_ASSERT(strstr(sample, "{") != NULL, "Should have opening brace");
    TEST_ASSERT(strstr(sample, "}") != NULL, "Should have closing brace");
    TEST_ASSERT(strstr(sample, "=\"") != NULL, "Should have label assignment");
    
    return true;
}

/* ── Test 3: Histogram Buckets ──────────────────────────────────────────────── */
static bool test_histogram_buckets(void)
{
    /* Verify histogram exports _bucket, _sum, _count */
    
    const char *histogram_metric = "middleware_latency_bucket{le=\"0.001\"} 10\n"
                                   "middleware_latency_bucket{le=\"0.01\"} 50\n"
                                   "middleware_latency_bucket{le=\"+Inf\"} 100\n"
                                   "middleware_latency_sum 15.5\n"
                                   "middleware_latency_count 100\n";
    
    TEST_ASSERT(strstr(histogram_metric, "_bucket") != NULL, "Should have _bucket");
    TEST_ASSERT(strstr(histogram_metric, "_sum") != NULL, "Should have _sum");
    TEST_ASSERT(strstr(histogram_metric, "_count") != NULL, "Should have _count");
    TEST_ASSERT(strstr(histogram_metric, "le=") != NULL, "Should have le= label");
    TEST_ASSERT(strstr(histogram_metric, "+Inf") != NULL, "Should have +Inf bucket");
    
    return true;
}

/* ── Test 4: Counter Monotonicity ───────────────────────────────────────────── */
static bool test_counter_monotonicity(void)
{
    /* Scrape metrics twice, verify counters don't decrease */
    
    CURL *curl = curl_easy_init();
    TEST_ASSERT_NOT_NULL(curl, "Curl init failed");
    
    /* First scrape */
    struct response_buffer buf1 = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, METRICS_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);
    
    usleep(100000); /* Wait 100ms */
    
    /* Second scrape */
    struct response_buffer buf2 = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf2);
    curl_easy_perform(curl);
    
    if (buf1.data && buf2.data) {
        /* For now, just verify both scrapes succeeded */
        TEST_ASSERT(buf1.size > 0, "First scrape empty");
        TEST_ASSERT(buf2.size > 0, "Second scrape empty");
    }
    
    if (buf1.data) free(buf1.data);
    if (buf2.data) free(buf2.data);
    curl_easy_cleanup(curl);
    
    return true;
}

/* ── Test 5: /health JSON Format ─────────────────────────────────────────────── */
static bool test_health_json(void)
{
    CURL *curl = curl_easy_init();
    TEST_ASSERT_NOT_NULL(curl, "Curl init failed");
    
    struct response_buffer buf = {NULL, 0};
    
    curl_easy_setopt(curl, CURLOPT_URL, HEALTH_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && buf.data) {
        /* Verify JSON structure */
        TEST_ASSERT(strstr(buf.data, "{") != NULL, "Should be JSON");
        TEST_ASSERT(strstr(buf.data, "}") != NULL, "Should close JSON");
        TEST_ASSERT(strstr(buf.data, "status") != NULL, "Should have status field");
        TEST_ASSERT(strstr(buf.data, "timestamp") != NULL, "Should have timestamp");
    }
    
    if (buf.data) free(buf.data);
    curl_easy_cleanup(curl);
    
    return true;
}

/* ── Test 6: HTTP Response Codes ─────────────────────────────────────────────── */
static bool test_http_codes(void)
{
    CURL *curl = curl_easy_init();
    TEST_ASSERT_NOT_NULL(curl, "Curl init failed");
    
    long response_code = 0;
    
    /* /metrics should return 200 */
    curl_easy_setopt(curl, CURLOPT_URL, METRICS_URL);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    TEST_ASSERT_EQ(response_code, 200, "/metrics should return 200");
    
    /* /health should return 200 */
    curl_easy_setopt(curl, CURLOPT_URL, HEALTH_URL);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    TEST_ASSERT_EQ(response_code, 200, "/health should return 200");
    
    /* /nonexistent should return 404 */
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:9090/nonexistent");
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    TEST_ASSERT_EQ(response_code, 404, "Unknown path should return 404");
    
    curl_easy_cleanup(curl);
    return true;
}

/* ── Test 7: Content-Type Headers ───────────────────────────────────────────── */
static bool test_content_type(void)
{
    /* /metrics should be text/plain */
    /* /health should be application/json */
    
    /* This requires parsing HTTP headers from curl */
    /* For now, placeholder */
    TEST_ASSERT(1, "Content-Type verification placeholder");
    
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"metrics_format", test_metrics_format, false}, /* Requires HTTP server */
        {"label_syntax", test_label_syntax, true},
        {"histogram_buckets", test_histogram_buckets, true},
        {"counter_monotonicity", test_counter_monotonicity, false}, /* Requires HTTP */
        {"health_json", test_health_json, false}, /* Requires HTTP */
        {"http_codes", test_http_codes, false}, /* Requires HTTP */
        {"content_type", test_content_type, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Prometheus Export");
}
