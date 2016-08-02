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

/*
 * Basic API test
 * XXX
 */

#undef NDEBUG
#include <assert.h>
#include <c-macro.h>
#include <c-syscall.h>
#include <linux/bus1.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include "org.bus1/b1-peer.h"

static void test_peer(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer1 = NULL, *peer2 = NULL, *peer3 = NULL;
        int fd, r;

        /* create three peers: peer1 and peer2 are two instances of the same */
        r = b1_peer_new(&peer1);
        assert(r >= 0);
        assert(peer1);

        fd = b1_peer_get_fd(peer1);
        assert(fd >= 0);

        r = b1_peer_new_from_fd(&peer2, fd);
        assert(r >= 0);
        assert(peer2);

        r = b1_peer_new(&peer3);
        assert(r >= 0);
        assert(peer3);
}

static void test_node(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        B1Handle *handle;
        int r;

        r = b1_peer_new(&peer);
        assert(r >= 0);

        r = b1_node_new(peer, &node, NULL);
        assert(r >= 0);
        assert(node);

        assert(b1_node_get_peer(node) == peer);

        handle = b1_node_get_handle(node);
        assert(handle);

        handle = b1_handle_ref(handle);
        assert(handle);
        b1_handle_unref(handle);
}

static void test_handle(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;
        int r;

        r = b1_peer_new(&peer);
        assert(r >= 0);

        r = b1_node_new(peer, &node, NULL);
        assert(r >= 0);

        r = b1_handle_transfer(b1_node_get_handle(node), peer, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle == b1_node_get_handle(node));
}

static void test_message(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        B1Handle *handle;
        int r, fd;

        r = b1_peer_new(&peer);
        assert(r >= 0);

        r = b1_node_new(peer, &node, NULL);
        assert(r >= 0);

        r = b1_message_new(peer, &message);
        assert(r >= 0);
        assert(message);

        assert(b1_message_get_type(message) == BUS1_MSG_DATA);
        assert(!b1_message_get_destination_node(message));
        assert(!b1_message_get_destination_handle(message));
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);

        handle = b1_node_get_handle(node);

        r = b1_message_set_handles(message, &handle, 1);
        assert(r >= 0);

        r = b1_message_get_handle(message, 0, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle == b1_node_get_handle(node));

        fd = b1_peer_get_fd(peer);

        r = b1_message_set_fds(message, &fd, 1);
        assert(r >= 0);

        r = b1_message_get_fd(message, 0, &fd);
        assert(r >= 0);
        assert(fd >= 0);
        assert(fd != b1_peer_get_fd(peer));
}

static void test_transaction(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *src = NULL, *dst = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r, fd;

        r = b1_peer_new(&src);
        assert(r >= 0);

        r = b1_peer_new(&dst);
        assert(r >= 0);

        r = b1_node_new(dst, &node, NULL);
        assert(r >= 0);

        r = b1_handle_transfer(b1_node_get_handle(node), src, &handle);
        assert(r >= 0);

        r = b1_message_new(src, &message);
        assert(r >= 0);

        r = b1_message_set_handles(message, &handle, 1);
        assert(r >= 0);

        fd = eventfd(0, 0);
        assert(fd >= 0);

        r = b1_message_set_fds(message, &fd, 1);
        assert(r >= 0);

        assert(close(fd) >= 0);

        r = b1_message_send(message, &handle, 1);
        assert(r >= 0);

        message = b1_message_unref(message);
        assert(!message);
        handle = b1_handle_unref(handle);
        assert(!handle);

        r = b1_peer_recv(dst, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_DATA);
        assert(b1_message_get_destination_node(message) == node);
        assert(b1_message_get_uid(message) == getuid());
        assert(b1_message_get_gid(message) == getgid());
        assert(b1_message_get_pid(message) == getpid());
        assert(b1_message_get_tid(message) == c_syscall_gettid());
        r = b1_message_get_handle(message, 0, &handle);
        assert(r >= 0);
        assert(handle == b1_node_get_handle(node));
        r = b1_message_get_fd(message, 0, &fd);
        assert(r >= 0);
        assert(fd >= 0);
}

int main(int argc, char **argv) {
        if (access("/dev/bus1", F_OK) < 0 && errno == ENOENT)
                return 77;

        test_peer();
        test_node();
        test_handle();
        test_message();
        test_transaction();

        return 0;
}
