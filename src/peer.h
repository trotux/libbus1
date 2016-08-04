/*
 * This file is part of bus1. See COPYING for details.
 *
 * bus1 is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

#include <c-rbtree.h>
#include "bus1-client.h"
#include "org.bus1/b1-peer.h"

struct B1Peer {
        unsigned long n_ref;

        struct bus1_client *client;

        CRBTree nodes;
        CRBTree handles;
};
