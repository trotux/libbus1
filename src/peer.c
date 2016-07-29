/***
  This file is part of bus1. See COPYING for details.

  bus1 is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  bus1 is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with bus1; If not, see <http://www.gnu.org/licenses/>.
***/

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
 * @path:               the path to the bus1 character device, or NULL
 *
 * Create a new peer disconnected from all existing peers.
 *
 * Return: 0 on success, a negative error code on failure.
 */
_c_public_ int b1_peer_new(B1Peer **peerp, const char *path) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        int r;

        peer = calloc(1, sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->n_ref = 1;

        r = bus1_client_new_from_path(&peer->client, path);
        if (r < 0)
                return r;

        r = bus1_client_mmap(peer->client);
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
 * This takes a pre-initialized bus1 filedescriptor and wrapps creates a b1_peer
 * object around it.
 *
 * Return: 0 on success, a negative error code on failure.
 */
_c_public_ int b1_peer_new_from_fd(B1Peer **peerp, int fd) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        int r;

        peer = calloc(1, sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->n_ref = 1;

        r = bus1_client_new_from_fd(&peer->client, fd);
        if (r < 0)
                return r;

        r = bus1_client_mmap(peer->client);
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
        if (!peer)
                return NULL;

        assert(peer->n_ref > 0);

        ++peer->n_ref;

        return peer;
}

/**
 * b1_peer_unref() - release reference
 * @peer:               peer to release reference to, or NULL
 *
 * Return: NULL is returned.
 */
_c_public_ B1Peer *b1_peer_unref(B1Peer *peer) {
        if (!peer)
                return NULL;

        assert(peer->n_ref > 0);

        if (--peer->n_ref > 0)
                return NULL;

        assert(!c_rbtree_first(&peer->handles));
        assert(!c_rbtree_first(&peer->nodes));
        bus1_client_free(peer->client);
        free(peer);

        return NULL;
}

/**
 * b1_peer_get_fd() - get file descriptor representing peer in the kernel
 * @peer:               the peer
 *
 * Return: the file descriptor.
 */
_c_public_ int b1_peer_get_fd(B1Peer *peer) {
        return bus1_client_get_fd(peer->client);
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

        r = bus1_client_recv(peer->client, &recv);
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
                                         bus1_client_slice_from_offset(peer->client, recv.msg.offset),
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

        r = bus1_client_recv(peer->client, &recv);
        if (r < 0)
                return r;

        if (recv.msg.type != BUS1_MSG_DATA)
                return -EIO;

        if (recv.n_dropped)
                return -ENOBUFS;

        return b1_message_new_from_slice(peer,
                                         seedp,
                                         bus1_client_slice_from_offset(peer->client, recv.msg.offset),
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

/**
 * b1_handle_transfer() - transfer a handle from one peer to another
 * @src_handle:         source handle
 * @dst:                destination peer
 * @dst_handlep:        pointer to destination handle
 *
 * In order for peers to communicate, they must be reachable from one another.
 * This transfers a handle from one peer to another.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_handle_transfer(B1Handle *src_handle, B1Peer *dst, B1Handle **dst_handlep) {
        _c_cleanup_(b1_handle_unrefp) B1Handle *dst_handle = NULL;
        uint64_t src_handle_id, dst_handle_id = BUS1_HANDLE_INVALID;
        int r;

        if (src_handle->id == BUS1_HANDLE_INVALID)
                src_handle_id = BUS1_NODE_FLAG_MANAGED | BUS1_NODE_FLAG_ALLOCATE;
        else
                src_handle_id = src_handle->id;

        r = bus1_client_handle_transfer(src_handle->holder->client, dst->client, &src_handle_id, &dst_handle_id);
        if (r < 0)
                return r;

        if (src_handle->id == BUS1_HANDLE_INVALID) {
                r = b1_handle_link(src_handle, src_handle_id);
                if (r < 0)
                        return r;

                if (src_handle->node) {
                        r = b1_node_link(src_handle->node, src_handle_id);
                        if (r < 0)
                                return r;
                }
        }

        r = b1_handle_acquire(dst, &dst_handle, dst_handle_id);
        if (r < 0)
                return r;

        *dst_handlep = dst_handle;
        dst_handle = NULL;

        return 0;
}
