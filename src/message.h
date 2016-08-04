/*
 * This file is part of bus1. See COPYING for details.
 *
 * bus1 is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include "org.bus1/b1-peer.h"

struct B1Message {
        unsigned long n_ref;
        B1Peer *peer;
        const void *slice; /* NULL if not backed by a slice */

        uint64_t type; /* BUS1_MSG_* */

        uint64_t destination;
        uid_t uid;
        gid_t gid;
        pid_t pid;
        pid_t tid;

        /* each of the following arrays are owned by the message */
        struct iovec *vecs; /* message does not own the backing data */
        size_t n_vecs;
        B1Handle **handles; /* message owns a ref to each handle */
        size_t n_handles;
        int *fds; /* message owns each fd */
        size_t n_fds;
};

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
                              size_t n_fds);
