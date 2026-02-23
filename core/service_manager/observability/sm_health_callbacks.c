#define _POSIX_C_SOURCE 200809L

/*
 * sm_health_callbacks.c - Custom health check registration
 */

#include "../observability/sm_health_callbacks.h"
#include "../observability/sm_logging.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_CALLBACKS 32

static struct {
    char service_name[64];
    sm_health_check_fn fn;
} g_callbacks[MAX_CALLBACKS];

static int g_callback_count = 0;
static pthread_mutex_t cb_mutex = PTHREAD_MUTEX_INITIALIZER;

int sm_health_callback_register(const char* service_name, sm_health_check_fn fn)
{
    if (!service_name || !fn) return -1;
    
    pthread_mutex_lock(&cb_mutex);
    
    if (g_callback_count >= MAX_CALLBACKS) {
        pthread_mutex_unlock(&cb_mutex);
        return -1;
    }
    
    strncpy(g_callbacks[g_callback_count].service_name, service_name, 63);
    g_callbacks[g_callback_count].fn = fn;
    g_callback_count++;
    
    pthread_mutex_unlock(&cb_mutex);
    
    sm_log(SM_LOG_INFO, "health: registered callback for '%s'", service_name);
    return 0;
}

int sm_health_callback_check(const char* service_name)
{
    if (!service_name) return -1;
    
    pthread_mutex_lock(&cb_mutex);
    
    /* Find the callback and copy the function pointer to avoid holding the lock during execution */
    sm_health_check_fn fn = NULL;
    for (int i = 0; i < g_callback_count; i++) {
        if (!strcmp(g_callbacks[i].service_name, service_name)) {
            fn = g_callbacks[i].fn;
            break;
        }
    }
    
    pthread_mutex_unlock(&cb_mutex);
    
    /* Execute the callback outside the lock to prevent deadlock */
    if (fn) {
        return fn(service_name);
    }
    
    return 0;  /* no callback, assume healthy */
}

void sm_health_callbacks_check_all(void)
{
    /* FIX: Pehle saari callbacks copy karo lock ke andar
     * Phir lock chhoro, phir call karo
     * Warna: callback khud register() call kare toh deadlock! */

    sm_health_check_fn fns[MAX_CALLBACKS];
    char names[MAX_CALLBACKS][64];
    int count = 0;

    /* Step 1: Lock ke andar sirf copy karo */
    pthread_mutex_lock(&cb_mutex);
    count = g_callback_count;
    for (int i = 0; i < count; i++) {
        fns[i] = g_callbacks[i].fn;
        strncpy(names[i], g_callbacks[i].service_name, 63);
        names[i][63] = '\0';
    }
    pthread_mutex_unlock(&cb_mutex);
    /* Step 2: Lock chhor di — ab koi bhi register/unregister kar sakta hai */

    /* Step 3: Lock ke bahar callbacks call karo */
    for (int i = 0; i < count; i++) {
        if (fns[i]) {
            int result = fns[i](names[i]);
            if (result < 0) {
                sm_log(SM_LOG_WARN, "health: callback failed for '%s'", names[i]);
            }
        }
    }
}

int sm_health_callback_unregister(const char* service_name)
{
    if (!service_name) return -1;
    
    pthread_mutex_lock(&cb_mutex);
    
    for (int i = 0; i < g_callback_count; i++) {
        if (!strcmp(g_callbacks[i].service_name, service_name)) {
            for (int j = i; j < g_callback_count - 1; j++) {
                g_callbacks[j] = g_callbacks[j + 1];
            }
            g_callback_count--;
            pthread_mutex_unlock(&cb_mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&cb_mutex);
    return -1;
}

void sm_health_callbacks_cleanup(void)
{
    pthread_mutex_lock(&cb_mutex);
    memset(g_callbacks, 0, sizeof(g_callbacks));
    g_callback_count = 0;
    pthread_mutex_unlock(&cb_mutex);
}