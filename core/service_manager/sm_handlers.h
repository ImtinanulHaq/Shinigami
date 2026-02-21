#ifndef SM_HANDLERS_H
#define SM_HANDLERS_H

/*
 * sm_handlers.h - Message dispatch and per-type request handlers.
 */

#include "sm_protocol.h"
#include "sm_registry.h"

/*
 * Handle one complete client connection: recv header+payload, validate,
 * authenticate, dispatch, send reply, return.
 * The caller is responsible for closing client_fd after this returns.
 * Returns 0 on success, -1 on any error.
 */
int sm_handle_client(int client_fd);

/* Individual message handlers */
int sm_handle_register  (int fd, const sm_hdr_t* hdr, const sm_register_req_t*   req);
int sm_handle_lookup    (int fd, const sm_hdr_t* hdr, const sm_lookup_req_t*      req);
int sm_handle_heartbeat (int fd, const sm_hdr_t* hdr, const sm_heartbeat_req_t*  req);
int sm_handle_unregister(int fd, const sm_hdr_t* hdr, const sm_unregister_req_t* req);

#endif /* SM_HANDLERS_H */