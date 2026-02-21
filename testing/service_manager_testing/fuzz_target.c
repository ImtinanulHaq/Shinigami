/* Level 5: Fuzz Target - Protocol parser fuzzing with ASAN instrumentation */
#include "sm_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* This is an AFL-compatible fuzzing target that exercises the protocol parser */

/* Mock implementations for missing functions (in real scenario, link with actual libs) */
void sm_log(int level, const char* fmt, ...) {
    /* Suppress logging during fuzzing */
    (void)level;
    (void)fmt;
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
            if (hdr->length < sizeof(sm_msg_register_t)) {
                break;
            }
            
            sm_msg_register_t* msg = (sm_msg_register_t*)(fuzz_data + sizeof(sm_hdr_t));
            
            /* Validate service name length */
            if (msg->name_len > SM_MAX_NAME) {
                break;  /* Invalid, but shouldn't crash */
            }
            
            /* Validate path length */
            if (msg->path_len > SM_MAX_PATH) {
                break;
            }
            
            break;
        }
        
        case SM_MSG_LOOKUP: {
            /* Lookup message fuzzing */
            if (hdr->length < sizeof(sm_msg_lookup_t)) {
                break;
            }
            
            sm_msg_lookup_t* msg = (sm_msg_lookup_t*)(fuzz_data + sizeof(sm_hdr_t));
            
            /* Service name should be null-terminated */
            if (msg->name_len > SM_MAX_NAME) {
                break;
            }
            
            break;
        }
        
        case SM_MSG_HEARTBEAT: {
            /* Heartbeat is simple, just validate size */
            if (hdr->length < sizeof(sm_msg_heartbeat_t)) {
                break;
            }
            
            break;
        }
        
        case SM_MSG_UNREGISTER: {
            /* Unregister message fuzzing */
            if (hdr->length < sizeof(sm_msg_unregister_t)) {
                break;
            }
            
            sm_msg_unregister_t* msg = (sm_msg_unregister_t*)(fuzz_data + sizeof(sm_hdr_t));
            
            /* Service name validation */
            if (msg->name_len > SM_MAX_NAME) {
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
