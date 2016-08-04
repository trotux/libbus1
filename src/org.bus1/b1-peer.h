#pragma once

/*
 * This file is part of bus1. See COPYING for details.
 *
 * bus1 is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

/*
 * XXX
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct B1Handle B1Handle;
typedef struct B1Message B1Message;
typedef struct B1Node B1Node;
typedef struct B1Peer B1Peer;

/* peers */

int b1_peer_new(B1Peer **peerp);
int b1_peer_new_from_fd(B1Peer **peerp, int fd);
B1Peer *b1_peer_ref(B1Peer *peer);
B1Peer *b1_peer_unref(B1Peer *peer);

int b1_peer_get_fd(B1Peer *peer);

int b1_peer_recv(B1Peer *peer, B1Message **messagep);

int b1_peer_set_seed(B1Peer *peer, B1Message *seed);
int b1_peer_get_seed(B1Peer *peer, B1Message **seedp);

/* messages */

int b1_message_new(B1Peer *peer, B1Message **messagep);
B1Message *b1_message_ref(B1Message *message);
B1Message *b1_message_unref(B1Message *message);

int b1_message_set_payload(B1Message *message, void *payload, size_t size);
int b1_message_set_handles(B1Message *message, B1Handle **handles, size_t n_handles);
int b1_message_set_fds(B1Message *message, int *fds, size_t n_fds);

int b1_message_send(B1Message *message, B1Handle **dests, size_t n_dests);

uid_t b1_message_get_uid(B1Message *message);
gid_t b1_message_get_gid(B1Message *message);
pid_t b1_message_get_pid(B1Message *message);
pid_t b1_message_get_tid(B1Message *message);

unsigned int b1_message_get_type(B1Message *message);
B1Node *b1_message_get_destination_node(B1Message *message);
B1Handle *b1_message_get_destination_handle(B1Message *message);

int b1_message_get_payload(B1Message *message, void **payloadp, size_t *sizep);
int b1_message_get_handle(B1Message *message, unsigned int index, B1Handle **handlep);
int b1_message_get_fd(B1Message *message, unsigned int index, int *fdp);

/* nodes */

int b1_node_new(B1Peer *peer, B1Node **nodep);
B1Node *b1_node_free(B1Node *node);

B1Peer *b1_node_get_peer(B1Node *node);
B1Handle *b1_node_get_handle(B1Node *node);

void b1_node_destroy(B1Node *node);

/* handles */

B1Handle *b1_handle_ref(B1Handle *handle);
B1Handle *b1_handle_unref(B1Handle *handle);

int b1_handle_transfer(B1Handle *src_handle, B1Peer *dst, B1Handle **dst_handlep);

B1Peer *b1_handle_get_peer(B1Handle *handle);

/* inline helpers */

static inline void b1_peer_unrefp(B1Peer **peer) {
        if (*peer)
                b1_peer_unref(*peer);
}

static inline void b1_message_unrefp(B1Message **message) {
        if (*message)
                b1_message_unref(*message);
}

static inline void b1_node_freep(B1Node **node) {
        if (*node)
                b1_node_free(*node);
}

static inline void b1_handle_unrefp(B1Handle **handle) {
        if (*handle)
                b1_handle_unref(*handle);
}

#ifdef __cplusplus
}
#endif
