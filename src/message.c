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
#include "bus1-client.h"
#include "org.bus1/b1-peer.h"

struct B1ReplySlot {
        char *type_input;
        B1Node *reply_node;
        B1ReplyFn fn;
        void *userdata;
};

/**
 * b1_reply_slot_free() - unregister and free slot
 * @slot:               a slot, or NULL
 *
 * Return: NULL.
 */
_c_public_ B1ReplySlot *b1_reply_slot_free(B1ReplySlot *slot) {
        if (!slot)
                return NULL;

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

static int b1_reply_slot_new(B1Peer *peer, B1ReplySlot **slotp, const char *type_input, B1ReplyFn fn, void *userdata) {
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
                .ptr_destinations = n_handles > 0 ? (uintptr_t)destinations : 0,
                .n_destinations = n_handles,
        };
        int r;

        assert(!n_handles || handles);

        if (!message || message->type == B1_MESSAGE_TYPE_NODE_DESTROY)
                return -EINVAL;

        if (message->type == B1_MESSAGE_TYPE_SEED) {
                send.flags = BUS1_SEND_FLAG_SEED;
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
                                        BUS1_NODE_FLAG_ALLOCATE |
                                        (handle->node->persistent ? BUS1_NODE_FLAG_PERSISTENT : 0);
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

                handle->id = handle_ids[i];

                assert(b1_handle_link(handle) >= 0);

                if (handle->node) {
                        handle->node->id = handle_ids[i];
                        assert(b1_node_link(handle->node) >= 0);
                }
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

        return r;
}

int b1_message_new_from_slice(B1Message **messagep, B1Peer *peer, void *slice, size_t n_bytes) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        const struct iovec vec = {
                .iov_base = slice,
                .iov_len = n_bytes,
        };
        int r;

        assert(messagep);

        message = calloc(1, sizeof(*message));
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

        message = calloc(1, sizeof(*message));
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
 * @signature_input:    the type of the payload
 * @signature_output:   the type of the expected reply message, or NULL
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
                                   B1ReplyFn fn,
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
 * @signature:          the payload type
 *
 * A reply to a method does not need an interface or a method name, as it is
 * sent directly to the reply object it is responding to. It is not possible to
 * reply to a reply, so no slot is provided. Otherwise this behaves like a
 * method call.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_message_new_reply(B1Peer *peer,
                                    B1Message **messagep,
                                    const char *signature) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new(peer, &message, B1_MESSAGE_TYPE_REPLY);
        if (r < 0)
                return r;

        /* <> */
        r = c_variant_write(message->data.cv, "v", "()");
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
 * b1_message_new_error() - create a new method error reply
 * @messagep:           the new message object
 * @type:               the payload type
 *
 * An error reply to a message is exactly like any other method reply, except
 * the receiver does not enforce a specific type.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
_c_public_ int b1_message_new_error(B1Peer *peer, B1Message **messagep, const char *name, const char *signature) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new(peer, &message, B1_MESSAGE_TYPE_ERROR);
        if (r < 0)
                return r;

        /* <error name> */
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

static int strcmpuniq(const void *ap, const void *bp, void *userdata) {
        B1Node *a = * (B1Node**) ap;
        B1Node *b = * (B1Node**) bp;
        bool *uniqp = userdata;
        int r;

        r = strcmp(a->name, b->name);
        if (r == 0)
                *uniqp = false;

        return r;
}

_c_public_ int b1_message_new_seed(B1Peer *peer,
                                   B1Message **messagep,
                                   B1Node **nodes,
                                   size_t n_nodes,
                                   const char *signature) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        bool uniq = true;
        int r;

        assert(peer);
        assert(messagep);
        assert(!n_nodes || nodes);
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
                if (!nodes[i]->name)
                        return -EINVAL;

                r = b1_message_append_handle(message, nodes[i]->handle);
                if (r < 0)
                        return r;

                r = c_variant_write(message->data.cv, "(su)", nodes[i]->name, r);
                if (r < 0)
                        return r;
        }

        /* node names must be globally unique, so refuse duplicates */
        qsort_r(nodes, n_nodes, sizeof(B1Node*), strcmpuniq, &uniq);
        if (!uniq)
                return -EINVAL;

        r = c_variant_end(message->data.cv, "a");
        if (r < 0)
                return r;

        r = c_variant_end(message->data.cv, "v");
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

        if (message->type == B1_MESSAGE_TYPE_SEED) {
                CRBNode *n;

                while ((n = c_rbtree_first(&message->data.seed.root_nodes))) {
                        B1Node *node = c_container_of(n, B1Node, rb_root_nodes);

                        c_rbtree_remove_init(&message->data.seed.root_nodes, n);
                        b1_node_free(node);
                }
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

static int b1_message_dispatch_notification(B1Message *message) {
        B1Node *node;
        B1Handle *handle;
        uint64_t handle_id;
        int r = 0, k;

        assert(message);

        handle_id = message->notification.handle_id;

        switch (message->type) {
        case B1_MESSAGE_TYPE_NODE_DESTROY:
                handle = b1_peer_get_handle(message->peer, handle_id);
                if (handle) {
                        for (CListEntry *le = c_list_first(&handle->notification_slots);
                             le; le = c_list_entry_next(le)) {
                                B1NotificationSlot *slot = c_container_of(le, B1NotificationSlot, le);

                                k = b1_notification_dispatch(slot);
                                if (k < 0 && r == 0)
                                        r = k;
                        }
                }

                break;
        case B1_MESSAGE_TYPE_NODE_RELEASE:
                node = b1_peer_get_node(message->peer, handle_id);
                if (node && node->destroy_fn) {
                        k = node->destroy_fn(node, node->userdata, message);
                        if (k < 0 && r == 0)
                                r = k;
                }

                break;
        }

        return r;
}

static int b1_message_reply_error(B1Message *origin, const char *name) {
        _c_cleanup_(b1_message_unrefp) B1Message *error = NULL;
        B1Handle *reply_handle;
        int r;

        reply_handle = b1_message_get_reply_handle(origin);
        if (!reply_handle)
                return 0;

        r = b1_message_new_error(origin->peer, &error, name, "");
        if (r < 0)
                return r;

        return b1_message_send(error, &reply_handle, 1);
}

static int b1_message_reply_errno(B1Message *origin, unsigned int err) {
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
                return -EIO;

        node->live = true;

        switch (message->type) {
        case B1_MESSAGE_TYPE_CALL:
                interface = b1_node_get_interface(node, message->data.call.interface);
                if (!interface) {
                        if (b1_peer_get_root_node(message->peer, message->data.call.interface))
                                return b1_message_reply_error(message, "org.bus1.Error.MissingRootInterface");
                        return b1_message_reply_error(message, "org.bus1.Error.InvalidInterface");
                }

                member = b1_interface_get_member(interface, message->data.call.member);
                if (!member)
                        return b1_message_reply_error(message, "org.bus1.Error.InvalidMember");

                signature = b1_message_peek_type(message, &signature_len);
                if (strncmp(member->type_input, signature, signature_len) != 0)
                        return b1_message_reply_error(message, "org.bus1.Error.InvalidSignature");

                r = member->fn(node, node->userdata, message);
                if (r < 0)
                        return b1_message_reply_errno(message, -r);

                break;
        case B1_MESSAGE_TYPE_REPLY:
                if (!node->slot)
                        break;

                signature = b1_message_peek_type(message, &signature_len);
                if (strncmp(node->slot->type_input, signature, signature_len) != 0)
                        break;

                (void)node->slot->fn(node->slot, node->userdata, message);

                break;
        case B1_MESSAGE_TYPE_ERROR:
                if (node->slot)
                        (void)node->slot->fn(node->slot, node->userdata, message);

                break;
        default:
                return b1_message_reply_error(message, "org.bus1.Error.InvalidMessageType");
        }

        return 0;
}

static int b1_message_dispatch_seed(B1Message *message) {
        CRBNode *n;

        while ((n = c_rbtree_first(&message->peer->root_nodes))) {
                B1Node *node = c_container_of(n, B1Node, rb_root_nodes);

                c_rbtree_remove_init(&message->peer->root_nodes, n);
                b1_node_free(node);
        }

        message->peer->root_nodes = message->data.seed.root_nodes;
        message->data.seed.root_nodes = (CRBTree){};

        return 0;
}

static bool b1_message_is_notification(B1Message *message) {
        assert(message);

        return (message->type == B1_MESSAGE_TYPE_NODE_DESTROY ||
                message->type == B1_MESSAGE_TYPE_NODE_RELEASE);
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

        if (b1_message_is_notification(message))
                return b1_message_dispatch_notification(message);
        else if (message->type == B1_MESSAGE_TYPE_SEED)
                return b1_message_dispatch_seed(message);
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
        if (!message || b1_message_is_notification(message))
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
        if (!message || b1_message_is_notification(message))
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
        if (!message || b1_message_is_notification(message))
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
        if (!message || b1_message_is_notification(message))
                return -1;

        return message->data.tid;
}

/**
 * XXX: see CVariant
 */
_c_public_ size_t b1_message_peek_count(B1Message *message) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_peek_count(cv);
}

/**
 * XXX: see CVariant
 */
_c_public_ const char *b1_message_peek_type(B1Message *message, size_t *sizep) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_peek_type(cv, sizep);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_enter(B1Message *message, const char *containers) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_enter(cv, containers);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_exit(B1Message *message, const char *containers) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_exit(cv, containers);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_readv(B1Message *message, const char *signature, va_list args) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_readv(cv, signature, args);
}

/**
 * XXX: see CVariant
 */
_c_public_ void b1_message_rewind(B1Message *message) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
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

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_beginv(cv, containers, args);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_end(B1Message *message, const char *containers) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_end(cv, containers);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_writev(B1Message *message, const char *signature, va_list args) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_writev(cv, signature, args);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_insert(B1Message *message, const char *type, const struct iovec *vecs, size_t n_vecs) {
        CVariant *cv = NULL;

        if (message && !b1_message_is_notification(message))
                cv = message->data.cv;

        return c_variant_insert(cv, type, vecs, n_vecs);
}

/**
 * XXX: see CVariant
 */
_c_public_ int b1_message_seal(B1Message *message) {
        CVariant *cv;
        int r;

        if (!message || b1_message_is_notification(message))
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

        if (!message || b1_message_is_notification(message))
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

        if (!message || b1_message_is_notification(message))
                return -EINVAL;

        if (index >= message->data.n_fds)
                return -ERANGE;

        *fdp = message->data.fds[index];

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
