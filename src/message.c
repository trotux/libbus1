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
#include "bus1-client.h"
#include "org.bus1/b1-peer.h"

static int b1_message_new_internal(B1Peer *peer, B1Message **messagep) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        message = calloc(1, sizeof(*message));
        if (!message)
                return -ENOMEM;

        message->n_ref = 1;
        message->peer = b1_peer_ref(peer);

        *messagep = message;
        message = NULL;
        return 0;
}

_c_public_ int b1_message_new(B1Peer *peer, B1Message **messagep) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new_internal(peer, &message);
        if (r < 0)
                return r;

        message->type = BUS1_MSG_DATA;

        message->destination = BUS1_HANDLE_INVALID;
        message->uid = -1;
        message->gid = -1;
        message->pid = 0;
        message->tid = 0;

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

static void b1_message_free_handles(B1Message *message) {
        for (unsigned int i = 0; i < message->n_handles; i++)
                b1_handle_unref(message->handles[i]);

        free(message->handles);
        message->handles = NULL;
}

static void b1_message_free_fds(B1Message *message) {
        for (unsigned int i = 0; i < message->n_fds; i++)
                if (message->fds[i] >= 0)
                        close(message->fds[i]);

        free(message->fds);
        message->fds = NULL;
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

        b1_message_free_handles(message);
        b1_message_free_fds(message);

        b1_peer_unref(message->peer);
        free(message);

        return NULL;
}

int b1_message_new_from_slice(B1Peer *peer,
                              B1Message **messagep,
                              const void *slice,
                              uint64_t type,
                              uint64_t destination,
                              uid_t uid,
                              gid_t gid,
                              pid_t pid,
                              pid_t tid,
                              size_t n_bytes,
                              size_t n_handles,
                              size_t n_fds) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        struct iovec *vec;
        uint64_t *handle_ids;
        int r;

        r = b1_message_new_internal(peer, &message);
        if (r < 0)
                return r;
        message->slice = slice;

        message->type = type;
        message->destination = destination;
        message->uid = uid;
        message->gid = gid;
        message->pid = pid;
        message->tid = tid;

        message->vecs = calloc(1, sizeof(*vec));
        if (!message->vecs)
                return -ENOMEM;

        message->vecs->iov_base = (void*)slice;
        message->vecs->iov_len = c_align_to(n_bytes, 8) +
                                 c_align_to(n_handles * sizeof(uint64_t), 8) +
                                 c_align_to(n_fds * sizeof(int), 8);
        message->n_vecs = 1;

        message->handles = calloc(n_handles, sizeof(B1Handle*));
        message->n_handles = n_handles;

        handle_ids = (uint64_t*)((uint8_t*)slice + c_align_to(n_bytes, 8));

        for (unsigned int i = 0; i < n_handles; i++) {
                B1Handle *handle;

                if (handle_ids[i] == BUS1_HANDLE_INVALID)
                        handle = NULL;
                else {
                        r = b1_handle_acquire(peer, &handle, handle_ids[i]);
                        if (r < 0)
                                return r;
                        else if (r == 0)
                                /* reusing an existing handle, drop ref from kernel */
                                b1_handle_release(handle);
                }

                message->handles[i] = handle;
        }

        message->fds = calloc(n_fds, sizeof(int));
        if (!message->fds)
                return -ENOMEM;
        memcpy(message->fds, (uint8_t*)handle_ids + c_align_to(n_handles, 8), n_fds * sizeof(int));

        *messagep = message;
        message = NULL;

        return 0;
}
/**
 * b1_message_send() - send a message to the given handles
 * @message             the message to be sent
 * @destinations        the destination handles
 * @n_destinations      the number of destinations
 *
 * Return: 0 on succes, or a negative error code on failure.
 */
_c_public_ int b1_message_send(B1Message *message,
                               B1Handle **destinations,
                               size_t n_destinations) {
        /* limit number of destinations? */
        uint64_t destination_ids[n_destinations];
        uint64_t *handle_ids;
        struct bus1_cmd_send send = {
                .ptr_destinations = n_destinations > 0 ? (uintptr_t)destination_ids : 0,
                .n_destinations = n_destinations,
        };
        int r;

        assert(!n_destinations || destinations);

        if (!message || message->type != BUS1_MSG_DATA)
                return -EINVAL;

        handle_ids = malloc(sizeof(uint64_t) * message->n_handles);
        if (!handle_ids)
                return -ENOMEM;

        send.ptr_vecs = (uintptr_t)message->vecs;
        send.n_vecs = message->n_vecs;
        send.ptr_handles = (uintptr_t)handle_ids;
        send.n_handles = message->n_handles;
        send.ptr_fds = (uintptr_t)message->fds;
        send.n_fds = message->n_fds;

        for (unsigned int i = 0; i < message->n_handles; i++) {
                B1Handle *handle = message->handles[i];

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

        for (unsigned int i = 0; i < n_destinations; i++) {
                if (destinations[i]->holder != message->peer) {
                        r = -EINVAL;
                        goto error;
                }

                destination_ids[i] = destinations[i]->id;
        }

        r = bus1_client_send(message->peer->client, &send);
        if (r < 0)
                goto error;

        for (unsigned int i = 0; i < message->n_handles; i++) {
                B1Handle *handle = message->handles[i];

                handle->marked = false;

                if (handle->id != BUS1_HANDLE_INVALID)
                        continue;

                assert(b1_handle_link(handle, handle_ids[i]) >= 0);

                if (handle->node)
                        assert(b1_node_link(handle->node, handle_ids[i]) >= 0);
        }

        free(handle_ids);

        return 0;

error:
        free(handle_ids);

        /* unmark handles */
        for (unsigned int i = 0; i < message->n_handles; i++)
                message->handles[i]->marked = false;

        return r;
}

_c_public_ int b1_message_set_handles(B1Message *message, B1Handle **handles, size_t n_handles) {
        B1Handle **handles_new;

        assert(!handles || n_handles);

        if (n_handles == 0) {
                b1_message_free_handles(message);
                return 0;
        }

        for (unsigned int i = 0; i < n_handles; i++) {
                if (message->peer != handles[i]->holder)
                        return -EINVAL;
        }

        handles_new = malloc(sizeof(*handles_new) * n_handles);
        if (!handles_new)
                return -ENOMEM;
        memcpy(handles_new, handles, sizeof(*handles) * n_handles);

        for (unsigned int i = 0; i < n_handles; i++)
                b1_handle_ref(handles[i]);

        b1_message_free_handles(message);
        message->handles = handles_new;
        message->n_handles = n_handles;

        return 0;
}

_c_public_ int b1_message_set_fds(B1Message *message, int *fds, size_t n_fds) {
        int *fds_new, r;

        assert(!fds || n_fds);

        if (n_fds == 0) {
                b1_message_free_fds(message);
                return 0;
        }

        fds_new = malloc(sizeof(*fds_new) * n_fds);
        if (!fds_new)
                return -ENOMEM;
        memset(fds_new, -1, sizeof(*fds_new) * n_fds);

        for (unsigned int i = 0; i < n_fds; i++) {
                fds_new[i] = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
                if (fds_new[i] == -1) {
                        r = -errno;
                        goto error;
                }
        }

        b1_message_free_fds(message);
        message->fds = fds_new;
        message->n_fds = n_fds;

        return 0;

error:
        for (unsigned int i = 0; i < n_fds; i++)
                if (fds_new[i] >= 0)
                        close(fds_new[i]);

        free(fds_new);
        return r;
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
                return BUS1_MSG_NONE;

        return message->type;
}

_c_public_ B1Node *b1_message_get_destination_node(B1Message *message) {
        if (message->type == BUS1_MSG_NODE_DESTROY)
                return NULL;

        return b1_node_lookup(message->peer, message->destination);
}

_c_public_ B1Handle *b1_message_get_destination_handle(B1Message *message) {
        if (message->type != BUS1_MSG_NODE_DESTROY)
                return NULL;

        return b1_handle_lookup(message->peer, message->destination);
}

/**
 * b1_message_get_uid() - get uid of sending peer
 * @message:            the received message
 *
 * Return: the uid
 */
_c_public_ uid_t b1_message_get_uid(B1Message *message) {
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
_c_public_ gid_t b1_message_get_gid(B1Message *message) {
        if (!message)
                return -1;

        return message->gid;
}

/**
 * b1_message_get_pid() - get pid of sending peer
 * @message:            the received message
 *
 * Return: the pid
 */
_c_public_ pid_t b1_message_get_pid(B1Message *message) {
        if (!message)
                return 0;

        return message->pid;
}

/**
 * b1_message_get_tid() - get tid of sending peer
 * @message:            the received message
 *
 * Return: the tid
 */
_c_public_ pid_t b1_message_get_tid(B1Message *message) {
        if (!message)
                return 0;

        return message->tid;
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
_c_public_ int b1_message_get_fd(B1Message *message, unsigned int index, int *fdp) {
        assert(fdp);

        if (!message)
                return -EINVAL;

        if (index >= message->n_fds)
                return -ERANGE;

        *fdp = message->fds[index];

        return 0;
}
