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
#include <errno.h>
#include <string.h>
#include "org.bus1/b1-peer.h"
#include "org.bus1/c-rbtree.h"
#include "org.bus1/c-variant.h"
#include "org.bus1/c-macro.h"
#include "bus1-client.h"

typedef struct B1Member {
        CRBNode rb;
        char *name;
        B1NodeFn fn;
} B1Member;

typedef struct B1Implementation {
        CRBNode rb;
        B1Interface *interface;
} B1Implementation;

struct B1Handle {
        unsigned n_ref;

        B1Peer *holder;
        uint64_t id;
};

struct B1Interface {
        unsigned n_ref;

        char *name;

        CRBTree members;
};

struct B1Message {
        unsigned int n_ref;
        unsigned int type;

        B1Node *destination;
        B1Handle *reply_handle;
        uint32_t uid;
        uint32_t gid;
        uint32_t pid;
        uint32_t tid;

        B1Handle **handles;
        size_t n_handles;
        int *fds;
        size_t n_fds;

        CVariant *cv;
};

struct B1Node {
        B1Peer *owner;
        B1Handle *handle;
        uint64_t id;
        void *userdata;

        CRBTree implementations;
};

struct B1Peer {
        unsigned n_ref;

        struct bus1_client *client;
};

struct B1Slot {
};

#define BUS1_DEFAULT_POOL_SIZE (1024 * 1024 * 16)
#define _cleanup_(_x) __attribute__((__cleanup__(_x)))

/**
 * b1_peer_new() - creates a new disconnected peer
 * @peerp:              the new peer object
 * @path:               the path to the bus1 character device, or NULL
 *
 * Create a new peer disconnected from all existing peers.
 *
 * Return: 0 on success, a negative error code on failure.
 */
int b1_peer_new(B1Peer **peerp, const char *path)
{
        _cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        int r;

        assert(peerp);

        peer = malloc(sizeof(*peer));
        if (!peer)
                return -ENOMEM;

        peer->n_ref = 1;

        r = bus1_client_new_from_path(&peer->client, path);
        if (r < 0)
                return r;

        r = bus1_client_init(peer->client, BUS1_DEFAULT_POOL_SIZE);
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
int b1_peer_new_from_fd(B1Peer **peerp, int fd)
{
        _cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        int r;

        assert(peerp);

        peer = malloc(sizeof(*peer));
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
B1Peer *b1_peer_ref(B1Peer *peer)
{
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
B1Peer *b1_peer_unref(B1Peer *peer)
{
        if (!peer)
                return NULL;

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
int b1_peer_get_fd(B1Peer *peer)
{
        assert(peer);

        return bus1_client_get_fd(peer->client);
}

/**
 * b1_peer_send() - send a message to the given handles
 * @peer                the sending peer
 * @handles             the destination handles
 * @n_handles           the number of handles
 * @message             the message to be sent
 *
 * Return: 0 on succes, or a negative error code on failure.
 */
int b1_peer_send(B1Peer *peer, B1Handle **handles, size_t n_handles,
                 B1Message *message)
{
        /* XXX: limit number of handles, and support handle and fd passing */
        uint64_t destinations[n_handles];
        const struct iovec *vecs;
        size_t n_vecs;
        int r;

        assert(peer);

        if (!message || !b1_message_is_sealed(message))
                return -EINVAL;

        for (unsigned int i = 0; i < n_handles; i++)
                destinations[i] = handles[i]->id;

        vecs = c_variant_get_vecs(message->cv, &n_vecs);

        r = bus1_client_send(peer->client,
                             destinations, n_handles,
                             vecs, n_vecs,
                             NULL, 0,
                             NULL, 0);
        if (r < 0)
                return r;

        return 0;
}

static int b1_message_new_from_slice(B1Message **messagep,
                                     void *slice, size_t n_bytes)
{
        _cleanup_(b1_message_unrefp) B1Message *message = NULL;
        const struct iovec vec = {
                .iov_base = slice,
                .iov_len = n_bytes,
        };
        int r;

        assert(messagep);

        message = malloc(sizeof(*message));
        if (!message)
                return -ENOMEM;

        r = c_variant_new_from_vecs(&message->cv,
                                    "tvv", strlen("tvv"),
                                    &vec, 1);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

static int b1_handle_new(B1Handle **handlep, B1Peer *peer)
{
        B1Handle *handle;

        assert(handlep);
        assert(peer);

        /* XXX: add handle map to the peer object */

        handle = malloc(sizeof(*handle));
        if (!handle)
                return -ENOMEM;

        handle->n_ref = 1;
        handle->holder = peer;
        handle->id = BUS1_HANDLE_INVALID;

        *handlep = handle;

        return 0;
}

static int b1_peer_recv_data(B1Peer *peer, struct bus1_msg_data *data,
                             B1Message **messagep)
{
        _cleanup_(b1_message_unrefp) B1Message *message = NULL;
        void *slice;
        uint64_t *handle_ids;
        int r;

        assert(peer);
        assert(data);
        assert(messagep);

        slice = bus1_client_slice_from_offset(peer->client, data->offset);

        r = b1_message_new_from_slice(&message, slice, data->n_bytes);
        if (r < 0)
                return r;

        /* XXX: get destination node */
        message->uid = data->uid;
        message->gid = data->gid;
        message->pid = data->pid;
        message->tid = data->tid;
        handle_ids =
                (uint64_t*)((uint8_t*)slice + c_align_to(data->n_bytes, 8));
        message->n_handles = data->n_handles;
        message->fds = (int*)(handle_ids + message->n_handles);
        message->n_fds = data->n_fds;
        message->handles = calloc(data->n_handles, sizeof(*message->handles));

        for (unsigned int i = 0; i < data->n_handles; i++) {
                r = b1_handle_new(&message->handles[i], peer);
                if (r < 0)
                        return r;

                message->handles[i]->id = handle_ids[i];
        }

        r = c_variant_read(message->cv, "t", message->type);
        if (r < 0)
                return r;

        return 0;
}

static int b1_peer_recv_node_destroy(B1Peer *peer,
                                     struct bus1_msg_node_destroy *node_destroy,
                                     B1Message **messagep)
{
        /* XXX */
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
int b1_peer_recv(B1Peer *peer, B1Message **messagep)
{
        struct bus1_cmd_recv recv = {};
        int r;

        assert(peer);

        r = bus1_client_ioctl(peer->client, BUS1_CMD_SEND, &recv);
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
int b1_peer_clone(B1Peer *peer, B1Node **nodep, B1Handle **handlep)
{
        return 0;
}

/**
 * b1_slot_free() - unregister and free slot
 * @slot:               a slot, or NULL
 *
 * Return: NULL.
 */
B1Slot *b1_slot_free(B1Slot *slot)
{
        return NULL;
}

/**
 * b1_slot_get_userdata() - get userdata from slot
 * @slot:               a slot
 *
 * Retrurn: the userdata.
 */
void *b1_slot_get_userdata(B1Slot *slot)
{
        return NULL;
}

static int b1_message_new(B1Message **messagep, unsigned int type)
{
        _cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        message = malloc(sizeof(*message));
        if (!message)
                return -ENOMEM;

        message->type = type;
        message->destination = NULL;
        message->reply_handle = NULL;
        message->uid = -1;
        message->gid = -1;
        message->pid = -1;
        message->tid = -1;
        message->handles = NULL;
        message->n_handles = 0;
        message->fds = NULL;
        message->n_fds = 0;
        message->cv = NULL;

        /* <type, header variant, payload variant> */
        r = c_variant_new(&message->cv, "tvv", strlen("tvv"));
        if (r < 0)
                return r;

        r = c_variant_write(message->cv, "t", type);
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
 * @slotp:              pointer to a new reply object, or NULL
 * @fn:                 the reply handler, or NULL
 * @userdata:           the userdata to pass to the reply handler, or NULL
 *
 * All methods are namespaced by an interface name, and may optionally expect a 
 * response. If a response is expected, a new B1Slot object is created and @fn
 * is called on @userdata when the response is received.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int b1_message_new_call(B1Message **messagep,
                        const char *interface,
                        const char *member,
                        B1Slot **slotp,
                        B1SlotFn fn,
                        void *userdata)
{
        _cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new(&message, B1_MESSAGE_TYPE_CALL);
        if (r < 0)
                return r;

        /* <interface, member, reply handle id> */
        r = c_variant_write(message->cv, "v", "sst", interface, member, -1);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

/**
 * b1_message_new_reply() - create a new method reply
 * @messagep:           the new message object
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
int b1_message_new_reply(B1Message **messagep,
                         B1Slot **slotp,
                         B1SlotFn fn,
                         void *userdata)
{
        _cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new(&message, B1_MESSAGE_TYPE_REPLY);
        if (r < 0)
                return r;

        /* <reply handle id> */
        r = c_variant_write(message->cv, "v", "t", -1);
        if (r < 0)
                return r;

        *messagep = message;
        message = NULL;

        return 0;
}

/**
 * b1_message_new_error() - create a new method error reply
 * @messagep:           the new message object
 *
 * An error reply to a message can not receive a reply, otherwise it is exactly
 * like any other method reply.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int b1_message_new_error(B1Message **messagep)
{
        _cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new(&message, B1_MESSAGE_TYPE_ERROR);
        if (r < 0)
                return r;

        /* <> */
        r = c_variant_write(message->cv, "v", "");
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
B1Message *b1_message_ref(B1Message *message)
{
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
B1Message *b1_message_unref(B1Message *message)
{
        if (!message)
                return NULL;

        if (--message->n_ref > 0)
                return NULL;

        for (unsigned int i = 0; i < message->n_handles; i++)
                b1_handle_unref(message->handles[i]);

        for (unsigned int i = 0; i < message->n_fds; i++)
                close(message->fds[i]);

        c_variant_free(message->cv);

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
bool b1_message_is_sealed(B1Message *message)
{
        return c_variant_is_sealed(message ? message->cv : NULL);
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
unsigned int b1_message_get_type(B1Message *message)
{
        if (!message)
                return _B1_MESSAGE_TYPE_INVALID;

        return message->type;
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
int b1_message_dispatch(B1Message *message)
{
        return 0;
}

/**
 * b1_message_get_destination_node() - get destination node of received message
 * @message:             the message
 *
 * Every message a peer receives is destined for a node. Determine the node for
 * a given message.
 *
 * Return: The node, or NULL.
 */
B1Node *b1_message_get_destination_node(B1Message *message)
{
        if (!message)
                return NULL;

        return message->destination;
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
B1Handle *b1_message_get_reply_handle(B1Message *message)
{
        if (!message)
                return NULL;

        return message->reply_handle;
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
uid_t b1_message_get_uid(B1Message *message)
{
        if (!message)
                return -1;

        return message->uid;
}

/**
 * b1_message_get_gid() - get gid of sending peer
 * @message:            the received message
 *
 * Return: the gid
 */
gid_t b1_message_get_gid(B1Message *message)
{
        if (!message)
                return -1;

        return message->gid;
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
pid_t b1_message_get_pid(B1Message *message)
{
        if (!message)
                return -1;

        return message->pid;
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
pid_t b1_message_get_tid(B1Message *message)
{
        if (!message)
                return -1;

        return message->tid;
}

/**
 * XXX: see CVariant
 */
size_t b1_message_peek_count(B1Message *message)
{
        return c_variant_peek_count(message ? message->cv : NULL);
}

/**
 * XXX: see CVariant
 */
const char *b1_message_peek_type(B1Message *message, size_t *sizep)
{
        return c_variant_peek_type(message ? message->cv : NULL, sizep);
}

/**
 * XXX: see CVariant
 */
int b1_message_enter(B1Message *message, const char *containers)
{
        return c_variant_enter(message ? message->cv : NULL, containers);
}

/**
 * XXX: see CVariant
 */
int b1_message_exit(B1Message *message, const char *containers)
{
        return c_variant_exit(message ? message->cv : NULL, containers);
}

/**
 * XXX: see CVariant
 */
int b1_message_readv(B1Message *message, const char *signature, va_list args)
{
        return c_variant_readv(message ? message->cv : NULL, signature, args);
}

/**
 * XXX: see CVariant
 */
void b1_message_rewind(B1Message *message)
{
        return c_variant_rewind(message ? message->cv : NULL);
}

/**
 * XXX: see CVariant
 */
int b1_message_beginv(B1Message *message, const char *containers, va_list args)
{
        return c_variant_beginv(message ? message->cv : NULL, containers, args);
}

/**
 * XXX: see CVariant
 */
int b1_message_end(B1Message *message, const char *containers)
{
        return c_variant_end(message ? message->cv : NULL, containers);
}

/**
 * XXX: see CVariant
 */
int b1_message_writev(B1Message *message, const char *signature, va_list args)
{
        return c_variant_writev(message ? message->cv : NULL, signature, args);
}

/**
 * XXX: see CVariant
 */
int b1_message_seal(B1Message *message)
{
        return c_variant_seal(message ? message->cv : NULL);
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
int b1_message_get_handle(B1Message *message, unsigned int index,
                          B1Handle **handlep)
{
        assert(handlep);

        if (!message)
                return -EINVAL;

        if (index >= message->n_handles)
                return -ERANGE;

        *handlep = message->handles[index];

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
int b1_message_get_fd(B1Message *message, unsigned int index, int *fdp)
{
        assert(fdp);

        if (!message)
                return -EINVAL;

        if (index >= message->n_fds)
                return -ERANGE;

        *fdp = message->fds[index];

        return 0;
}

/**
 * b1_node_new() - create a new node for a peer
 * @nodep:              pointer to the new node object
 * @peer:               the owning peer
 * @userdata:           userdata to associate with the node
 *
 * A node is the recipient of messages, and are always associated with a peer.
 * Nodes are allocated lazily in the kernel, so it is not guaranteed to be a
 * kernel equivalent to the userspace object at all times.
 *
 * Return: 0 on success, and a negative error code on failure.
 */
int b1_node_new(B1Node **nodep, B1Peer *peer, void *userdata)
{
        _cleanup_(b1_node_freep) B1Node *node = NULL;
        int r;

        assert(nodep);
        assert(peer);

        /* XXX: add node map to the peer object */

        node = malloc(sizeof(*node));
        if (!node)
                return -ENOMEM;

        node->id = BUS1_HANDLE_INVALID;
        node->owner = peer;
        node->userdata = userdata;
        node->handle = NULL;

        r = b1_handle_new(&node->handle, peer);
        if (r < 0)
                return r;

        *nodep = node;
        node = NULL;

        return 0;
}

/**
 * b1_node_free() - unregister the node and free the associated resources
 * @node:               the node to operate on
 *
 * This is typically called when a node destruction notification has been
 * receied from the kernel, indicating that the node can not be used any more.
 * However, if this is called when the node is still in use, it is dropped from
 * the kernel, and any future messages destined for the node are silently
 * discarded.
 *
 * Return: NULL.
 */
B1Node *b1_node_free(B1Node *node)
{
        CRBNode *n;

        if (!node)
                return NULL;

        b1_handle_unref(node->handle);
        b1_node_destroy(node);

        while ((n = c_rbtree_first(&node->implementations))) {
                B1Implementation *implementation =
                                c_container_of(n, B1Implementation, rb);

                c_rbtree_remove(&node->implementations, n);

                b1_interface_unref(implementation->interface);
                free(implementation);
        }

        free(node);

        return NULL;
}

/**
 * b1_node_get_peer() - get owning peer
 * @node:               the node
 *
 * Return: the peer owning @node
 */
B1Peer *b1_node_get_peer(B1Node *node)
{
        if (!node)
                return NULL;

        return node->owner;
}

/**
 * b1_node_get_peer() - get handle referring to node
 * @node:               the node
 *
 * The owner of a node, will typically hold a handle to that node, unless it has
 * been released.
 *
 * Return: the handle, or NULL if it has been released.
 */
B1Handle *b1_node_get_handle(B1Node *node)
{
        if (!node)
                return NULL;

        return node->handle;
}

/**
 * b1_node_get_userdata() - get userdata associatde with a node
 * @node:               the node
 *
 * Every node may have userdata associated with it.
 *
 * Return: the userdata.
 */
void *b1_node_get_userdata(B1Node *node)
{
        if (!node)
                return NULL;

        return node->userdata;
}

/**
 * b1_node_set_destroy_fn() - set function to call when node is destroyed
 * @node:               the node
 * @fn:                 the function to call
 *
 * A node destruction notification is recevied when a node is destoryed, this
 * function is then called to allow resources to be cleaned up.
 */
void b1_node_set_destroy_fn(B1Node *node, B1NodeFn fn)
{
}

static int implementations_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Implementation *implementation = c_container_of(n, B1Implementation,
                                                          rb);
        const char *name = k;

        return strcmp(name, implementation->interface->name);
}

/**
 * b1_node_implement() - implement interface on node
 * @node:               the node
 * @interface:          an interface to implement
 *
 * A node can handle method calls by implementing an interface. This associates
 * an interface with a node.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int b1_node_implement(B1Node *node, B1Interface *interface)
{
        B1Implementation *implementation;
        CRBNode **slot, *p;

        assert(node);
        assert(interface);

        slot = c_rbtree_find_slot(&node->implementations,
                                  implementations_compare, interface->name, &p);
        if (!slot)
                return -ENOTUNIQ;

        implementation = malloc(sizeof(*implementation));
        if (!implementation)
                return -ENOMEM;

        implementation->interface = b1_interface_ref(interface);

        c_rbtree_add(&node->implementations, p, slot, &implementation->rb);

        return 0;
}

/**
 * b1_node_release() - release handle to node
 * @node:               node to release
 *
 * When the owner of a node releases its handle to it, it means that the node
 * will be released as soon as no other peer is pinning a handle to it. It also
 * means that the owning peer can no longer hand out handles to the node to
 * other peers.
 */
void b1_node_release(B1Node *node)
{
        if (!node)
                return;

        node->handle = b1_handle_unref(node->handle);
}

/**
 * b1_node_destroy() - destroy node
 * @node:               node to destroy
 *
 * Destroy the node in the kernel, regardless of any handles held by other
 * peers. If any peers still hold handles, they will receive node destruction
 * notifications for this node.
 */
void b1_node_destroy(B1Node *node)
{
        if (!node)
                return;

        if (node->id == BUS1_HANDLE_INVALID)
                return;

        (void)bus1_client_node_destroy(node->owner->client, node->id);
}

/**
 * b1_handle_ref() - acquire reference
 * @handle:             handle to acquire reference to, or NULL
 *
 * Return: @handle is returned.
 */
B1Handle *b1_handle_ref(B1Handle *handle)
{
        if (!handle)
                return NULL;

        assert(handle->n_ref > 0);

        ++handle->n_ref;

        return handle;
}

static void b1_handle_release(B1Handle *handle)
{
        if (!handle)
                return;

        if (handle->id == BUS1_HANDLE_INVALID)
                return;

        (void)bus1_client_handle_release(handle->holder->client, handle->id);
}

/**
 * b1_handle_unref() - release reference
 * @handle:             handle to release reference to, or NULL
 *
 * Return: NULL is returned.
 */
B1Handle *b1_handle_unref(B1Handle *handle)
{
        if (!handle)
                return NULL;

        if (--handle->n_ref > 0)
                return NULL;

        b1_handle_release(handle);
        free(handle);

        return NULL;
}

/**
 * b1_handle_get_peer() - get holder of handle
 * @handle:             the handle
 *
 * Every handle has a holding peer.
 *
 * Return: the peer.
 */
B1Peer *b1_handle_get_peer(B1Handle *handle)
{
        if (!handle)
                return NULL;

        return handle->holder;
}

/**
 * b1_handle_subscribe() - subscribe to handle events
 * @handle:             the handle
 * @slotp:              pointer to handler object
 * @fn:                 handler function
 * @userdata:           userdata to be passed to the handler function
 *
 * When a node is destroyed, any holder of handles to that node receive node
 * destruction notifications. This registers a handler for such notifications to
 * clean up any possible allocated resources.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int b1_handle_subscribe(B1Handle *handle, B1Slot **slotp, B1SlotFn fn,
                        void *userdata)
{
        return 0;
}

/**
 * b1_interface_new() - create new interface
 * @interfacep:         pointer to new interface object
 * @name:               interface name
 *
 * An interface is a named collection of methods, that can be associated with
 * nodes. A method is invocked over the bus by makinga method call to a node and
 * supplying the interafce and method names.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int b1_interface_new(B1Interface **interfacep, const char *name)
{
        _cleanup_(b1_interface_unrefp) B1Interface *interface = NULL;

        assert(interfacep);
        assert(name);

        interface = malloc(sizeof(*interface));
        if (!interface)
                return -ENOMEM;

        interface->name = strdup(name);
        interface->members = (CRBTree){};

        *interfacep = interface;
        interface = NULL;

        return 0;
}

/**
 * b1_interface_ref() - acquire reference
 * @interface:          interface to acquire reference to, or NULL
 *
 * Return: @interface is returned.
 */
B1Interface *b1_interface_ref(B1Interface *interface)
{
        if (!interface)
                return NULL;

        assert(interface->n_ref > 0);

        ++interface->n_ref;

        return interface;
}

/**
 * b1_interface_unref() - release reference
 * @interface:          interface to release reference to, or NULL
 *
 * Return: NULL is returned.
 */
B1Interface *b1_interface_unref(B1Interface *interface)
{
        CRBNode *node;

        if (!interface)
                return NULL;

        if (--interface->n_ref > 0)
                return NULL;

        while ((node = c_rbtree_first(&interface->members))) {
                B1Member *member = c_container_of(node, B1Member, rb);

                c_rbtree_remove(&interface->members, node);

                free(member);
        }

        free(interface->name);
        free(interface);

        return NULL;
}

static int members_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Member *member = c_container_of(n, B1Member, rb);
        const char *name = k;

        return strcmp(name, member->name);
}

/**
 * b1_interface_add_member() - add member to interface
 * @interface:          interface to operate on
 * @name:               member name
 * @fn:                 function implmenting the member
 *
 * Return: 0 on succes, or a negative error code on failure.
 */
int b1_interface_add_member(B1Interface *interface, const char *name,
                            B1NodeFn fn)
{
        B1Member *member;
        CRBNode **slot, *p;

        assert(interface);
        assert(name);
        assert(fn);

        slot = c_rbtree_find_slot(&interface->members,
                                  members_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        member = malloc(sizeof(*member));
        if (!member)
                return -ENOMEM;

        member->name = strdup(name);
        if (!member->name) {
                free(member);
                return -ENOMEM;
        }

        c_rbtree_add(&interface->members, p, slot, &member->rb);

        return 0;
}

/**
 * b1_peer_reply() - send message in reply to messag
 * @peer:               sending peer
 * @origin:             message to reply to
 * @reply:              the reply
 *
 * For convenience, this allows a reply to be sent directly to the reply handle
 * of another message.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int b1_peer_reply(B1Peer *peer, B1Message *origin, B1Message *reply)
{
        return 0;
}
