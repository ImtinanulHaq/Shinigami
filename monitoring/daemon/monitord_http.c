/**
 * @file    monitord_http.c
 * @brief   HTTP server implementation (minimal HTTP/1.1).
 */
#define _POSIX_C_SOURCE 200809L
#include "monitord_http.h"
#include "../metrics/metrics_export_prometheus.h"
#include "../health/health_score.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

struct monitord_http {
    int               listen_fd;
    pthread_t         accept_thread;
    volatile int      stop_flag;
    monitord_config_t *config;
    monitord_state_t  *state;
};

static void handle_request(int client_fd, monitord_state_t *state)
{
    char req[4096];
    ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    /* Parse request line */
    char method[16], path[256];
    if (sscanf(req, "%15s %255s", method, path) != 2) {
        close(client_fd);
        return;
    }

    /* Handle /metrics */
    if (strcmp(path, "/metrics") == 0) {
        mon_snapshot_t snapshot;
        monitord_state_serialize_snapshot(state, &snapshot);

        char prom_buf[65536];
        uint32_t prom_len = prometheus_render(&snapshot, prom_buf, sizeof(prom_buf));

        char response[65536 + 512];
        int len = snprintf(response, sizeof(response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain; version=0.0.4\r\n"
                           "Content-Length: %u\r\n"
                           "Connection: close\r\n"
                           "\r\n%s",
                           prom_len, prom_buf);
        send(client_fd, response, (size_t)len, 0);
    }
    /* Handle /health */
    else if (strcmp(path, "/health") == 0) {
        int score = health_compute_system_score(state->services, SERVICE_MAX);
        const char *status = (score >= 70) ? "ok" : "degraded";

        char json[512];
        int json_len = snprintf(json, sizeof(json),
                                "{\"status\":\"%s\",\"score\":%d}",
                                status, score);

        char response[1024];
        int len = snprintf(response, sizeof(response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n%s",
                           json_len, json);
        send(client_fd, response, (size_t)len, 0);
    }
    /* 404 */
    else {
        const char *response = "HTTP/1.1 404 Not Found\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    }

    close(client_fd);
}

static void *accept_thread_fn(void *arg)
{
    monitord_http_t *http = (monitord_http_t *)arg;

    while (!http->stop_flag) {
        int client_fd = accept(http->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("accept (http)");
            break;
        }

        /* Handle request inline (simple, single-threaded) */
        handle_request(client_fd, http->state);
    }

    return NULL;
}

monitord_http_t *monitord_http_start(monitord_config_t *config,
                                      monitord_state_t *state)
{
    monitord_http_t *http = calloc(1, sizeof(monitord_http_t));
    if (!http) return NULL;

    http->config = config;
    http->state = state;

    /* Create TCP socket */
    http->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (http->listen_fd < 0) {
        perror("socket (http)");
        free(http);
        return NULL;
    }

    int opt = 1;
    setsockopt(http->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->http_port);
    inet_pton(AF_INET, config->http_bind_addr, &addr.sin_addr);

    if (bind(http->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind (http)");
        close(http->listen_fd);
        free(http);
        return NULL;
    }

    if (listen(http->listen_fd, 8) < 0) {
        perror("listen (http)");
        close(http->listen_fd);
        free(http);
        return NULL;
    }

    /* Start accept thread */
    if (pthread_create(&http->accept_thread, NULL, accept_thread_fn, http) != 0) {
        perror("pthread_create (http)");
        close(http->listen_fd);
        free(http);
        return NULL;
    }

    return http;
}

void monitord_http_stop(monitord_http_t *http)
{
    if (!http) return;

    http->stop_flag = 1;
    shutdown(http->listen_fd, SHUT_RDWR);
    pthread_join(http->accept_thread, NULL);
    close(http->listen_fd);
    free(http);
}
