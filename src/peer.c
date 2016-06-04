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
#include <c-variant.h>
#include <errno.h>
#include "interface.h"
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

        assert(peerp);

        peer = calloc(1, sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->n_ref = 1;

        r = bus1_client_new_from_path(&peer->client, path);
        if (r < 0)
                return r;

        r = bus1_client_init(peer->client, BUS1_CLIENT_POOL_SIZE);
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

        assert(peerp);

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
        CRBNode *n;

        if (!peer)
                return NULL;

        assert(peer->n_ref > 0);

        if (--peer->n_ref > 0)
                return NULL;

        while ((n = c_rbtree_first(&peer->root_nodes))) {
                B1Node *node = c_container_of(n, B1Node, rb);

                c_rbtree_remove(&peer->root_nodes, n);
                b1_node_free(node);
        }

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
        assert(peer);

        return bus1_client_get_fd(peer->client);
}

static int b1_peer_recv_data(B1Peer *peer, struct bus1_msg_data *data, B1Message **messagep) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        const uint64_t *handle_ids;
        void *slice;
        unsigned int reply_handle;
        int r;

        assert(peer);
        assert(data);
        assert(messagep);

        slice = bus1_client_slice_from_offset(peer->client, data->offset);

        r = b1_message_new_from_slice(&message, peer, slice, data->n_bytes);
        if (r < 0)
                return r;

        message->data.destination = data->destination;
        message->data.uid = data->uid;
        message->data.gid = data->gid;
        message->data.pid = data->pid;
        message->data.tid = data->tid;
        handle_ids = (uint64_t*)((uint8_t*)slice + c_align_to(data->n_bytes, 8));
        message->data.fds = (int*)(handle_ids + data->n_handles);
        message->data.n_fds = data->n_fds;

        message->data.n_handles = 0;
        message->data.handles = calloc(data->n_handles, sizeof(*message->data.handles));
        if (!message->data.handles)
                return -ENOMEM;
        message->data.n_handles = data->n_handles;

        for (unsigned int i = 0; i < data->n_handles; i++) {
                r = b1_handle_acquire(&message->data.handles[i], peer, handle_ids[i]);
                if (r < 0)
                        return r;
        }

        r = c_variant_enter(message->data.cv, "(");
        if (r < 0)
                return r;

        r = c_variant_read(message->data.cv, "t", &message->type);
        if (r < 0)
                return r;

        switch (message->type) {
        case B1_MESSAGE_TYPE_CALL:
                r = c_variant_enter(message->data.cv, "v(");
                if (r < 0)
                        return r;

                r = c_variant_read(message->data.cv, "ss",
                                   &message->data.call.interface,
                                   &message->data.call.member);
                if (r < 0)
                        return r;

                r = c_variant_enter(message->data.cv, "m");
                if (r < 0)
                        return r;

                r = c_variant_peek_count(message->data.cv);
                if (r < 0)
                        return r;
                else if (r == 1) {
                        r = c_variant_read(message->data.cv, "u", &reply_handle);
                        if (r < 0)
                                return r;

                        if (data->n_handles <= reply_handle)
                                return -EIO;

                        message->data.call.reply_handle = message->data.handles[reply_handle];
                } else
                        message->data.call.reply_handle = NULL;

                r = c_variant_exit(message->data.cv, "m)v");

                break;

        case B1_MESSAGE_TYPE_REPLY:
                r = c_variant_read(message->data.cv, "v", "()");
                if (r < 0)
                        return r;

                break;

        case B1_MESSAGE_TYPE_ERROR:
                r = c_variant_read(message->data.cv, "v", "s", &message->data.error.name);
                if (r < 0)
                        return r;

                break;

        case B1_MESSAGE_TYPE_SEED:
                r = c_variant_enter(message->data.cv, "va");
                if (r < 0)
                        return r;

                r = c_variant_peek_count(message->data.cv);
                if (r < 0)
                        return r;

                message->data.seed.root_nodes = (CRBTree){};

                for (unsigned i = 0, n = r; i < n; i ++) {
                        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
                        const char *name;
                        unsigned int offset;
                        CRBNode **slot, *p;

                        r = c_variant_read(message->data.cv, "(su)", &name, &offset);
                        if (r < 0)
                                return r;

                        slot = c_rbtree_find_slot(&message->data.seed.root_nodes,
                                                  root_nodes_compare, name, &p);
                        if (!slot)
                                return -EIO;

                        r = b1_node_new_internal(peer, &node, NULL,
                                                 message->data.handles[offset]->id, name);
                        if (r < 0)
                                return r;

                        node->handle = b1_handle_ref(message->data.handles[offset]);

                        c_rbtree_add(&message->data.seed.root_nodes, p, slot, &node->rb);
                        node->owned = true;

                        node = NULL;
                }

                r = c_variant_exit(message->data.cv, "av");
                if (r < 0)
                        return r;

                break;

        default:
                return -EIO;
        }

        r = c_variant_enter(message->data.cv, "v");
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

static int b1_peer_recv_node_destroy(B1Peer *peer,
                                     struct bus1_msg_node_destroy *node_destroy,
                                     B1Message **messagep) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;

        message = calloc(1, sizeof(*message));
        if (!message)
                return -ENOMEM;

        message->type = B1_MESSAGE_TYPE_NODE_DESTROY;
        message->n_ref = 1;
        message->node_destroy.handle_id = node_destroy->handle;
        message->peer = b1_peer_ref(peer);

        *messagep = message;
        message = NULL;

        return 0;
}

/**
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

        switch (recv.type) {
                case BUS1_MSG_DATA:
                        return b1_peer_recv_data(peer, &recv.data, messagep);
                case BUS1_MSG_NODE_DESTROY:
                        return b1_peer_recv_node_destroy(peer,
                                                         &recv.node_destroy,
                                                         messagep);
        }

        return -EIO;
}

/**
 * b1_peer_recv_seed() - receive the seed message
 * @peer:               the receiving peer
 * @messagep:           the received seed
 *
 * Receives the seed message if available and returns it.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_peer_recv_seed(B1Peer *peer, B1Message **seedp) {
        struct bus1_cmd_recv recv = {
                .flags = BUS1_RECV_FLAG_SEED,
        };
        int r;

        assert(peer);

        r = bus1_client_recv(peer->client, &recv);
        if (r < 0)
                return r;

        if (recv.type != BUS1_MSG_DATA)
                return -EIO;

        return b1_peer_recv_data(peer, &recv.data, seedp);
}

/**
 * b1_peer_clone() - create a new peer connected to an existing one
 * @peer:               existing, parent peer
 * @childp:             new, child peer
 * @handle:             handle in @peer to transfer to @child
 * @child_handlep:            new handle in @child
 *
 * In order for peers to communicate, they must be reachable from one another.
 * This creates a new peer from an existing one and hands the new peer a handle
 * from the existing peer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_peer_clone(B1Peer *peer, B1Peer **childp, B1Handle *handle, B1Handle **child_handlep) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *child = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *child_handle = NULL;
        uint64_t parent_id, child_id;
        int r, fd;

        assert(peer);
        assert(childp);
        assert(handle);
        assert(child_handlep);

        if (handle->id != BUS1_HANDLE_INVALID)
                return -EOPNOTSUPP;

        r = bus1_client_clone(peer->client, &parent_id, &child_id, &fd, BUS1_CLIENT_POOL_SIZE);
        if (r < 0)
                return r;

        if (handle->id == BUS1_HANDLE_INVALID) {
                handle->id = parent_id;

                r = b1_handle_link(handle);
                if (r < 0)
                        return r;

                if (handle->node) {
                        assert(handle->node->id == BUS1_HANDLE_INVALID);
                        handle->node->id = parent_id;
                        r = b1_node_link(handle->node);
                        if (r < 0)
                                return r;
                }
        }

        r = b1_peer_new_from_fd(&child, fd);
        if (r < 0)
                return r;

        r = b1_handle_new(child, child_id, &child_handle);
        if (r < 0)
                return r;

        r = b1_handle_link(child_handle);
        if (r < 0)
                return r;

        *childp = child;
        child = NULL;
        *child_handlep = child_handle;
        child_handle = NULL;

        return 0;
}

B1Node *b1_peer_get_root_node(B1Peer *peer, const char *name) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->root_nodes, root_nodes_compare, name);
        if (!n)
                return NULL;

        return c_container_of(n, B1Node, rb);
}

/**
 * b1_peer_implement() - implement an interface on the corresponding root node
 * @peer:               peer holding the root node
 * @nodep:              pointer to root node
 * @userdata:           userdata to associate with root node
 * @interface:          interface to implement on root node
 *
 * A peer may contain a set of named, unused root nodes. In order to make use of
 * these nodes they must be associated with an interface. This takes an
 * interface and implements it on the root node with the corresponding name,
 * returning the node. The caller then becomes the owner of the node and it is
 * removed from the root-node map.
 *
 * Return: 0 on success, or a negitave error code on failure.
 */
_c_public_ int b1_peer_implement(B1Peer *peer, B1Node **nodep, void *userdata, B1Interface *interface) {
        B1Node *node;
        int r;

        assert(peer);
        assert(interface);

        node = b1_peer_get_root_node(peer, interface->name);
        if (!node)
                return -ENOENT;

        r = b1_node_implement(node, interface);
        if (r < 0)
                return r;

        node->userdata = userdata;
        c_rbtree_remove(&peer->root_nodes, &node->rb);
        node->owned = false;

        *nodep = node;

        return 0;
}

B1Node *b1_peer_get_node(B1Peer *peer, uint64_t node_id) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->nodes, nodes_compare, &node_id);
        if (!n)
                return NULL;

        return c_container_of(n, B1Node, rb);
}

B1Handle *b1_peer_get_handle(B1Peer *peer, uint64_t handle_id) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->handles, handles_compare, &handle_id);
        if (!n)
                return NULL;

        return c_container_of(n, B1Handle, rb);
}
