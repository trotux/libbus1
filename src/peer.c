/*
 * Copyright (C) 2013-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 */

#include <assert.h>
#include <c-macro.h>
#include <c-rbtree.h>
#include <errno.h>
#include "message.h"
#include "node.h"
#include "peer.h"
#include <stdlib.h>
#include <string.h>

/**
 * b1_peer_new() - creates a new disconnected peer
 * @peerp:              the new peer object
 *
 * Create a new peer disconnected from all existing peers.
 *
 * Return: 0 on success, a negative error code on failure.
 */
_c_public_ int b1_peer_new(B1Peer **peerp) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        int r;

        peer = calloc(1, sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->ref = C_REF_INIT;

        r = bus1_peer_new_from_path(&peer->peer, NULL);
        if (r < 0)
                return r;

        r = bus1_peer_mmap(peer->peer);
        if (r < 0)
                return r;

        *peerp = peer;
        peer = NULL;

        return 0;
}

/**
 * b1_peer_new_from_fd() - create new peer object from existing fd
 * @peerp:              the new peer object
 * @fd:                 a file descriptor representing an existing peer
 *
 * This takes a pre-initialized bus1 filedescriptor and creates a b1_peer object
 * around it.
 *
 * Return: 0 on success, a negative error code on failure.
 */
_c_public_ int b1_peer_new_from_fd(B1Peer **peerp, int fd) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        int r;

        peer = calloc(1, sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->ref = C_REF_INIT;

        r = bus1_peer_new_from_fd(&peer->peer, fd);
        if (r < 0)
                return r;

        r = bus1_peer_mmap(peer->peer);
        if (r < 0)
                return r;

        *peerp = peer;
        peer = NULL;

        return 0;
}

/**
 * b1_peer_ref() - acquire reference
 * @peer:               peer to acquire reference to, or NULL
 *
 * Return: @peer is returned.
 */
_c_public_ B1Peer *b1_peer_ref(B1Peer *peer) {
        if (peer)
                c_ref_inc(&peer->ref);

        return peer;
}

static void b1_peer_free(_Atomic unsigned long *ref, void *userdata) {
        B1Peer *peer = userdata;

        assert(!c_rbtree_first(&peer->handles));
        assert(!c_rbtree_first(&peer->nodes));
        bus1_peer_free(peer->peer);
        free(peer);
}

/**
 * b1_peer_unref() - release reference
 * @peer:               peer to release reference to, or NULL
 *
 * Return: NULL is returned.
 */
_c_public_ B1Peer *b1_peer_unref(B1Peer *peer) {
        if (peer)
                c_ref_dec(&peer->ref, b1_peer_free, peer);

        return NULL;
}

/**
 * b1_peer_get_fd() - get file descriptor representing peer in the kernel
 * @peer:               the peer
 *
 * Return: the file descriptor.
 */
_c_public_ int b1_peer_get_fd(B1Peer *peer) {
        return bus1_peer_get_fd(peer->peer);
}

/*
 * b1_peer_recv() - receive one message
 * @peer:               the receiving peer
 * @messagep:           the received message
 *
 * Dequeues one message from the queue if available and returns it.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_peer_recv(B1Peer *peer, B1Message **messagep) {
        struct bus1_cmd_recv recv = {};
        int r;

        assert(peer);

        r = bus1_peer_recv(peer->peer, &recv);
        if (r < 0)
                return r;

        if (recv.n_dropped)
                return -ENOBUFS;

        if (recv.msg.type != BUS1_MSG_DATA &&
            recv.msg.type != BUS1_MSG_NODE_DESTROY &&
            recv.msg.type != BUS1_MSG_NODE_RELEASE)
                return -EIO;

        return b1_message_new_from_slice(peer,
                                         messagep,
                                         bus1_peer_slice_from_offset(peer->peer, recv.msg.offset),
                                         recv.msg.type,
                                         recv.msg.destination,
                                         recv.msg.uid,
                                         recv.msg.gid,
                                         recv.msg.pid,
                                         recv.msg.tid,
                                         recv.msg.n_bytes,
                                         recv.msg.n_handles,
                                         recv.msg.n_fds);

        return -EIO;
}

/**
 * b1_peer_get_seed() - receive the seed message
 * @peer:               the receiving peer
 * @messagep:           the received seed
 *
 * Receives the seed message if available and returns it.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_peer_get_seed(B1Peer *peer, B1Message **seedp) {
        struct bus1_cmd_recv recv = {
                .flags = BUS1_RECV_FLAG_SEED,
        };
        int r;

        r = bus1_peer_recv(peer->peer, &recv);
        if (r < 0)
                return r;

        if (recv.msg.type != BUS1_MSG_DATA)
                return -EIO;

        if (recv.n_dropped)
                return -ENOBUFS;

        return b1_message_new_from_slice(peer,
                                         seedp,
                                         bus1_peer_slice_from_offset(peer->peer, recv.msg.offset),
                                         recv.msg.type,
                                         recv.msg.destination,
                                         recv.msg.uid,
                                         recv.msg.gid,
                                         recv.msg.pid,
                                         recv.msg.tid,
                                         recv.msg.n_bytes,
                                         recv.msg.n_handles,
                                         recv.msg.n_fds);
}
