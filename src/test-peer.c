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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "org.bus1/b1-peer.h"
#include "org.bus1/c-variant.h"

static int node_function(B1Node *node, void *userdata, B1Message *message)
{
        B1Message *reply = NULL;
        uint64_t num1 = 0;
        uint32_t num2 = 0;

        fprintf(stderr, "PING!\n");

        assert(b1_message_read(message, "(tu)", &num1, &num2) >= 0);
        assert(num1 = 1);
        assert(num2 = 2);

        num1 = 0;
        num2 = 0;
        b1_message_rewind(message);
        assert(b1_message_read(message, "(tu)", &num1, &num2) >= 0);
        assert(num1 = 1);
        assert(num2 = 2);

        assert(b1_message_new_reply(&reply, "", NULL, NULL, NULL) >= 0);
        assert(message);

        assert(b1_peer_reply(message, reply) >= 0);

        return 0;
}

static int slot_function(B1Slot *slot, void *userdata, B1Message *message)
{
        fprintf(stderr, "PONG!\n");

        return 0;
}

static void test_cvariant(void)
{
        CVariant *cv = NULL, *cv2;
        const struct iovec *vecs;
        size_t n_vecs;
        uint64_t num = 0;
        const char *str1 = NULL, *str2 = NULL;
        bool b = false;

        assert(c_variant_new(&cv, "(tvv)", strlen("(tvv)")) >= 0);
        assert(cv);

        assert(c_variant_begin(cv, "(") >= 0);
        assert(c_variant_write(cv, "t", 1) >= 0);
        assert(c_variant_write(cv, "v", "(ssb)", "foo", "bar", true) >= 0);

        assert(c_variant_seal(cv) >= 0);

        assert(c_variant_enter(cv, "(") >= 0);
        assert(c_variant_read(cv, "t", &num) >= 0);
        assert(num == 1);
        assert(c_variant_read(cv, "v", "(ssb)", &str1, &str2, &b) >= 0);
        assert(str1);
        assert(!strcmp(str1, "foo"));
        assert(str2);
        assert(!strcmp(str2, "bar"));
        assert(b);

        vecs = c_variant_get_vecs(cv, &n_vecs);
        assert(vecs);
        assert(n_vecs == 1);

        assert(c_variant_new_from_vecs(&cv2, "(tvv)", strlen("(tvv)"), vecs, 1) >= 0);

        num = 0;
        str1 = NULL;
        str2 = NULL;
        b = false;

        assert(c_variant_enter(cv2, "(") >= 0);
        assert(c_variant_read(cv2, "t", &num) >= 0);
        assert(num == 1);
        assert(c_variant_read(cv2, "v", "(ssb)", &str1, &str2, &b) >= 0);
        assert(str1);
        assert(!strcmp(str1, "foo"));
        assert(str2);
        assert(!strcmp(str2, "bar"));
        assert(b);
}

static void test_api(void)
{
        B1Peer *peer = NULL, *clone = NULL;
        B1Handle *handle = NULL;
        B1Interface *interface = NULL;
        B1Node *node = NULL;
        B1Slot *slot = NULL;
        B1Message *message = NULL, *request = NULL, *reply = NULL;
        uint64_t num1 = 0;
        uint32_t num2 = 0;

        assert(b1_interface_new(&interface, "foo") >= 0);
        assert(interface);

        assert(b1_interface_add_member(interface, "bar", node_function) >= 0);

        assert(b1_peer_new(&peer, NULL) >= 0);
        assert(peer);

        assert(b1_peer_clone(peer, &node, &handle) >= 0);
        assert(node);
        assert(handle);
        clone = b1_node_get_peer(node);
        assert(clone);

        assert(b1_node_implement(node, interface) >= 0);

        assert(b1_message_new_call(&message, "foo", "bar", &slot, slot_function, NULL) >= 0);
        assert(message);
        assert(slot);

        assert(b1_message_write(message, "(tu)", 1, 2) >= 0);
        assert(b1_message_seal(message) >= 0);
        assert(b1_message_read(message, "(tu)", &num1, &num2) >= 0);
        assert(num1 == 1);
        assert(num2 == 2);

        assert(b1_peer_send(peer, &handle, 1, message) >= 0);

        assert(b1_peer_recv(clone, &request) >= 0);
        assert(request);
        assert(b1_message_dispatch(request) >= 0);

        assert(b1_peer_recv(peer, &reply) >= 0);
        assert(reply);
        assert(b1_message_dispatch(reply) >= 0);

        assert(!b1_message_unref(reply));
        assert(!b1_message_unref(request));
        assert(!b1_message_unref(message));
        assert(!b1_slot_free(slot));
        assert(!b1_node_free(node));
        assert(!b1_interface_unref(interface));
        assert(!b1_handle_unref(handle));
        assert(!b1_peer_unref(peer));
}

int main(int argc, char **argv) {
        test_cvariant();
        test_api();

        return 0;
}
