#define _POSIX_C_SOURCE 200809L

/*
 * sm_management.c - Simple HTTP management API
 *
 * Note: In production, use a proper HTTP library (libmicrohttpd, etc)
 * This is a minimal implementation for demonstration.
 */

#include "sm_management.h"
#include "sm_logging.h"
#include "sm_metrics.h"
#include "sm_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int mgmt_fd = -1;
static int mgmt_running = 0;

static void* management_worker(void* arg)
{
    (void)arg;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(9999);  /* default port */
    
    mgmt_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (mgmt_fd < 0) {
        sm_log(SM_LOG_ERROR, "management: socket failed");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(mgmt_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(mgmt_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        sm_log(SM_LOG_ERROR, "management: bind failed");
        close(mgmt_fd);
        mgmt_fd = -1;
        return NULL;
    }
    
    if (listen(mgmt_fd, 5) < 0) {
        sm_log(SM_LOG_ERROR, "management: listen failed");
        close(mgmt_fd);
        mgmt_fd = -1;
        return NULL;
    }
    
    sm_log(SM_LOG_INFO, "management: listening on port 9999");
    
    /* Accept management connections */
    while (mgmt_running) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        
        int client_fd = accept(mgmt_fd, (struct sockaddr*)&client, &clen);
        if (client_fd < 0) continue;
        
        char buf[1024];
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            
            /* Simple HTTP response */
            const char* response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 25\r\n"
                "\r\n"
                "{\"status\": \"running\"}";
            
            send(client_fd, response, strlen(response), 0);
        }
        
        close(client_fd);
    }
    
    return NULL;
}

int sm_management_start(int port)
{
    pthread_t tid;
    
    if (mgmt_running) return -1;  /* already running */
    
    mgmt_running = 1;
    
    if (pthread_create(&tid, NULL, management_worker, NULL) != 0) {
        mgmt_running = 0;
        return -1;
    }
    
    pthread_detach(tid);
    
    sm_log(SM_LOG_INFO, "management: API started on port %d", port);
    return 0;
}

void sm_management_stop(void)
{
    mgmt_running = 0;
    
    if (mgmt_fd >= 0) {
        close(mgmt_fd);
        mgmt_fd = -1;
    }
    
    sm_log(SM_LOG_INFO, "management: API stopped");
}
