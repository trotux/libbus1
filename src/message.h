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
#include <c-variant.h>
#include <stdlib.h>
#include "org.bus1/b1-peer.h"

struct B1Message {
        unsigned long n_ref;
        uint64_t type;

        B1Peer *peer;
        union {
                struct {
                        uint64_t destination;
                        uid_t uid;
                        gid_t gid;
                        pid_t pid;
                        pid_t tid;

                        void *slice;

                        B1Handle **handles;
                        size_t n_handles;
                        int *fds;
                        size_t n_fds;

                        CVariant *cv;

                        union {
                                struct {
                                        const char *interface;
                                        const char *member;
                                        B1Handle *reply_handle;
                                } call;
                                struct {
                                        B1Handle *reply_handle;
                                } reply;
                                struct {
                                        const char *name;
                                } error;
                                struct {
                                        CRBTree root_nodes;
                                } seed;
                        };
                } data;
                struct {
                        uint64_t handle_id;
                } node_destroy;
        };
};

int b1_message_new_from_slice(B1Message **messagep, B1Peer *peer, void *slice, size_t n_bytes);
