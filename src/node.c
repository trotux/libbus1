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
#include "interface.h"
#include "linux/bus1.h"
#include "node.h"
#include "peer.h"
#include <stdlib.h>
#include <string.h>

typedef struct B1Implementation {
        CRBNode rb;
        B1Interface *interface;
} B1Implementation;

int nodes_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Node *node = c_container_of(n, B1Node, rb);
        uint64_t id = *(uint64_t*)k;

        if (id < node->id)
                return -1;
        else if (id > node->id)
                return 1;
        else
                return 0;
}

int root_nodes_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Node *node = c_container_of(n, B1Node, rb);
        const char *name = k;

        return strcmp(node->name, name);
}

int handles_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Handle *handle = c_container_of(n, B1Handle, rb);
        uint64_t id = *(uint64_t*)k;

        if (id < handle->id)
                return -1;
        else if (id > handle->id)
                return 1;
        else
                return 0;
}

int b1_node_link(B1Node *node) {
        CRBNode **slot, *p;

        assert(node);
        assert(node->id != BUS1_HANDLE_INVALID);
        assert(node->owner);

        slot = c_rbtree_find_slot(&node->owner->nodes,
                                  nodes_compare, &node->id, &p);
        if (!slot)
                return -ENOTUNIQ;

        c_rbtree_add(&node->owner->nodes, p, slot, &node->rb);

        return 0;

}

int b1_handle_link(B1Handle *handle) {
        CRBNode **slot, *p;

        assert(handle);
        assert(handle->id != BUS1_HANDLE_INVALID);
        assert(handle->holder);

        slot = c_rbtree_find_slot(&handle->holder->handles,
                                  handles_compare, &handle->id, &p);
        if (!slot)
                return -ENOTUNIQ;

        c_rbtree_add(&handle->holder->handles, p, slot, &handle->rb);

        return 0;

}

int b1_handle_new(B1Peer *peer, uint64_t id, B1Handle **handlep) {
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;

        assert(peer);
        assert(handlep);

        handle = calloc(1, sizeof(*handle));
        if (!handle)
                return -ENOMEM;

        handle->n_ref = 1;
        handle->holder = b1_peer_ref(peer);
        handle->id = id;
        handle->marked = false;
        c_rbnode_init(&handle->rb);
        c_list_entry_init(&handle->multicast_group_le);

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

int b1_handle_acquire(B1Handle **handlep, B1Peer *peer, uint64_t handle_id) {
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
                r = b1_handle_new(peer, handle_id, &handle);
                if (r < 0)
                        return r;

                c_rbtree_add(&peer->handles, p, slot, &handle->rb);
        } else {
                handle = c_container_of(p, B1Handle, rb);
                b1_handle_ref(handle);
                b1_handle_release(handle);
        }

        *handlep = handle;

        return 0;
}

int b1_node_new_internal(B1Peer *peer, B1Node **nodep, void *userdata, uint64_t id, const char *name) {
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        size_t n_name;

        assert(peer);
        assert(nodep);

        n_name = name ? strlen(name) + 1 : 0;
        node = calloc(1, sizeof(*node) + n_name);
        if (!node)
                return -ENOMEM;
        if (name)
                node->name = memcpy((void*)(node + 1), name, n_name);

        node->id = id;
        node->owner = b1_peer_ref(peer);
        node->userdata = userdata;
        node->live = false;

        *nodep = node;
        node = NULL;
        return 0;
}

static int implementations_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Implementation *implementation = c_container_of(n, B1Implementation,
                                                          rb);
        const char *name = k;

        return strcmp(name, implementation->interface->name);
}

B1Interface *b1_node_get_interface(B1Node *node, const char *name) {
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

        r = b1_node_new_internal(peer, &node, userdata, BUS1_HANDLE_INVALID, NULL);
        if (r < 0)
                return r;

        r = b1_handle_new(peer, BUS1_HANDLE_INVALID, &node->handle);
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

        assert(node->owner);

        b1_node_release(node);

        while ((n = c_rbtree_first(&node->implementations))) {
                B1Implementation *implementation = c_container_of(n, B1Implementation, rb);

                c_rbtree_remove(&node->implementations, n);
                b1_interface_unref(implementation->interface);
                free(implementation);
        }

        /* if the node name is set, it means this node is owned by a message or
         * peer object, which will be responsibly for cleaning it up */
        if (!node->name && node->id != BUS1_HANDLE_INVALID) {
                b1_node_destroy(node);
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

        implementation = calloc(1, sizeof(*implementation));
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

        assert(!c_list_entry_is_linked(&handle->multicast_group_le));
        assert(!handle->multicast_group_notification);

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
 * b1_notification_slot_free() - unregister and free notification slot
 * @slot:               a notification slot, or NULL
 *
 * Return: NULL.
 */
_c_public_ B1NotificationSlot *b1_notification_slot_free(B1NotificationSlot *slot) {
        if (!slot)
                return NULL;

        c_list_remove(&slot->handle->notification_slots, &slot->le);

        free(slot);

        return NULL;
}

/**
 * b1_notification_slot_get_userdata() - get userdata from notification slot
 * @slot:               a notification slot
 *
 * Retrurn: the userdata.
 */
_c_public_ void *b1_notification_slot_get_userdata(B1NotificationSlot *slot) {
        if (!slot)
                return NULL;

        return slot->userdata;
}

/**
 * b1_handle_subscribe() - monitor handle for events
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
_c_public_ int b1_handle_monitor(B1Handle *handle, B1NotificationSlot **slotp, B1NotificationFn fn, void *userdata) {
        _c_cleanup_(b1_notification_slot_freep) B1NotificationSlot *slot = NULL;

        assert(slotp);
        assert(fn);

        slot = calloc(1, sizeof(*slot));
        if (!slot)
                return -ENOMEM;

        slot->fn = fn;
        slot->userdata = userdata;
        c_list_entry_init(&slot->le);

        c_list_append(&handle->notification_slots, &slot->le);

        *slotp = slot;
        slot = NULL;
        return 0;
}

int b1_notification_dispatch(B1NotificationSlot *s) {
        assert(s);
        assert(s->fn);

        return s->fn(s, s->userdata, s->handle);
}
