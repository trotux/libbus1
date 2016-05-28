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

#include <c-list.h>
#include <c-macro.h>
#include <org.bus1/b1-peer.h>

#include "message.h"
#include "node.h"

struct B1Match {
        B1Peer *peer;
        CList subscriptions;
        size_t n_subscriptions;
};

_c_public_ int b1_match_new(B1Peer *peer, B1Match **matchp) {
        _c_cleanup_(b1_match_freep) B1Match *match = NULL;

        assert(peer);
        assert(matchp);

        match = calloc(1, sizeof(*match));
        if (!match)
                return -ENOMEM;

        match->peer = b1_peer_ref(peer);
        c_list_init(&match->subscriptions);

        *matchp = match;
        match = NULL;

        return 0;
}

_c_public_ B1Match *b1_match_free(B1Match *match) {
        for (CListEntry *le = c_list_first(&match->subscriptions); le;
             le = c_list_entry_next(le)) {
                B1Handle *handle = c_container_of(le, B1Handle, match_le);

                c_list_remove(&match->subscriptions, le);
                b1_handle_unref(handle);
        }

        b1_peer_unref(match->peer);

        free(match);

        return NULL;
}

static int node_destruction(B1NotificationSlot *slot, void *userdata, B1Handle *handle) {
        B1Match *match = userdata;

        assert(match);

        c_list_remove(&match->subscriptions, &handle->match_le);
        -- match->n_subscriptions;

        b1_handle_unref(handle);

        return 0;
}

_c_public_ int b1_match_subscribe(B1Match *match, B1Message *message) {
        B1Handle *handle;
        int r;

        assert(match);

        if (!message)
                return 0;

        if (match->peer != message->peer)
                return -EINVAL;

        handle = b1_message_get_reply_handle(message);
        if (!handle)
                return -ENOENT;

        if (c_list_entry_is_linked(&handle->match_le))
                return -EBUSY;

        r = b1_handle_monitor(handle, &handle->match_notification, node_destruction, match);
        if (r < 0)
                return r;

        c_list_append(&match->subscriptions, &handle->match_le);
        ++match->n_subscriptions;

        return 0;
}

_c_public_ int b1_match_send(B1Match **matches, size_t n_matches, B1Message *message) {
        B1Handle **handles;
        size_t n_handles = 0;
        unsigned int k = 0;

        for (unsigned int i = 0; i < n_matches; ++i)
                n_handles += matches[i]->n_subscriptions;

        handles = malloc(sizeof(B1Handle*) * n_handles);
        if (!handles)
                return -ENOMEM;

        for (unsigned int i = 0; i < n_matches; ++i)
                for (CListEntry *le = c_list_first(&matches[i]->subscriptions); le; le = c_list_entry_next(le))
                        handles[k ++] = c_container_of(le, B1Handle, match_le);

        return b1_message_send(message, handles, n_handles);
}

_c_public_ int b1_match_is_empty(B1Match *match) {
        if (match && match->n_subscriptions > 0)
                return 1;
        else
                return 0;
}
