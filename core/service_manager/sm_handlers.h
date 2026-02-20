#ifndef SM_HANDLERS_H
#define SM_HANDLERS_H

#include "sm_protocol.h"
#include "sm_registry.h"

// ── MESSAGE HANDLERS ───────────────────────────────────────────────────────────

// Handle client connection and dispatch message
// Returns: 0 on success, < 0 on error
int  sm_handle_client(int client_fd);

// Individual handlers
int  sm_handle_register(int fd, const sm_hdr_t* hdr, const sm_register_req_t* req);
int  sm_handle_lookup(int fd, const sm_hdr_t* hdr, const sm_lookup_req_t* req);
int  sm_handle_heartbeat(int fd, const sm_hdr_t* hdr, const sm_heartbeat_req_t* req);
int  sm_handle_unregister(int fd, const sm_hdr_t* hdr, const sm_unregister_req_t* req);

#endif // SM_HANDLERS_H
