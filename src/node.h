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
#include <c-list.h>
#include "org.bus1/b1-peer.h"

struct B1Handle {
        unsigned long n_ref;

        B1Peer *holder;
        B1Node *node;
        uint64_t id;

        bool marked; /* used for duplicate detection */

        CList notification_slots;

        CRBNode rb;

        B1NotificationSlot *multicast_group_notification;
        CListEntry multicast_group_le;
};

struct B1Node {
        B1Peer *owner;
        B1Handle *handle;
        uint64_t id;
        const char *name;
        void *userdata;

        bool live:1;
        bool persistent:1;

        CRBNode rb; /* used either to link into nodes map or root_nodes map */

        CRBTree implementations;
        B1ReplySlot *slot;
        B1NodeFn destroy_fn;
};

struct B1NotificationSlot {
        CListEntry le;

        B1Handle *handle;

        B1NotificationFn fn;
        void *userdata;
};

int root_nodes_compare(CRBTree *t, void *k, CRBNode *n);
int handles_compare(CRBTree *t, void *k, CRBNode *n);
int nodes_compare(CRBTree *t, void *k, CRBNode *n);

int b1_handle_acquire(B1Handle **handlep, B1Peer *peer, uint64_t handle_id);
int b1_handle_new(B1Peer *peer, uint64_t id, B1Handle **handlep);
int b1_handle_link(B1Handle *handle);

int b1_node_new_internal(B1Peer *peer, B1Node **nodep, void *userdata, uint64_t id, const char *name);
int b1_node_link(B1Node *node);

B1Interface *b1_node_get_interface(B1Node *node, const char *name);

int b1_notification_dispatch(B1NotificationSlot *s);
