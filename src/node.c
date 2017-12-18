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
#include <errno.h>
#include "linux/bus1.h"
#include "node.h"
#include "peer.h"
#include <stdlib.h>
#include <string.h>

static int nodes_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Node *node = c_container_of(n, B1Node, rb_nodes);
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

int b1_node_link(B1Node *node, uint64_t id) {
        CRBNode **slot, *p;

        assert(node);
        assert(node->id == BUS1_HANDLE_INVALID);
        assert(id != BUS1_HANDLE_INVALID);
        assert(node->owner);

        slot = c_rbtree_find_slot(&node->owner->nodes, nodes_compare, &id, &p);
        if (!slot)
                return -ENOTUNIQ;

        node->id = id;

        c_rbtree_add(&node->owner->nodes, p, slot, &node->rb_nodes);

        return 0;
}

int b1_handle_link(B1Handle *handle, uint64_t id) {
        CRBNode **slot, *p;

        assert(handle);
        assert(handle->id == BUS1_HANDLE_INVALID);
        assert(id != BUS1_HANDLE_INVALID);
        assert(handle->holder);

        slot = c_rbtree_find_slot(&handle->holder->handles, handles_compare, &id, &p);
        if (!slot)
                return -ENOTUNIQ;

        handle->id = id;

        c_rbtree_add(&handle->holder->handles, p, slot, &handle->rb);

        return 0;
}

static int b1_handle_new(B1Peer *peer, B1Handle **handlep) {
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;

        assert(peer);
        assert(handlep);

        handle = calloc(1, sizeof(*handle));
        if (!handle)
                return -ENOMEM;

        handle->ref = C_REF_INIT;
        handle->holder = b1_peer_ref(peer);
        handle->id = BUS1_HANDLE_INVALID;
        handle->marked = false;
        handle->live = false;
        c_rbnode_init(&handle->rb);

        *handlep = handle;
        handle = NULL;
        return 0;
}

int b1_handle_acquire(B1Peer *peer, B1Handle **handlep, uint64_t handle_id) {
        B1Handle *handle;
        CRBNode **slot, *p;
        int r;

        assert(peer);
        assert(handlep);

        if (handle_id == BUS1_HANDLE_INVALID) {
                *handlep = NULL;
                return 0;
        }

        slot = c_rbtree_find_slot(&peer->handles, handles_compare, &handle_id, &p);
        if (slot) {
                r = b1_handle_new(peer, &handle);
                if (r < 0)
                        return r;

                handle->ref_kernel = C_REF_INIT;
                handle->live = true;
                handle->id = handle_id;

                c_rbtree_add(&peer->handles, p, slot, &handle->rb);
        } else {
                handle = c_container_of(p, B1Handle, rb);
                if (handle->live) {
                        c_ref_inc(&handle->ref_kernel);
                        /* reusing existing handle, drop redundant reference from kernel */
                        r = bus1_peer_handle_release(handle->holder->peer, handle->id);
                        if (r < 0)
                                return r;
                } else {
                        handle->ref_kernel = C_REF_INIT;
                        handle->live = true;
                }
                c_ref_inc(&handle->ref);
        }

        *handlep = handle;
        return 0;
}

/**
 * b1_node_new() - create a new node for a peer
 * @peer:               the owning peer
 * @nodep:              pointer to the new node object
 *
 * Return: 0 on success, and a negative error code on failure.
 */
_c_public_ int b1_node_new(B1Peer *peer, B1Node **nodep) {
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        int r;

        node = calloc(1, sizeof(*node));
        if (!node)
                return -ENOMEM;

        node->id = BUS1_HANDLE_INVALID;
        node->owner = b1_peer_ref(peer);
        c_rbnode_init(&node->rb_nodes);

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
        if (!node)
                return NULL;

//        c_rbtree_remove_init(&node->owner->nodes, &node->rb_nodes);
        c_rbnode_unlink(&node->rb_nodes);
        b1_node_destroy(node);

        b1_handle_unref(node->handle);
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
        return node->owner;
}

/**
 * b1_node_get_handle() - get owner handle of the node
 * @node:               node to query
 *
 * This gets the owner's handle of a node.
 *
 * Return: Pointer to owner's handle.
 */
_c_public_ B1Handle *b1_node_get_handle(B1Node *node) {
        return node->handle;
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
 *
 * Return: 0 on success, and a negative error code on failure.
 */
_c_public_ int b1_node_destroy(B1Node *node) {
        struct bus1_cmd_nodes_destroy nodes_destroy = {
                .ptr_nodes = (uintptr_t)&node->id,
                .n_nodes = 1,
        };

        if (!node)
                return 0;

        return bus1_peer_nodes_destroy(node->owner->peer, &nodes_destroy);
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
        B1Handle *new_handle;
        int r;

        if (handle) {
                if (!handle->live) {
                        r = b1_handle_transfer(handle, handle->holder, &new_handle);
                        assert(r >= 0);
                        assert(new_handle == handle);
                } else {
                        c_ref_inc(&handle->ref_kernel);
                        c_ref_inc(&handle->ref);
                }
        }

        return handle;
}

static void b1_handle_release(_Atomic unsigned long *ref, void *userdata) {
        B1Handle *handle = userdata;
        int r;

        handle->live = false;
        r = bus1_peer_handle_release(handle->holder->peer, handle->id);
        assert(r >= 0);
}

static void b1_handle_free(_Atomic unsigned long *ref, void *userdata) {
        B1Handle *handle = userdata;

        assert(!handle->live);

//        c_rbtree_remove_init(&handle->holder->handles, &handle->rb);
        c_rbnode_unlink(&handle->rb);

        b1_peer_unref(handle->holder);
        free(handle);
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
        if (handle) {
                if (handle->live)
                        c_ref_dec(&handle->ref_kernel, b1_handle_release, handle);
                c_ref_dec(&handle->ref, b1_handle_free, handle);
        }

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
        return handle->holder;
}

B1Node *b1_node_lookup(B1Peer *peer, uint64_t node_id) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->nodes, nodes_compare, &node_id);
        if (!n)
                return NULL;

        return c_container_of(n, B1Node, rb_nodes);
}

B1Handle *b1_handle_lookup(B1Peer *peer, uint64_t handle_id) {
        CRBNode *n;

        assert(peer);

        n = c_rbtree_find_node(&peer->handles, handles_compare, &handle_id);
        if (!n)
                return NULL;

        return c_container_of(n, B1Handle, rb);
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

        r = bus1_peer_handle_transfer(src_handle->holder->peer, dst->peer, &src_handle_id, &dst_handle_id);
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
