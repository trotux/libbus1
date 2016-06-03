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

struct B1MulticastGroup {
        B1Peer *peer;
        CList members;
        size_t n_members;
};

_c_public_ int b1_multicast_group_new(B1Peer *peer, B1MulticastGroup **groupp) {
        _c_cleanup_(b1_multicast_group_freep) B1MulticastGroup *group = NULL;

        assert(peer);
        assert(groupp);

        group = calloc(1, sizeof(*group));
        if (!group)
                return -ENOMEM;

        group->peer = b1_peer_ref(peer);
        c_list_init(&group->members);

        *groupp = group;
        group = NULL;

        return 0;
}

_c_public_ B1MulticastGroup *b1_multicast_group_free(B1MulticastGroup *group) {
        for (CListEntry *le = c_list_first(&group->members); le;
             le = c_list_entry_next(le)) {
                B1Handle *handle = c_container_of(le, B1Handle, multicast_group_le);

                c_list_remove(&group->members, le);
                b1_handle_unref(handle);
        }

        b1_peer_unref(group->peer);

        free(group);

        return NULL;
}

static int node_destruction(B1NotificationSlot *slot, void *userdata, B1Handle *handle) {
        B1MulticastGroup *group = userdata;

        assert(group);

        c_list_remove(&group->members, &handle->multicast_group_le);
        -- group->n_members;

        b1_handle_unref(handle);

        return 0;
}

_c_public_ int b1_multicast_group_join(B1MulticastGroup *group, B1Message *message) {
        B1Handle *handle;
        int r;

        assert(group);

        if (!message)
                return 0;

        if (group->peer != message->peer)
                return -EINVAL;

        handle = b1_message_get_reply_handle(message);
        if (!handle)
                return -ENOENT;

        if (c_list_entry_is_linked(&handle->multicast_group_le))
                return -EBUSY;

        r = b1_handle_monitor(handle, &handle->multicast_group_notification, node_destruction, group);
        if (r < 0)
                return r;

        c_list_append(&group->members, &handle->multicast_group_le);
        ++group->n_members;

        return 0;
}

_c_public_ int b1_multicast_groups_send(B1MulticastGroup **groups, size_t n_groups, B1Message *message) {
        B1Handle **handles;
        size_t n_handles = 0;
        unsigned int k = 0;

        for (unsigned int i = 0; i < n_groups; ++i)
                n_handles += groups[i]->n_members;

        handles = malloc(sizeof(B1Handle*) * n_handles);
        if (!handles)
                return -ENOMEM;

        for (unsigned int i = 0; i < n_groups; ++i)
                for (CListEntry *le = c_list_first(&groups[i]->members); le; le = c_list_entry_next(le))
                        handles[k ++] = c_container_of(le, B1Handle, multicast_group_le);

        return b1_message_send(message, handles, n_handles);
}

_c_public_ int b1_multicast_group_is_empty(B1MulticastGroup *group) {
        if (group && group->n_members > 0)
                return 1;
        else
                return 0;
}
