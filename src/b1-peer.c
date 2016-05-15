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
#include <stdlib.h>
#include <string.h>
#include "bus1-client.h"
#include "org.bus1/b1-peer.h"

typedef struct B1Member {
        CRBNode rb;
        char *name;
        char *type_input;
        char *type_output;
        B1NodeFn fn;
} B1Member;

typedef struct B1Implementation {
        CRBNode rb;
        B1Interface *interface;
} B1Implementation;

struct B1Handle {
        unsigned long n_ref;

        B1Peer *holder;
        B1Node *node;
        uint64_t id;

        bool marked; /* used for duplicate detection */

        B1Subscription *subscriptions;

        CRBNode rb;
};

struct B1Interface {
        unsigned long n_ref;
        bool implemented;

        char *name;

        CRBTree members;
};

struct B1Message {
        unsigned long n_ref;
        uint64_t type;

        B1Peer *peer;
        union {
                struct {
                        uint64_t destination;
                        uid_t uid;
                        gid_t gid;
                        pid_t pid;
                        pid_t tid;

                        void *slice;

                        B1Handle **handles;
                        size_t n_handles;
                        int *fds;
                        size_t n_fds;

                        CVariant *cv;

                        union {
                                struct {
                                        const char *interface;
                                        const char *member;
                                        B1Handle *reply_handle;
                                } call;
                                struct {
                                        B1Handle *reply_handle;
                                } reply;
                                struct {
                                        const char *name;
                                } error;
                        };
                } data;
                struct {
                        uint64_t handle_id;
                } node_destroy;
        };
};

struct B1Node {
        B1Peer *owner;
        B1Handle *handle;
        uint64_t id;
        void *userdata;

        bool live;

        CRBNode rb;

        CRBTree implementations;
        B1ReplySlot *slot;
        B1NodeFn destroy_fn;
};

struct B1Subscription {
        B1Subscription *previous;
        B1Subscription *next;

        B1Handle *handle;

        B1SubscriptionFn fn;
        void *userdata;
};

struct B1Peer {
        unsigned long n_ref;

        struct bus1_client *client;

        CRBTree nodes;
        CRBTree handles;
};

struct B1ReplySlot {
        char *type_input;
        B1Node *reply_node;
        B1ReplySlotFn fn;
        void *userdata;
};

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

        peer = malloc(sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->n_ref = 1;
        peer->nodes = (CRBTree){};
        peer->handles = (CRBTree){};
        peer->client = NULL;

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

        peer = malloc(sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->n_ref = 1;
        peer->nodes = (CRBTree){};
        peer->handles = (CRBTree){};

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

static int nodes_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Node *node = c_container_of(n, B1Node, rb);
        uint64_t id = *(uint64_t*)k;

        if (id < node->id)
                return -1;
        else if (id > node->id)
                return 1;
        else
                return 0;
}

static int handles_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Handle *handle = c_container_of(n, B1Handle, rb);
        uint64_t id = *(uint64_t*)k;

        if (id < handle->id)
                return -1;
        else if (id > handle->id)
                return 1;
        else
                return 0;
}

static int b1_node_link(B1Node *node, B1Peer *peer, uint64_t handle_id) {
        CRBNode **slot, *p;

        assert(node);
        assert(peer);
        assert(handle_id != BUS1_HANDLE_INVALID);

        slot = c_rbtree_find_slot(&peer->nodes,
                                  nodes_compare, &handle_id, &p);
        if (!slot)
                return -ENOTUNIQ;

        if (!node->owner)
                node->owner = b1_peer_ref(peer);
        else
                assert(node->owner == peer);

        node->id = handle_id;
        c_rbtree_add(&peer->nodes, p, slot, &node->rb);

        return 0;

}

static int b1_handle_link(B1Handle *handle, B1Peer *peer, uint64_t handle_id) {
        CRBNode **slot, *p;

        assert(handle);
        assert(peer);
        assert(handle_id != BUS1_HANDLE_INVALID);

        slot = c_rbtree_find_slot(&peer->handles,
                                  handles_compare, &handle_id, &p);
        if (!slot)
                return -ENOTUNIQ;

        if (!handle->holder)
                handle->holder = b1_peer_ref(peer);
        else
                assert(handle->holder == peer);

        handle->id = handle_id;
        c_rbtree_add(&peer->handles, p, slot, &handle->rb);

        return 0;

}

/**
 * b1_message_send() - send a message to the given handles
 * @message             the message to be sent
 * @handles             the destination handles
 * @n_handles           the number of handles
 *
 * Return: 0 on succes, or a negative error code on failure.
 */
_c_public_ int b1_message_send(B1Message *message,
                               B1Handle **handles,
                               size_t n_handles) {
        /* limit number of destinations? */
        uint64_t destinations[n_handles];
        uint64_t *handle_ids;
        size_t n_vecs;
        struct bus1_cmd_send send = {
                .ptr_destinations = (uintptr_t)destinations,
                .n_destinations = n_handles,
        };
        int r;

        assert(!n_handles || handles);

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -EINVAL;

        if (message->type == B1_MESSAGE_TYPE_SEED) {
                send.flags = BUS1_SEND_FLAG_SILENT | BUS1_SEND_FLAG_SEED;
                if (n_handles)
                        return -EINVAL;
        }

        b1_message_seal(message);

        handle_ids = malloc(sizeof(uint64_t) * message->data.n_handles);
        if (!handle_ids)
                return -ENOMEM;

        send.ptr_vecs = (uintptr_t)c_variant_get_vecs(message->data.cv, &n_vecs);
        send.n_vecs = n_vecs;
        send.ptr_handles = (uintptr_t)handle_ids;
        send.n_handles = message->data.n_handles;
        send.ptr_fds = (uintptr_t)message->data.fds;
        send.n_fds = message->data.n_fds;

        for (unsigned int i = 0; i < message->data.n_handles; i++) {
                B1Handle *handle = message->data.handles[i];

                if (handle->marked) {
                        r = -ENOTUNIQ;
                        goto error;
                }

                handle->marked = true;

                if (handle->id == BUS1_HANDLE_INVALID)
                        handle_ids[i] = BUS1_NODE_FLAG_MANAGED |
                                        BUS1_NODE_FLAG_ALLOCATE;
                else
                        handle_ids[i] = handle->id;
        }

        for (unsigned int i = 0; i < n_handles; i++) {
                if (handles[i]->holder != message->peer) {
                        r = -EINVAL;
                        goto error;
                }

                destinations[i] = handles[i]->id;
        }

        r = bus1_client_send(message->peer->client, &send);
        if (r < 0)
                goto error;

        for (unsigned int i = 0; i < message->data.n_handles; i++) {
                B1Handle *handle = message->data.handles[i];

                handle->marked = false;

                if (handle->id != BUS1_HANDLE_INVALID)
                        continue;

                assert(b1_handle_link(handle, message->peer, handle_ids[i]) >= 0);

                if (handle->node)
                        assert(b1_node_link(handle->node, message->peer, handle_ids[i]) >= 0);
        }

        free(handle_ids);

        return 0;

error:
        free(handle_ids);

        /* unmark handles */
        for (unsigned int i = 0; i < message->data.n_handles; i++) {
                B1Handle *handle = message->data.handles[i];

                /* found a handle that was never marked, so must be done */
                if (!handle->marked)
                        break;
                else
                        handle->marked = false;
        }

        return 0;
}

static int b1_message_new_from_slice(B1Message **messagep,
                                     B1Peer *peer,
                                     void *slice,
                                     size_t n_bytes) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        const struct iovec vec = {
                .iov_base = slice,
                .iov_len = n_bytes,
        };
        int r;

        assert(messagep);

        message = malloc(sizeof(*message));
        if (!message)
                return -ENOMEM;

        message->n_ref = 1;
        message->peer = b1_peer_ref(peer);
        message->data.slice = slice;

        r = c_variant_new_from_vecs(&message->data.cv,
                                    "(tvv)", strlen("(tvv)"),
                                    &vec, 1);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

static int b1_handle_new(B1Peer *peer, B1Handle **handlep) {
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;

        assert(handlep);

        handle = malloc(sizeof(*handle));
        if (!handle)
                return -ENOMEM;

        handle->n_ref = 1;
        handle->holder = b1_peer_ref(peer);
        handle->node = NULL;
        handle->id = BUS1_HANDLE_INVALID;
        handle->marked = false;
        handle->subscriptions = NULL;
        c_rbnode_init(&handle->rb);

        *handlep = handle;
        handle = NULL;
        return 0;
}

static void b1_handle_release(B1Handle *handle) {
        if (!handle)
                return;

        if (handle->id == BUS1_HANDLE_INVALID)
                return;

        (void)bus1_client_handle_release(handle->holder->client, handle->id);
}

static int b1_handle_acquire(B1Handle **handlep, B1Peer *peer, uint64_t handle_id) {
        B1Handle *handle;
        CRBNode **slot, *p;
        int r;

        assert(handlep);
        assert(peer);

        if (handle_id == BUS1_HANDLE_INVALID) {
                *handlep = NULL;
                return 0;
        }

        slot = c_rbtree_find_slot(&peer->handles, handles_compare, &handle_id, &p);
        if (slot) {
                r = b1_handle_new(peer, &handle);
                if (r < 0)
                        return r;

                handle->id = handle_id;

                c_rbtree_add(&peer->handles, p, slot, &handle->rb);
        } else {
                handle = c_container_of(p, B1Handle, rb);
                b1_handle_ref(handle);
                b1_handle_release(handle);
        }

        *handlep = handle;

        return 0;
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
                r = c_variant_enter(message->data.cv, "vm");
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

                        message->data.reply.reply_handle = message->data.handles[reply_handle];
                } else
                        message->data.reply.reply_handle = NULL;

                r = c_variant_exit(message->data.cv, "mv");

                break;

        case B1_MESSAGE_TYPE_ERROR:
                r = c_variant_read(message->data.cv, "v", "s", &message->data.error.name);
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

        message = malloc(sizeof(*message));
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
 * b1_peer_clone() - create a new peer connected to an existing one
 * @peer:               existing, parent peer
 * @nodep:              root node of new child peer
 * @handlep:            handle to @nodep in the parent peer
 *
 * In order for peers to communicate, they must be reachable from one another.
 * This creates a new peer and gives a handle to it to an existing peer,
 * allowing communication to be established.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_peer_clone(B1Peer *peer, B1Node **nodep, B1Handle **handlep) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *clone = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        uint64_t handle_id, node_id;
        int r, fd;

        assert(peer);
        assert(nodep);
        assert(handlep);

        r = bus1_client_clone(peer->client, &node_id, &handle_id, &fd, BUS1_CLIENT_POOL_SIZE);
        if (r < 0)
                return r;

        r = b1_peer_new_from_fd(&clone, fd);
        if (r < 0)
                return r;

        r = b1_handle_new(peer, &handle);
        if (r < 0)
                return r;

        r = b1_handle_link(handle, peer, handle_id);
        if (r < 0)
                return r;

        r = b1_node_new(clone, &node, clone);
        if (r < 0)
                return r;

        r = b1_node_link(node, clone, node_id);
        if (r < 0)
                return r;

        *nodep = node;
        node = NULL;
        *handlep = handle;
        handle = NULL;

        return 0;
}

/**
 * b1_reply_slot_free() - unregister and free slot
 * @slot:               a slot, or NULL
 *
 * Return: NULL.
 */
_c_public_ B1ReplySlot *b1_reply_slot_free(B1ReplySlot *slot) {
        b1_node_free(slot->reply_node);
        free(slot);

        return NULL;
}

/**
 * b1_reply_slot_get_userdata() - get userdata from slot
 * @slot:               a slot
 *
 * Retrurn: the userdata.
 */
_c_public_ void *b1_reply_slot_get_userdata(B1ReplySlot *slot) {
        if (!slot)
                return NULL;

        return b1_node_get_userdata(slot->reply_node);
}

static int b1_reply_slot_new(B1Peer *peer, B1ReplySlot **slotp, const char *type_input, B1ReplySlotFn fn, void *userdata) {
        _c_cleanup_(b1_reply_slot_freep) B1ReplySlot *slot = NULL;
        size_t n_type_input;
        int r;

        assert(slotp);
        assert(type_input);
        assert(fn);

        n_type_input = strlen(type_input) + 1;
        slot = malloc(sizeof(*slot) + n_type_input);
        if (!slot)
                return -ENOMEM;

        slot->type_input = NULL;
        slot->reply_node = NULL;
        slot->fn = fn;
        slot->type_input = (void *)(slot + 1);
        memcpy(slot->type_input, type_input, n_type_input);

        r = b1_node_new(peer, &slot->reply_node, userdata);
        if (r < 0)
                return r;

        slot->reply_node->slot = slot;

        *slotp = slot;
        slot = NULL;
        return 0;
}

_c_public_ int b1_message_append_handle(B1Message *message, B1Handle *handle) {
        B1Handle **handles;

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -EINVAL;

        if (message->peer != handle->holder)
                return -EINVAL;

        for (unsigned int i = 0; i < message->data.n_handles; i++) {
                if (message->data.handles[i] == handle)
                        return i;
        }

        handles = realloc(message->data.handles,
                          sizeof(*handles) * (message->data.n_handles + 1));
        if (!handles)
                return -ENOMEM;

        handles[message->data.n_handles ++] = b1_handle_ref(handle);

        message->data.handles = handles;

        return message->data.n_handles - 1;
}

_c_public_ int b1_message_append_fd(B1Message *message, int fd) {
        _c_cleanup_(c_closep) int new_fd = -1;
        int *fds;

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -EINVAL;

        new_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (new_fd == -1)
                return -errno;

        fds = realloc(message->data.fds,
                      sizeof(*fds) * (message->data.n_fds + 1));
        if (!fds)
                return -ENOMEM;

        fds[message->data.n_fds ++] = new_fd;
        new_fd = -1;

        message->data.fds = fds;

        return message->data.n_fds - 1;
}

static int b1_message_new(B1Peer *peer, B1Message **messagep, unsigned int type) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        message = malloc(sizeof(*message));
        if (!message)
                return -ENOMEM;

        message->type = type;
        message->n_ref = 1;
        message->peer = b1_peer_ref(peer);

        message->data.destination = BUS1_HANDLE_INVALID;
        message->data.uid = -1;
        message->data.gid = -1;
        message->data.pid = -1;
        message->data.tid = -1;
        message->data.slice = NULL;
        message->data.handles = NULL;
        message->data.n_handles = 0;
        message->data.fds = NULL;
        message->data.n_fds = 0;
        message->data.cv = NULL;

        /* <type, header variant, payload variant> */
        r = c_variant_new(&message->data.cv, "(tvv)", strlen("(tvv)"));
        if (r < 0)
                return r;

        r = c_variant_begin(message->data.cv, "(");
        if (r < 0)
                return r;

        r = c_variant_write(message->data.cv, "t", type);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

/**
 * b1_message_new_call() - create new method call
 * @messagep:           pointer to the new message object
 * @interface:          the interface to call on
 * @member:             the member of the interface
 * @type:               the type of the payload
 * @slotp:              pointer to a new reply object, or NULL
 * @fn:                 the reply handler, or NULL
 * @userdata:           the userdata to pass to the reply handler, or NULL
 *
 * All methods are namespaced by an interface name, and may optionally expect a 
 * response. If a response is expected, a new B1ReplySlot object is created and @fn
 * is called on @userdata when the response is received.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_message_new_call(B1Peer *peer,
                                   B1Message **messagep,
                                   const char *interface,
                                   const char *member,
                                   const char *signature_input,
                                   const char *signature_output,
                                   B1ReplySlot **slotp,
                                   B1ReplySlotFn fn,
                                   void *userdata) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        _c_cleanup_(b1_reply_slot_freep) B1ReplySlot *slot = NULL;
        int r;

        r = b1_message_new(peer, &message, B1_MESSAGE_TYPE_CALL);
        if (r < 0)
                return r;

        if (slotp) {
                r = b1_reply_slot_new(peer, &slot, signature_output, fn, userdata);
                if (r < 0)
                        return r;

                r = b1_message_append_handle(message, slot->reply_node->handle);
                if (r < 0)
                        return r;

                /* <interface, member, reply handle> */
                r = c_variant_write(message->data.cv, "v", "(ssmu)", interface, member, true, r);
                if (r < 0)
                        return r;
        } else {
                /* <interface, member, nothing> */
                r = c_variant_write(message->data.cv, "v", "(ssmu)", interface, member, false);
                if (r < 0)
                        return r;
        }

        r = c_variant_begin(message->data.cv, "v", signature_input);
        if (r < 0)
                return r;

        if (slotp) {
                *slotp = slot;
                slot = NULL;
        }

        *messagep = message;
        message = NULL;

        return 0;
}

/**
 * b1_message_new_reply() - create a new method reply
 * @messagep:           the new message object
 * @type:               the payload type
 * @slotp:              pointer to a new reply object, or NULL
 * @fn:                 the reply handler, or NULL
 * @userdata:           the userdata to pass to the reply handler, or NULL
 *
 * A reply to a method does not need an interface or a method name, as it is
 * sent directly to the reply object it es responding to. Otherwise it is
 * exactly like any other method call.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_message_new_reply(B1Peer *peer,
                                    B1Message **messagep,
                                    const char *signature_input,
                                    const char *signature_output,
                                    B1ReplySlot **slotp,
                                    B1ReplySlotFn fn,
                                    void *userdata) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        _c_cleanup_(b1_reply_slot_freep) B1ReplySlot *slot = NULL;
        int r;

        r = b1_message_new(peer, &message, B1_MESSAGE_TYPE_REPLY);
        if (r < 0)
                return r;

        if (slotp) {
                r = b1_reply_slot_new(peer, &slot, signature_output, fn, userdata);
                if (r < 0)
                        return r;

                r = b1_message_append_handle(message, slot->reply_node->handle);
                if (r < 0)
                        return r;

                /* <reply handle> */
                r = c_variant_write(message->data.cv, "v", "mu", true, r);
                if (r < 0)
                        return r;
        } else {
                /* <nothing> */
                r = c_variant_write(message->data.cv, "v", "mu", false);
                if (r < 0)
                        return r;
        }

        r = c_variant_begin(message->data.cv, "v", signature_input);
        if (r < 0)
                return r;

        if (slotp) {
                *slotp = slot;
                slot = NULL;
        }

        *messagep = message;
        message = NULL;

        return 0;
}

/**
 * b1_message_new_error() - create a new method error reply
 * @messagep:           the new message object
 * @type:               the payload type
 *
 * An error reply to a message can not receive a reply, otherwise it is exactly
 * like any other method reply.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_message_new_error(B1Peer *peer, B1Message **messagep, const char *name, const char *signature) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new(peer, &message, B1_MESSAGE_TYPE_ERROR);
        if (r < 0)
                return r;

        /* <> */
        r = c_variant_write(message->data.cv, "v", "s", name);
        if (r < 0)
                return r;

        r = c_variant_begin(message->data.cv, "v", signature);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

_c_public_ int b1_message_new_seed(B1Peer *peer,
                                   B1Message **messagep,
                                   B1Node **nodes,
                                   const char **node_names,
                                   size_t n_nodes,
                                   const char *signature) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        assert(peer);
        assert(messagep);
        assert(!n_nodes || (nodes && node_names));
        assert(signature);

        r = b1_message_new(peer, &message, B1_MESSAGE_TYPE_SEED);
        if (r < 0)
                return r;

        /* <array of name -> root handle offset mappings> */
        r = c_variant_begin(message->data.cv, "v", "a(su)");
        if (r < 0)
                return r;

        r = c_variant_begin(message->data.cv, "a");
        if (r < 0)
                return r;

        for (unsigned i = 0; i < n_nodes; i ++) {
                r = b1_message_append_handle(message, nodes[i]->handle);
                if (r < 0)
                        return r;

                r = c_variant_write(message->data.cv, "(su)", node_names[i], r);
                if (r < 0)
                        return r;
        }

        r = c_variant_end(message->data.cv, "a");
        if (r < 0)
                return r;

        r = c_variant_begin(message->data.cv, "v", signature);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

/**
 * b1_message_ref() - acquire reference
 * @message:            message to acquire reference to, or NULL
 *
 * Return: @message is returned.
 */
_c_public_ B1Message *b1_message_ref(B1Message *message) {
        if (!message)
                return NULL;

        assert(message->n_ref > 0);

        ++message->n_ref;

        return message;
}

/**
 * b1_message_unref() - release reference
 * @message:            message to release reference to, or NULL
 *
 * Return: NULL is returned.
 */
_c_public_ B1Message *b1_message_unref(B1Message *message) {
        if (!message)
                return NULL;

        assert(message->n_ref > 0);

        if (--message->n_ref > 0)
                return NULL;

        if (message->type != B1_MESSAGE_TYPE_NODE_DESTROY) {
                c_variant_free(message->data.cv);

                for (unsigned int i = 0; i < message->data.n_handles; i++)
                        b1_handle_unref(message->data.handles[i]);

                free(message->data.handles);

                for (unsigned int i = 0; i < message->data.n_fds; i++)
                        close(message->data.fds[i]);

                if (message->data.slice) {
                        bus1_client_slice_release(message->peer->client,
                                bus1_client_slice_to_offset(message->peer->client,
                                                            message->data.slice));
                } else
                        free(message->data.fds);
        }

        b1_peer_unref(message->peer);
        free(message);

        return NULL;
}

/**
 * b1_message_is_sealed() - determines if a message is sealed
 * @message:            the message
 *
 * A sealed message is immutable.
 *
 * Return: true if @message is sealed, false otherwise.
 */
_c_public_ bool b1_message_is_sealed(B1Message *message) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_is_sealed(cv);
}

/**
 * b1_message_get_type() - get the message type
 * @message:            the message
 *
 * A message can be a call, a reply, an error, or a node destruction
 * notification.
 *
 * Return: the message type.
 */
_c_public_ unsigned int b1_message_get_type(B1Message *message) {
        if (!message)
                return _B1_MESSAGE_TYPE_INVALID;

        return message->type;
}

static B1Node *b1_peer_get_node(B1Peer *peer, uint64_t node_id) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->nodes, nodes_compare, &node_id);
        if (!n)
                return NULL;

        return c_container_of(n, B1Node, rb);
}

static B1Handle *b1_peer_get_handle(B1Peer *peer, uint64_t handle_id) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->handles, handles_compare, &handle_id);
        if (!n)
                return NULL;

        return c_container_of(n, B1Handle, rb);
}

static int implementations_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Implementation *implementation = c_container_of(n, B1Implementation,
                                                          rb);
        const char *name = k;

        return strcmp(name, implementation->interface->name);
}

static B1Interface *b1_node_get_interface(B1Node *node, const char *name) {
        B1Implementation *implementation;
        CRBNode *n;

        assert(node);
        assert(name);

        n = c_rbtree_find_node(&node->implementations, implementations_compare, name);
        if (!n)
                return NULL;

        implementation = c_container_of(n, B1Implementation, rb);

        return implementation->interface;
}

static int members_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Member *member = c_container_of(n, B1Member, rb);
        const char *name = k;

        return strcmp(name, member->name);
}

static B1Member *b1_interface_get_member(B1Interface *interface, const char *name) {
        CRBNode *n;

        assert(interface);
        assert(name);

        n = c_rbtree_find_node(&interface->members, members_compare, name);
        if (!n)
                return NULL;

        return c_container_of(n, B1Member, rb);
}

static int b1_message_dispatch_node_destroy(B1Message *message) {
        B1Node *node;
        B1Handle *handle;
        uint64_t handle_id;
        int r = 0, k;

        assert(message);

        handle_id = message->node_destroy.handle_id;

        handle = b1_peer_get_handle(message->peer, handle_id);
        if (handle) {
                for (B1Subscription *s = handle->subscriptions; s; s = s->next) {
                        k = s->fn(s, s->userdata, handle);
                        if (k < 0 && r == 0)
                                r = k;
                }
        }

        node = b1_peer_get_node(message->peer, handle_id);
        if (node && node->destroy_fn) {
                k = node->destroy_fn(node, node->userdata, message);
                if (k < 0 && r == 0)
                        r = k;
        }

        return r;
}

static int b1_peer_reply_error(B1Message *origin, const char *name) {
        _c_cleanup_(b1_message_unrefp) B1Message *error = NULL;
        B1Handle *reply_handle;
        int r;

        reply_handle = b1_message_get_reply_handle(origin);
        if (!reply_handle)
                return 0;

        r = b1_message_new_error(origin->peer, &error, name, NULL);
        if (r < 0)
                return r;

        return b1_message_send(error, &reply_handle, 1);
}

static int b1_peer_reply_errno(B1Message *origin, unsigned int err) {
        _c_cleanup_(b1_message_unrefp) B1Message *error = NULL;
        B1Handle *reply_handle;
        int r;

        reply_handle = b1_message_get_reply_handle(origin);
        if (!reply_handle)
                return 0;

        r = b1_message_new_error(origin->peer, &error, "org.bus1.Error.Errno", "u");
        if (r < 0)
                return r;

        r = b1_message_write(error, "u", err);
        if (r < 0)
                return r;

        return b1_message_send(error, &reply_handle, 1);
}

static int b1_message_dispatch_data(B1Message *message) {
        B1Node *node;
        B1Interface *interface;
        B1Member *member;
        const char *signature;
        size_t signature_len;
        int r;

        assert(message);

        node = b1_peer_get_node(message->peer, message->data.destination);
        if (!node)
                return b1_peer_reply_error(message, "org.bus1.Error.NodeDestroyed");

        node->live = true;

        switch (message->type) {
        case B1_MESSAGE_TYPE_CALL:
                interface = b1_node_get_interface(node, message->data.call.interface);
                if (!interface)
                        return b1_peer_reply_error(message, "org.bus1.Error.InvalidInterface");

                member = b1_interface_get_member(interface, message->data.call.member);
                if (!member)
                        return b1_peer_reply_error(message, "org.bus1.Error.InvalidMember");

                signature = b1_message_peek_type(message, &signature_len);
                if (strncmp(member->type_input, signature, signature_len) != 0)
                        return b1_peer_reply_error(message, "org.bus1.Error.InvalidSignature");

                r = member->fn(node, node->userdata, message);
                if (r < 0)
                        return b1_peer_reply_errno(message, -r);

                break;
        case B1_MESSAGE_TYPE_REPLY:
                if (!node->slot)
                        return b1_peer_reply_error(message, "org.bus1.Error.InvalidNode");

                signature = b1_message_peek_type(message, &signature_len);
                if (strncmp(node->slot->type_input, signature, signature_len) != 0)
                        return b1_peer_reply_error(message, "org.bus1.Error.InvalidSignature");

                r = node->slot->fn(node->slot, node->userdata, message);
                if (r < 0)
                        return b1_peer_reply_errno(message, -r);

                break;
        case B1_MESSAGE_TYPE_ERROR:
                if (!node->slot)
                        return b1_peer_reply_error(message, "org.bus1.Error.InvalidNode");

                r = node->slot->fn(node->slot, node->userdata, message);
                if (r < 0)
                        return b1_peer_reply_errno(message, -r);

                break;
        default:
                return b1_peer_reply_error(message, "org.bus1.Error.InvalidMessageType");
        }

        return 0;
}

/**
 * b1_message_dispatch() - handle received message
 * @message:            the message to handle
 *
 * Dispatch the incoming message by matching it with the correct node and
 * callback. The precis semantics depends on the message type.
 *
 * Return: 0 on success, or a negitave error code on failure.
 */
_c_public_ int b1_message_dispatch(B1Message *message) {
        assert(message);

        if (message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return b1_message_dispatch_node_destroy(message);
        else
                return b1_message_dispatch_data(message);
}

/**
 * b1_message_get_reply_handle() - get reply handle of received message
 * @message:            the message
 *
 * A method call or a method reply may optionally come with a reply handle,
 * where any replies should be sent.
 *
 * Return: the handle.
 */
_c_public_ B1Handle *b1_message_get_reply_handle(B1Message *message) {
        if (!message)
                return NULL;

        switch (message->type) {
                case B1_MESSAGE_TYPE_CALL:
                        return message->data.call.reply_handle;
                case B1_MESSAGE_TYPE_REPLY:
                        return message->data.reply.reply_handle;
                default:
                        return NULL;
        }
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
_c_public_ uid_t b1_message_get_uid(B1Message *message) {
        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -1;

        return message->data.uid;
}

/**
 * b1_message_get_gid() - get gid of sending peer
 * @message:            the received message
 *
 * Return: the gid
 */
_c_public_ gid_t b1_message_get_gid(B1Message *message) {
        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -1;

        return message->data.gid;
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
_c_public_ pid_t b1_message_get_pid(B1Message *message) {
        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -1;

        return message->data.pid;
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
_c_public_ pid_t b1_message_get_tid(B1Message *message) {
        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -1;

        return message->data.tid;
}

/**
 * XXX: see CVariant
 */
_c_public_ size_t b1_message_peek_count(B1Message *message) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_peek_count(cv);
}

/**
 * XXX: see CVariant
 */
_c_public_ const char *b1_message_peek_type(B1Message *message, size_t *sizep) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_peek_type(cv, sizep);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_enter(B1Message *message, const char *containers) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_enter(cv, containers);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_exit(B1Message *message, const char *containers) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_exit(cv, containers);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_readv(B1Message *message, const char *signature, va_list args) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_readv(cv, signature, args);
}

/**
 * XXX: see CVariant
 */
_c_public_ void b1_message_rewind(B1Message *message) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        c_variant_rewind(cv);

        assert(c_variant_enter(cv, "(") >= 0);
        assert(c_variant_read(cv, "tv", NULL, NULL) >= 0);
        assert(c_variant_enter(cv, "v") >= 0);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_beginv(B1Message *message, const char *containers, va_list args) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_beginv(cv, containers, args);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_end(B1Message *message, const char *containers) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_end(cv, containers);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_writev(B1Message *message, const char *signature, va_list args) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_writev(cv, signature, args);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_insert(B1Message *message, const char *type, const struct iovec *vecs, size_t n_vecs) {
        CVariant *cv = NULL;

        if (message && message->type != B1_MESSAGE_TYPE_NODE_DESTROY)
                cv = message->data.cv;

        return c_variant_insert(cv, type, vecs, n_vecs);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_seal(B1Message *message) {
        CVariant *cv;
        int r;

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return 0;

        cv = message->data.cv;

        r = c_variant_seal(cv);
        if (r < 0)
                return r;

        r = c_variant_enter(cv, "(");
        if (r < 0)
                return r;

        r = c_variant_read(cv, "tv", NULL, NULL);
        if (r < 0)
                return r;

        r = c_variant_enter(cv, "v");
        if (r < 0)
                return r;

        return 0;
}

/**
 * b1_message_get_handle() - get hande passed with a message
 * @message:            the message
 * @index               the index in the passed handle array
 * @handlep:            pointer to the returned handle object
 *
 * Messages may pass handles along. The payload typically describes the passed
 * handles and reference them by their index. The index is local to each message
 * and has no meaning outside of a given message.
 *
 * The caller needs to take a reference to the handle if they want to keep it
 * after the message has been freed.
 *
 * Returns: 0 on success, or a negitave error code on failure.
 */
_c_public_ int b1_message_get_handle(B1Message *message, unsigned int index, B1Handle **handlep) {
        assert(handlep);

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -EINVAL;

        if (index >= message->data.n_handles)
                return -ERANGE;

        *handlep = message->data.handles[index];

        return 0;
}

/**
 * b1_message_get_fd() - get fd passed with a message
 * @message:            the message
 * @index               the index in the passed fd array
 * @fdp:                pointer to the returned fd number
 *
 * Messages may pass file descriptors along. The payload typically describes the
 * passed fds and reference them by their index. The index is local to each
 * message and has no meaning outside of a given message.
 *
 * The caller needs to duplicate a file descriptor if they want to keep it after
 * the message has been freed.
 *
 * Returns: 0 on success, or a negitave error code on failure.
 */
_c_public_ int b1_message_get_fd(B1Message *message, unsigned int index, int *fdp) {
        assert(fdp);

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -EINVAL;

        if (index >= message->data.n_fds)
                return -ERANGE;

        *fdp = message->data.fds[index];

        return 0;
}

/**
 * b1_node_new() - create a new node for a peer
 * @peer:               the owning peer, or null
 * @nodep:              pointer to the new node object
 * @userdata:           userdata to associate with the node
 *
 * A node is the recipient of messages, and. Nodes are allocated lazily in the
 * kernel, so it is not guaranteed to be a kernel equivalent to the userspace
 * object at all times. A node is associated with at most one peer, and for a
 * lazily created node it may not be associated with any peer until it is
 * actually created, at which point it is associated with the creating peer.
 *
 * Return: 0 on success, and a negative error code on failure.
 */
_c_public_ int b1_node_new(B1Peer *peer, B1Node **nodep, void *userdata) {
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        int r;

        assert(nodep);

        node = malloc(sizeof(*node));
        if (!node)
                return -ENOMEM;

        node->id = BUS1_HANDLE_INVALID;
        node->owner = b1_peer_ref(peer);
        node->userdata = userdata;
        node->live = false;
        node->implementations = (CRBTree){};
        node->slot = NULL;
        node->handle = NULL;
        node->destroy_fn = NULL;

        r = b1_handle_new(peer, &node->handle);
        if (r < 0)
                return r;

        node->handle->node = node;

        *nodep = node;
        node = NULL;
        return 0;
}

/**
 * b1_node_free() - destroy a node
 * @node:               node to destroy
 *
 * This destroys the given node and releases all linked resources. This implies
 * a call to b1_node_destroy(), if not already done by the caller.
 *
 * Return: NULL is returned.
 */
_c_public_ B1Node *b1_node_free(B1Node *node) {
        CRBNode *n;

        if (!node)
                return NULL;

        b1_node_release(node);
        b1_node_destroy(node);

        while ((n = c_rbtree_first(&node->implementations))) {
                B1Implementation *implementation = c_container_of(n, B1Implementation, rb);

                c_rbtree_remove(&node->implementations, n);
                b1_interface_unref(implementation->interface);
                free(implementation);
        }

        if (node->id != BUS1_HANDLE_INVALID) {
                assert(node->owner);
                c_rbtree_remove(&node->owner->nodes, &node->rb);
        }

        b1_peer_unref(node->owner);
        free(node);

        return NULL;
}

/**
 * b1_node_get_peer() - get parent peer of a node
 * @node:               node to query
 *
 * Return a pointer to the parent peer of a node. This is the peer the node was
 * created on. It is constant and will never change.
 *
 * Return: Pointer to parent peer of @node.
 */
_c_public_ B1Peer *b1_node_get_peer(B1Node *node) {
        assert(node);
        return node->owner;
}

/**
 * b1_node_get_handle() - get owner handle of a node
 * @node:               node to query
 *
 * This returns the owner's handle of a node. For each create node, the owner
 * gets a handle themself initially. Unless released via b1_node_release(), an
 * owner can query this handle at any time.
 *
 * Return: Pointer to owner's handle, or NULL if already released.
 */
_c_public_ B1Handle *b1_node_get_handle(B1Node *node) {
        assert(node);
        return node->handle;
}

/**
 * b1_node_get_userdata() - get userdata associatde with a node
 * @node:               node to query
 *
 * Return the userdata associated with @node. If it was not set explicitly, it
 * will be set to NULL.
 *
 * Return: Userdata of the given node.
 */
_c_public_ void *b1_node_get_userdata(B1Node *node) {
        assert(node);
        return node->userdata;
}

/**
 * b1_node_set_destroy_fn() - set function to call when node is destroyed
 * @node:               node to operate on
 * @fn:                 function callback to set, or NULL
 *
 * This changes the function callback used for destruction notifications on
 * @node. If NULL, the functionality is disabled.
 */
_c_public_ void b1_node_set_destroy_fn(B1Node *node, B1NodeFn fn) {
        assert(node);

        node->destroy_fn = fn;
}

/**
 * b1_node_implement() - implement interface on node
 * @node:               node to operate on
 * @interface:          interface to implement
 *
 * Extend @node to support the interface given as @interface. From then on, the
 * node will dispatch incoming method calls on this interface.
 *
 * This fails, if the given interface is already implemented by @node.
 *
 * Return: 0 on success, negative error code on failure.
 */
_c_public_ int b1_node_implement(B1Node *node, B1Interface *interface) {
        B1Implementation *implementation;
        CRBNode **slot, *p;

        assert(node);
        assert(interface);

        if (node->live || node->slot)
                return -EBUSY;

        slot = c_rbtree_find_slot(&node->implementations, implementations_compare, interface->name, &p);
        if (!slot)
                return -ENOTUNIQ;

        implementation = malloc(sizeof(*implementation));
        if (!implementation)
                return -ENOMEM;

        c_rbnode_init(&implementation->rb);
        implementation->interface = b1_interface_ref(interface);
        interface->implemented = true;

        c_rbtree_add(&node->implementations, p, slot, &implementation->rb);
        return 0;
}

/**
 * b1_node_release() - release handle to node
 * @node:               node to release, or NULL
 *
 * When the owner of a node releases its handle to it, it means that the node
 * will be released as soon as no other peer is pinning a handle to it. It also
 * means that the owning peer can no longer hand out handles to the node to
 * other peers.
 *
 * If NULL is passed, this is a no-op.
 */
_c_public_ void b1_node_release(B1Node *node) {
        if (!node || !node->handle)
                return;

        node->handle->node = NULL;
        node->handle = b1_handle_unref(node->handle);
}

/**
 * b1_node_destroy() - destroy node
 * @node:               node to destroy, or NULL
 *
 * Destroy the node in the kernel, regardless of any handles held by other
 * peers. If any peers still hold handles, they will receive node destruction
 * notifications for this node.
 *
 * If NULL is passed, this is a no-op.
 */
_c_public_ void b1_node_destroy(B1Node *node) {
        if (!node || node->id == BUS1_HANDLE_INVALID)
                return;

        (void)bus1_client_node_destroy(node->owner->client, node->id);
}

/**
 * b1_handle_ref() - acquire reference
 * @handle:             handle to acquire reference to, or NULL
 *
 * Acquire a new reference to a handle. The caller must already own a reference
 * themself.
 *
 * If NULL is passed, this is a no-op.
 *
 * Return: @handle is returned.
 */
_c_public_ B1Handle *b1_handle_ref(B1Handle *handle) {
        if (handle) {
                assert(handle->n_ref > 0);
                ++handle->n_ref;
        }
        return handle;
}

/**
 * b1_handle_unref() - release reference
 * @handle:             handle to release reference to, or NULL
 *
 * Release a single reference to an handle. If this is the last reference, the
 * handle is freed.
 *
 * If NULL is passed, this is a no-op.
 *
 * Return: NULL is returned.
 */
_c_public_ B1Handle *b1_handle_unref(B1Handle *handle) {
        if (!handle)
                return NULL;

        assert(handle->n_ref > 0);

        if (--handle->n_ref > 0)
                return NULL;

        b1_handle_release(handle);

        if (handle->id != BUS1_HANDLE_INVALID) {
                assert(handle->holder);
                c_rbtree_remove(&handle->holder->handles, &handle->rb);
        }

        b1_peer_unref(handle->holder);
        free(handle);

        return NULL;
}

/**
 * b1_handle_get_peer() - get parent of a handle
 * @handle:             handle to query
 *
 * Query the parent peer of a handle.
 *
 * Return: Pointer to parent peer of @handle.
 */
_c_public_ B1Peer *b1_handle_get_peer(B1Handle *handle) {
        assert(handle);
        return handle->holder;
}

/**
 * b1_subscription_free() - unregister and free subscription
 * @subscription:               a subscription, or NULL
 *
 * Return: NULL.
 */
_c_public_ B1Subscription *b1_subscription_free(B1Subscription *subscription) {
        if (subscription->previous)
                subscription->previous->next = subscription->next;
        if (subscription->next)
                subscription->next->previous = subscription->previous;

        free(subscription);

        return NULL;
}

/**
 * b1_subscription_get_userdata() - get userdata from subscription
 * @subscription:               a subscription
 *
 * Retrurn: the userdata.
 */
_c_public_ void *b1_subscription_get_userdata(B1Subscription *subscription) {
        if (!subscription)
                return NULL;

        return subscription->userdata;
}

/**
 * b1_handle_subscribe() - subscribe to handle events
 * @handle:             handle to operate on
 * @slotp:              output argument for newly created slot
 * @fn:                 slot callback function
 * @userdata:           userdata to be passed to the callback function
 *
 * When a node is destroyed, all handle holders to that node receive a node
 * destruction notification. This function registers a new handler for such
 * notifications.
 *
 * The handler will stay linked to the handle as long as the returned slot is
 * valid. Once the slot is destroyed, the handler is unlinked as well.
 *
 * Return: 0 on success, negative error code on failure.
 */
_c_public_ int b1_handle_subscribe(B1Handle *handle, B1Subscription **subscriptionp, B1SubscriptionFn fn, void *userdata) {
        _c_cleanup_(b1_subscription_freep) B1Subscription *subscription = NULL;

        assert(subscriptionp);
        assert(fn);

        subscription = malloc(sizeof(*subscription));
        if (!subscription)
                return -ENOMEM;

        subscription->fn = fn;
        subscription->userdata = userdata;

        subscription->handle = handle;
        subscription->previous = NULL;
        subscription->next = handle->subscriptions;

        handle->subscriptions = subscription;

        *subscriptionp = subscription;
        subscription = NULL;
        return 0;
}

/**
 * b1_interface_new() - create new interface
 * @interfacep:         pointer to new interface object
 * @name:               interface name
 *
 * An interface is a named collection of methods, that can be associated with
 * nodes. A method is invoked over the bus by making a method call to a node
 * and supplying the interafce and method names.
 *
 * Return: 0 on success, negative error code on failure.
 */
_c_public_ int b1_interface_new(B1Interface **interfacep, const char *name) {
        _c_cleanup_(b1_interface_unrefp) B1Interface *interface = NULL;
        size_t n_name;

        assert(interfacep);
        assert(name);

        n_name = strlen(name) + 1;
        interface = malloc(sizeof(*interface) + n_name);
        if (!interface)
                return -ENOMEM;

        interface->n_ref = 1;
        interface->implemented = false;
        interface->name = (void *)(interface + 1);
        interface->members = (CRBTree){};
        memcpy(interface->name, name, n_name);

        *interfacep = interface;
        interface = NULL;
        return 0;
}

/**
 * b1_interface_ref() - acquire reference
 * @interface:          interface to acquire reference to, or NULL
 *
 * Acquire a single new reference to the given interface. The caller must
 * already own a reference.
 *
 * If NULL is passed, this is a no-op.
 *
 * Return: @interface is returned.
 */
_c_public_ B1Interface *b1_interface_ref(B1Interface *interface) {
        if (interface) {
                assert(interface->n_ref > 0);
                ++interface->n_ref;
        }
        return interface;
}

/**
 * b1_interface_unref() - release reference
 * @interface:          interface to release reference to, or NULL
 *
 * Release a single reference to @interface. If this is the last reference, the
 * interface is destroyed.
 *
 * If NULL is passed, this is a no-op.
 *
 * Return: NULL is returned.
 */
_c_public_ B1Interface *b1_interface_unref(B1Interface *interface) {
        CRBNode *node;

        if (!interface)
                return NULL;

        assert(interface->n_ref > 0);

        if (--interface->n_ref > 0)
                return NULL;

        while ((node = c_rbtree_first(&interface->members))) {
                B1Member *member = c_container_of(node, B1Member, rb);

                c_rbtree_remove(&interface->members, node);
                free(member);
        }

        free(interface);

        return NULL;
}

/**
 * b1_interface_add_member() - add member to interface
 * @interface:          interface to operate on
 * @name:               member name
 * @type_input:         type of the expected input argument
 * @type_output:        type of the produced output arguments
 * @fn:                 function implementing the member
 *
 * This adds a new member function to the given interface. The member function
 * name must not already exist, or this will fail.
 *
 * @type_input must describe the input types expected by the callback, which
 * will be pre-validated by the library on all input. @type_output describes
 * the types expected to be produced as a result, and is verified by the
 * library as well.
 *
 * Return: 0 on succes, or a negative error code on failure.
 */
_c_public_ int b1_interface_add_member(B1Interface *interface,
                                       const char *name,
                                       const char *type_input,
                                       const char *type_output,
                                       B1NodeFn fn) {
        size_t n_name, n_type_input, n_type_output;
        B1Member *member;
        CRBNode **slot, *p;

        assert(interface);
        assert(name);
        assert(type_input);
        assert(type_output);
        assert(fn);

        if (interface->implemented)
                return -EBUSY;

        slot = c_rbtree_find_slot(&interface->members, members_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        n_name = strlen(name) + 1;
        n_type_input = strlen(type_input) + 1;
        n_type_output = strlen(type_output) + 1;
        member = malloc(sizeof(*member) + n_name + n_type_input + n_type_output);
        if (!member)
                return -ENOMEM;

        c_rbnode_init(&member->rb);
        member->name = (void *)(member + 1);
        member->type_input = member->name + n_name;
        member->type_output = member->type_input + n_type_input;
        member->fn = fn;

        memcpy(member->name, name, n_name);
        memcpy(member->type_input, type_input, n_type_input);
        memcpy(member->type_output, type_output, n_type_output);
        c_rbtree_add(&interface->members, p, slot, &member->rb);

        return 0;
}

/**
 * b1_message_reply() - send reply to a message
 * @origin:             message to reply to
 * @reply:              reply to send
 *
 * For convenience, this allows a reply to be sent directly to the reply handle
 * of another message. It is equivalent to requesting the reply handle via
 * b1_message_get_reply_handle() and using it as destination via b1_message_send().
 *
 * Return: 0 on success, negative error code on failure.
 */
_c_public_ int b1_message_reply(B1Message *origin, B1Message *reply) {
        B1Handle *reply_handle;

        reply_handle = b1_message_get_reply_handle(origin);
        if (!reply_handle)
                return -EINVAL;

        return b1_message_send(reply, &reply_handle, 1);
}

/**
 * b1_peer_export_to_environment() - set a bus1 peer fd in the environment
 * @peer:               peer to export
 *
 * For convenience, this allows a bus1 peer to be exported to the environment
 * so its file descriptor can be passed to a child process. The environment
 * variable BUS1_PEER_FD is set to the file descriptor number of the underlying
 * bus1 fd, in decimal.
 */
_c_public_ int b1_peer_export_to_environment(B1Peer *peer) {
        char fdnum[C_DECIMAL_MAX(int)];
        int r;

        r = b1_peer_get_fd(peer);
        if (r < 0)
                return r;

        r = sprintf(fdnum, "%d", r);
        if (r < 0)
                return -EINVAL;

        r = setenv("BUS1_PEER_FD", fdnum, 1);
        if (r < 0)
                return -errno;

        return 0;
}

/**
 * b1_peer_new_from_environment() - get a passed in bus1 peer fd from the environment
 * @peerp:              pointer to new peer object
 *
 * For convenience, this allows a peer to be created from a passed in file
 * descriptor. The filedescriptor number to use is given by the environment
 * variable BUS1_PEER_FD.
 */
_c_public_ int b1_peer_new_from_environment(B1Peer **peerp) {
        char *fd_var, *endptr;
        int fd;

        fd_var = secure_getenv("BUS1_PEER_FD");
        if (!fd_var)
                return -ENOENT;
        else if (*fd_var == '\0')
                return -EINVAL;

        fd = strtol(fd_var, &endptr, 10);
        if (*endptr != '\0')
                return -EINVAL;

        return b1_peer_new_from_fd(peerp, fd);
}
