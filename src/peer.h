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

#include <c-rbtree.h>
#include "bus1-client.h"
#include "org.bus1/b1-peer.h"

struct B1Peer {
        unsigned long n_ref;

        struct bus1_client *client;

        CRBTree nodes;
        CRBTree handles;
        CRBTree root_nodes;
};

B1Node *b1_peer_get_node(B1Peer *peer, uint64_t node_id);
B1Handle *b1_peer_get_handle(B1Peer *peer, uint64_t handle_id);
B1Node *b1_peer_get_root_node(B1Peer *peer, const char *name);
