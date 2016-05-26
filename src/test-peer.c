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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <c-variant.h>
#include "org.bus1/b1-peer.h"

static bool done = false;

static int node_function(B1Node *node, void *userdata, B1Message *message)
{
        _c_cleanup_(b1_message_unrefp) B1Message *reply = NULL;
        B1Peer *peer = NULL;
        uint64_t num1 = 0;
        uint32_t num2 = 0;
        int r;

        fprintf(stderr, "PING!\n");

        r = b1_message_read(message, "(tu)", &num1, &num2);
        assert(r >= 0);
        assert(num1 = 1);
        assert(num2 = 2);

        num1 = 0;
        num2 = 0;
        b1_message_rewind(message);
        r = b1_message_read(message, "(tu)", &num1, &num2);
        assert(r >= 0);
        assert(num1 = 1);
        assert(num2 = 2);

        peer = b1_node_get_peer(node);
        assert(peer);

        r = b1_message_new_reply(peer, &reply, "");
        assert(r >= 0);
        assert(message);

        r = b1_message_reply(message, reply);
        assert(r >= 0);

        return 0;
}

static int slot_function(B1ReplySlot *slot, void *userdata, B1Message *message)
{
        fprintf(stderr, "PONG!\n");

        done = true;

        return 0;
}

static void test_cvariant(void)
{
        _c_cleanup_(c_variant_freep) CVariant *cv = NULL, *cv2;
        const struct iovec *vecs;
        size_t n_vecs;
        uint64_t num = 0;
        const char *str1 = NULL, *str2 = NULL;
        bool b = false;
        int r;

        r = c_variant_new(&cv, "(tvv)", strlen("(tvv)"));
        assert(r >= 0);
        assert(cv);

        r = c_variant_begin(cv, "(");
        assert(r >= 0);
        r = c_variant_write(cv, "t", 1);
        assert(r >= 0);
        r = c_variant_write(cv, "v", "(ssb)", "foo", "bar", true);
        assert(r >= 0);

        r = c_variant_seal(cv);
        assert(r >= 0);

        r = c_variant_enter(cv, "(");;
        assert(r >= 0);
        r = c_variant_read(cv, "t", &num);
        assert(r >= 0);
        assert(num == 1);
        r = c_variant_read(cv, "v", "(ssb)", &str1, &str2, &b);
        assert(r >= 0);
        assert(str1);
        assert(!strcmp(str1, "foo"));
        assert(str2);
        assert(!strcmp(str2, "bar"));
        assert(b);

        vecs = c_variant_get_vecs(cv, &n_vecs);
        assert(vecs);
        assert(n_vecs == 1);

        r = c_variant_new_from_vecs(&cv2, "(tvv)", strlen("(tvv)"), vecs, 1);
        assert(r >= 0);

        num = 0;
        str1 = NULL;
        str2 = NULL;
        b = false;

        r = c_variant_enter(cv2, "(");
        assert(r >= 0);
        r = c_variant_read(cv2, "t", &num);
        assert(r >= 0);
        assert(num == 1);
        r = c_variant_read(cv2, "v", "(ssb)", &str1, &str2, &b);
        assert(r >= 0);
        assert(str1);
        assert(!strcmp(str1, "foo"));
        assert(str2);
        assert(!strcmp(str2, "bar"));
        assert(b);
}

static void test_api(void)
{
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        B1Peer *clone = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;
        _c_cleanup_(b1_interface_unrefp) B1Interface *interface = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_reply_slot_freep) B1ReplySlot *slot = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL, *request = NULL, *reply = NULL;
        uint64_t num1 = 0;
        uint32_t num2 = 0;
        int r;

        r = b1_interface_new(&interface, "foo");
        assert(r >= 0);
        assert(interface);

        r = b1_interface_add_member(interface, "bar", "(tu)", "()", node_function);
        assert(r >= 0);

        r = b1_peer_new(&peer, NULL);
        assert(r >= 0);
        assert(peer);

        r = b1_peer_clone(peer, &node, NULL, &handle);
        assert(r >= 0);
        assert(node);
        assert(handle);
        clone = b1_node_get_peer(node);
        assert(clone);

        r = b1_node_implement(node, interface);
        assert(r >= 0);

        r = b1_message_new_call(peer, &message, "foo", "bar", "(tu)", "()", &slot, slot_function, NULL);
        assert(r >= 0);
        assert(message);
        assert(slot);

        r = b1_message_write(message, "(tu)", 1, 2);
        assert(r >= 0);
        r = b1_message_seal(message);
        assert(r >= 0);
        r = b1_message_read(message, "(tu)", &num1, &num2);
        assert(r >= 0);
        assert(num1 == 1);
        assert(num2 == 2);

        r = b1_message_send(message, &handle, 1);
        assert(r >= 0);

        r = b1_peer_recv(clone, &request);
        assert(r >= 0);
        assert(request);
        r = b1_message_dispatch(request);
        assert(r >= 0);

        r = b1_peer_recv(peer, &reply);
        assert(r >= 0);
        assert(reply);
        r = b1_message_dispatch(reply);
        assert(r >= 0);

        assert(done);
}

static void test_seed(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *seed1 = NULL, *seed2 = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node1 = NULL, *node2 = NULL;
        _c_cleanup_(b1_interface_unrefp) B1Interface *interface = NULL;
        const char *name ="org.foo.bar.Root";
        int r;

        r = b1_peer_new(&peer, NULL);
        assert(r >= 0);
        assert(peer);

        r = b1_node_new(peer, &node1, NULL);
        assert(r >= 0);
        assert(node1);

        r = b1_message_new_seed(peer, &seed1, &node1, &name, 1, "()");
        assert(r >= 0);
        assert(seed1);

        r = b1_message_send(seed1, NULL, 0);
        assert(r >= 0);

        r = b1_peer_recv_seed(peer, &seed2);
        assert(r >= 0);

        r = b1_interface_new(&interface, name);
        assert(r >= 0);

        r = b1_peer_implement(peer, &node2, NULL, interface);
        assert(!node2);
        assert(r == -ENOENT);

        r = b1_message_dispatch(seed2);
        assert(r >= 0);

        r = b1_peer_implement(peer, &node2, NULL, interface);
        assert(r >= 0);
}

int main(int argc, char **argv) {
        if (access("/dev/bus1", F_OK) < 0 && errno == ENOENT)
                return 77;

        test_cvariant();
        test_api();
        test_seed();

        return 0;
}
