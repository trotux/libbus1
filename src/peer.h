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
#include "bus1-peer.h"
#include "org.bus1/b1-peer.h"

struct B1Peer {
        _Atomic unsigned long ref;

        struct bus1_peer *peer;

        CRBTree nodes;
        CRBTree handles;
};
