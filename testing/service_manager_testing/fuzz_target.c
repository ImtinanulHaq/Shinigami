/* Level 5: Fuzz Target - Protocol parser fuzzing with ASAN instrumentation */
#include <core/service_manager/infrastructure/sm_protocol.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Mock implementations for functions we don't need to test                     */
/* ─────────────────────────────────────────────────────────────────────────── */

void sm_log(int level, const char* fmt, ...) {
    /* Suppress logging during fuzzing */
    (void)level;
    (void)fmt;
}

int sm_ratelimit_check_extended(const char* svc, uint16_t msg_type, int rl_id) {
    (void)svc;
    (void)msg_type;
    (void)rl_id;
    return 0;
}

void sm_audit_log(const char* action, const char* svc, int pid, int uid) {
    (void)action;
    (void)svc;
    (void)pid;
    (void)uid;
}

uint64_t sm_request_id_generate(void) {
    return 0;
}

void sm_request_id_set(uint64_t id) {
    (void)id;
}

void sm_request_id_clear(void) {
}

const char* sm_request_id_str(void) {
    return "mock-req-id";
}

/* Fuzz target entry point for AFL */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Ensure minimum header size */
    if (size < sizeof(sm_hdr_t)) {
        return 0;
    }
    
    /* Make a mutable copy */
    uint8_t* fuzz_data = malloc(size);
    if (!fuzz_data) {
        return 0;
    }
    memcpy(fuzz_data, data, size);
    
    /* Attempt to parse as sm_hdr_t */
    sm_hdr_t* hdr = (sm_hdr_t*)fuzz_data;
    
    /* Validate magic number */
    if (hdr->magic != SM_PROTOCOL_MAGIC) {
        free(fuzz_data);
        return 0;
    }
    
    /* Check protocol version */
    if (hdr->version != SM_PROTOCOL_VERSION) {
        free(fuzz_data);
        return 0;
    }
    
    /* Validate message type */
    if (hdr->type < SM_MSG_REGISTER || hdr->type > SM_MSG_UNREGISTER) {
        free(fuzz_data);
        return 0;
    }
    
    /* Check payload length doesn't exceed buffer */
    if (hdr->length > size - sizeof(sm_hdr_t)) {
        free(fuzz_data);
        return 0;
    }
    
    /* Fuzz different message types */
    switch (hdr->type) {
        case SM_MSG_REGISTER: {
            /* Register message payload fuzzing */
            if (hdr->length < sizeof(sm_register_req_t)) {
                break;
            }
            
            sm_register_req_t* msg = (sm_register_req_t*)(fuzz_data + sizeof(sm_hdr_t));
            
            /* Validate service name is present */
            if (msg->service_name[0] == '\0') {
                break;  /* Invalid, but shouldn't crash */
            }
            
            /* Validate path is present */
            if (msg->socket_path[0] == '\0') {
                break;
            }
            
            break;
        }
        
        case SM_MSG_LOOKUP: {
            /* Lookup message fuzzing */
            if (hdr->length < sizeof(sm_lookup_req_t)) {
                break;
            }
            
            sm_lookup_req_t* msg = (sm_lookup_req_t*)(fuzz_data + sizeof(sm_hdr_t));
            
            /* Service name should be present */
            if (msg->service_name[0] == '\0') {
                break;
            }
            
            break;
        }
        
        case SM_MSG_HEARTBEAT: {
            /* Heartbeat is simple, just validate size */
            if (hdr->length < sizeof(sm_heartbeat_req_t)) {
                break;
            }
            
            sm_heartbeat_req_t* msg = (sm_heartbeat_req_t*)(fuzz_data + sizeof(sm_hdr_t));
            if (msg->service_name[0] == '\0') {
                break;
            }
            
            break;
        }
        
        case SM_MSG_UNREGISTER: {
            /* Unregister message fuzzing */
            if (hdr->length < sizeof(sm_unregister_req_t)) {
                break;
            }
            
            sm_unregister_req_t* msg = (sm_unregister_req_t*)(fuzz_data + sizeof(sm_hdr_t));
            
            /* Service name validation */
            if (msg->service_name[0] == '\0') {
                break;
            }
            
            break;
        }
        
        default:
            break;
    }
    
    free(fuzz_data);
    return 0;
}

/* AFL-compatible main for standalone binary */
#ifdef AFL_MAIN
int main(int argc, char** argv) {
    (void)argc;   /* suppress unused parameter warning */
    (void)argv;   /* suppress unused parameter warning */
    
    #ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    #endif
    
    uint8_t buf[4096];
    ssize_t len;
    
    while ((len = read(0, buf, sizeof(buf))) > 0) {
        LLVMFuzzerTestOneInput(buf, len);
    }
    
    return 0;
}
#endif
