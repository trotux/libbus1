/*
 * Copyright (C) 2013-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 */

#include <stdatomic.h>
#include <c-rbtree.h>
#include <c-ref.h>
#include "org.bus1/b1-peer.h"

struct B1Handle {
        _Atomic unsigned long ref;
        _Atomic unsigned long ref_kernel;

        B1Peer *holder;
        B1Node *node;
        uint64_t id;

        bool live; /* holds a reference in the kernel */
        bool marked; /* used for duplicate detection */

        CRBNode rb;
};

struct B1Node {
        B1Peer *owner;
        B1Handle *handle;
        uint64_t id;

        CRBNode rb_nodes;
};

int b1_handle_acquire(B1Peer *peer, B1Handle **handlep, uint64_t handle_id);
int b1_handle_link(B1Handle *handle, uint64_t id);
B1Handle *b1_handle_lookup(B1Peer *peer, uint64_t id);

int b1_node_link(B1Node *node, uint64_t id);
B1Node *b1_node_lookup(B1Peer *peer, uint64_t id);
