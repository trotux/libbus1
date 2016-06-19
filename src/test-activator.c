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

#include <c-macro.h>
#include <c-rbtree.h>
#include <org.bus1/b1-peer.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#define B1_PEER_FD (3)

typedef struct Manager {
        unsigned long n_ref;
        B1Peer *peer;
        CRBTree components; /* currently only used as a list */
        CRBTree dependencies;
        B1Interface *component_interface;
} Manager;

typedef struct Component {
        Manager *manager;
        const char *name;
        CRBNode rb;
        B1Peer *peer;
        B1Node *management_node;
        B1Handle *management_handle;
        B1ReplySlot *slot;
        B1Node **root_nodes;
        size_t n_root_nodes;
        char **dependencies;
        size_t n_dependencies;
} Component;

typedef struct Dependency {
        Manager *manager;
        const char *name;
        CRBNode rb;
        B1Handle *handle;
} Dependency;

static Manager *manager_unref(Manager *m) {
        if (!m)
                return NULL;

        assert(m->n_ref > 0);
        -- m->n_ref;

        if (m->n_ref != 0)
                return NULL;

        assert(!c_rbtree_first(&m->components));
        assert(!c_rbtree_first(&m->dependencies));
        b1_interface_unref(m->component_interface);
        b1_peer_unref(m->peer);

        free(m);

        return NULL;
}

static void manager_unrefp(Manager **m) {
        (void)manager_unref(*m);
}

static Manager *manager_ref(Manager *m) {
        if (!m)
                return NULL;

        assert(m->n_ref > 0);

        ++ m->n_ref;

        return m;
}

static int component_set_root_nodes(B1Node *node, void *userdata, B1Message *message);
static int component_get_dependencies(B1Node *node, void *userdata, B1Message *message);

static int manager_new(Manager **managerp) {
        _c_cleanup_(manager_unrefp) Manager *manager = NULL;
        int r;

        assert(managerp);

        manager = calloc(1, sizeof(*manager));
        if (!manager)
                return -ENOMEM;

        manager->n_ref = 1;

        r = b1_peer_new(&manager->peer, NULL);
        if (r < 0)
                return r;

        r = b1_interface_new(&manager->component_interface, "org.bus1.Activator.Component");
        if (r < 0)
                return r;

        r = b1_interface_add_member(manager->component_interface,
                                    "setRootNodes", "a(su)", "()", component_set_root_nodes);
        if (r < 0)
                return r;

        r = b1_interface_add_member(manager->component_interface,
                                    "getDependencies", "()", "a(su)", component_get_dependencies);
        if (r < 0)
                return r;

        *managerp = manager;
        manager = NULL;
        return 0;
}

static void component_free(Component *c) {
        if (!c)
                return;

        c_rbtree_remove_init(&c->manager->components, &c->rb);

        for (unsigned int i = 0; i < c->n_root_nodes; ++i)
                b1_node_free(c->root_nodes[i]);

        manager_unref(c->manager);
        b1_peer_unref(c->peer);
        b1_node_free(c->management_node);
        b1_handle_unref(c->management_handle);
        b1_reply_slot_free(c->slot);

        free(c);
}

static void component_freep(Component **c) {
        component_free(*c);
}

static int components_compare(CRBTree *t, void *k, CRBNode *n) {
        Component *c = c_container_of(n, Component, rb);
        const char *name = k;

        return strcmp(name, c->name);
}

static int component_new(Manager *manager, Component **componentp, const char *name,
                         const char **root_nodes, size_t n_root_nodes,
                         const char **dependencies, size_t n_dependencies) {
        _c_cleanup_(component_freep) Component *component = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *management_handle = NULL;
        size_t n_names;
        CRBNode **slot, *p;
        char *buf;
        int r;

        assert(manager);
        assert(componentp);
        assert(name);
        assert(n_dependencies == 0 || dependencies);

        slot = c_rbtree_find_slot(&manager->components, components_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        n_names = strlen(name) + 1;
        for (unsigned int i = 0; i < n_dependencies; ++i)
                n_names += strlen(dependencies[i]) + 1;

        component = calloc(1, sizeof(*component) +
                              sizeof(char*) * n_dependencies +
                              sizeof(B1Node*) * n_root_nodes +
                              n_names);
        if (!component)
                return -ENOMEM;
        component->n_dependencies = n_dependencies;
        component->n_root_nodes = n_root_nodes;
        component->name = (void*)(component + 1);
        component->dependencies = (char **)(stpcpy((char*)component->name, name) + 1);
        component->root_nodes = (B1Node**)(component->dependencies + n_dependencies);
        buf = (char*)(component->root_nodes + n_root_nodes);
        for (unsigned int i = 0; i < n_dependencies; ++i) {
                component->dependencies[i] = buf;
                buf = stpcpy(buf, dependencies[i]) + 1;
        }

        c_rbnode_init(&component->rb);
        component->manager = manager_ref(manager);

        r = b1_node_new(manager->peer, &component->management_node, component);
        if (r < 0)
                return r;

        r = b1_node_implement(component->management_node, manager->component_interface);
        if (r < 0)
                return r;

        r = b1_node_acquire_handle(component->management_node, &management_handle);
        if (r < 0)
                return r;

        r = b1_peer_clone(manager->peer, &component->peer, management_handle, &component->management_handle);
        if (r < 0)
                return r;

        for (unsigned int i = 0; i < n_root_nodes; ++i) {
                r = b1_node_new_root(component->peer, &component->root_nodes[i], NULL, root_nodes[i]);
                if (r < 0)
                        return r;
        }

        c_rbtree_add(&manager->components, p, slot, &component->rb);

        *componentp = component;
        component = NULL;
        return 0;
}

static void dependency_free(Dependency *d) {
        if (!d)
                return;

        c_rbtree_remove_init(&d->manager->dependencies, &d->rb);

        manager_unref(d->manager);
        b1_handle_unref(d->handle);

        free(d);
}

static void dependency_freep(Dependency **d) {
        dependency_free(*d);
}

static int dependencies_compare(CRBTree *t, void *k, CRBNode *n) {
        Dependency *d = c_container_of(n, Dependency, rb);
        const char *name = k;

        return strcmp(name, d->name);
}

static int dependency_new(Manager *manager, Dependency **dependencyp, const char *name, B1Handle *handle) {
        _c_cleanup_(dependency_freep) Dependency *dependency = NULL;
        size_t n_name;
        CRBNode **slot, *p;

        assert(manager);
        assert(name);
        assert(handle);

        slot = c_rbtree_find_slot(&manager->dependencies, dependencies_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        n_name = strlen(name) + 1;
        dependency = calloc(1, sizeof(*dependency) + n_name);
        if (!dependency)
                return -ENOMEM;
        dependency->name = (void*)(dependency + 1);
        memcpy((char*)dependency->name, name, n_name);

        c_rbnode_init(&dependency->rb);
        dependency->manager = manager_ref(manager);
        dependency->handle = b1_handle_ref(handle);
        c_rbtree_add(&manager->dependencies, p, slot, &dependency->rb);

        if (dependencyp)
                *dependencyp = dependency;
        dependency = NULL;
        return 0;
}

static Dependency *dependency_get(Manager *manager, const char *name) {
        CRBNode *n;

        n = c_rbtree_find_node(&manager->dependencies, dependencies_compare, name);
        if (!n)
                return NULL;

        return c_container_of(n, Dependency, rb);
}

int component_set_root_nodes(B1Node *node, void *userdata, B1Message *message) {
        Component *component = userdata;
        unsigned int n;
        int r;

        assert(component);

        r = b1_message_enter(message, "a");
        if (r < 0)
                return r;

        r = b1_message_peek_count(message);
        if (r < 0)
                return r;
        else
                n = r;

        for (unsigned int i = 0; i < n; ++i) {
                const char *name;
                uint32_t offset;
                B1Handle *handle;

                r = b1_message_read(message, "(su)", &name, &offset);
                if (r < 0)
                        return r;

                r = b1_message_get_handle(message, offset, &handle);
                if (r < 0)
                        return r;

                r = dependency_new(component->manager, NULL, name, handle);
                if (r < 0)
                        return r;
        }

        r = b1_message_exit(message, "a");
        if (r < 0)
                return r;

        return 0;
}

int component_get_dependencies(B1Node *node, void *userdata, B1Message *message) {
        Component *component = userdata;
        _c_cleanup_(b1_message_unrefp) B1Message *reply = NULL;
        int r;

        assert(component);

        r = b1_message_new_reply(component->manager->peer, &reply, "a(su)");
        if (r < 0)
                return r;

        r = b1_message_begin(reply, "a");
        if (r < 0)
                return r;

        for (unsigned int i = 0; i < component->n_dependencies; ++i) {
                Dependency *dep;
                uint32_t handle;

                dep = dependency_get(component->manager, component->dependencies[i]);
                if (!dep)
                        return -ENOENT;

                r = b1_message_append_handle(reply, dep->handle);
                if (r < 0)
                        return r;
                else
                        handle = r;

                r = b1_message_write(reply, "(su)", dep->name, handle);
                if (r < 0)
                        return r;
        }

        r = b1_message_end(reply, "a");
        if (r < 0)
                return r;

        return b1_message_reply(message, reply);
}

static int component_send_root_nodes(Component *component) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new_call(component->peer, &message,
                                "org.bus1.Activator.Component",
                                "setRootNodes",
                                "a(su)",
                                "()",
                                NULL,
                                NULL,
                                NULL);
        if (r < 0)
                return r;

        r = b1_message_begin(message, "a");
        if (r < 0)
                return r;

        for (unsigned int i = 0; i < component->n_root_nodes; ++i) {
                B1Handle *handle;
                uint32_t index;
                const char *name;

                r = b1_node_acquire_handle(component->root_nodes[i], &handle);
                assert(r >= 0);

                r = b1_message_append_handle(message, handle);
                if (r < 0)
                        return r;
                else
                        index = r;

                name = b1_node_get_name(component->root_nodes[i]);
                assert(name);

                r = b1_message_write(message, "(su)", name, index);
                if (r < 0)
                        return r;
        }

        r = b1_message_end(message, "a");
        if (r < 0)
                return r;

        return b1_message_send(message, &component->management_handle, 1);
}

static int component_request_dependencies_handler(B1ReplySlot *slot, void *userdata, B1Message *reply) {
        Component *component = userdata;
        _c_cleanup_(b1_message_unrefp) B1Message *seed = NULL;
        unsigned int n;
        int r;

        assert(component);

        if (b1_message_get_type(reply) == B1_MESSAGE_TYPE_ERROR) {
                unsigned int err;

                r = b1_message_read(reply, "u", &err);
                fprintf(stderr, "Failed to get dependencies: %s\n", strerror(err));
        }

        fprintf(stderr, "Component: %s\n  Root Nodes:\n", component->name);

        for (unsigned int i = 0; i < component->n_root_nodes; ++i)
                fprintf(stderr, "    %s\n", b1_node_get_name(component->root_nodes[i]));

        fprintf(stderr, "  Dependencies:\n");

        r = b1_message_enter(reply, "a");

        r = b1_message_peek_count(reply);
        if (r < 0)
                return r;
        else
                n = r;

        r = b1_message_new_seed(component->peer, &seed,
                                component->root_nodes,
                                component->n_root_nodes,
                                "a(su)");
        if (r < 0)
                return r;

        r = b1_message_begin(seed, "a");
        if (r < 0)
                return r;

        for (unsigned int i = 0; i < n; ++i) {
                B1Handle *handle;
                const char *name;
                uint32_t offset;

                r = b1_message_read(reply, "(su)", &name, &offset);
                if (r < 0)
                        return r;

                fprintf(stderr, "    %s\n", name);

                r = b1_message_get_handle(reply, offset, &handle);
                if (r < 0)
                        return r;

                r = b1_message_append_handle(seed, handle);
                if (r < 0)
                        return r;
                else
                        offset = r;

                r = b1_message_write(seed, "(su)", name, offset);
                if (r < 0)
                        return r;
        }

        r = b1_message_exit(reply, "a");

        r = b1_message_end(seed, "a");
        if (r < 0)
                return r;

        return b1_message_send(seed, NULL, 0);
}

static int component_request_dependencies(Component *component) {
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        int r;

        r = b1_message_new_call(component->peer, &message,
                                "org.bus1.Activator.Component",
                                "getDependencies",
                                "()",
                                "a(su)",
                                &component->slot,
                                component_request_dependencies_handler,
                                component);
        if (r < 0)
                return r;

        return b1_message_send(message, &component->management_handle, 1);
}

static int component_spawn(Component *component) {
        pid_t pid;
        int r;

        /* destroy the management channel */
        component->management_node = b1_node_free(component->management_node);
        component->management_handle = b1_handle_unref(component->management_handle);

        pid = fork();
        if (pid < 0)
                return errno;
        else if (pid == 0) {
                /* Move our peer fd to the well-known peer fd number and clear
                 * CLOEXEC on it. */
                r = dup2(b1_peer_get_fd(component->peer), B1_PEER_FD);
                if (r < 0)
                        return -errno;

                r = execlp("/proc/self/exe", "/proc/self/exe", component->name, NULL);
                if (r < 0)
                        fprintf(stderr, "Spawning '%s' failed: %m\n", component->name);

                _exit(EXIT_FAILURE);
        }

        return 0;
}

static int peer_process(B1Peer *peer) {
        int r;

        for (;;) {
                _c_cleanup_(b1_message_unrefp) B1Message *request = NULL;

                r = b1_peer_recv(peer, &request);
                if (r == -EAGAIN)
                        return 0;
                else if (r < 0)
                        return r;

                r = b1_message_dispatch(request);
                if (r < 0)
                        return r;
        }
}

static int manager_instantiate_root_handles(Manager *manager) {
        CRBNode *n;
        int r;

        /* pass the root nodes from each component to the manager */
        for (n = c_rbtree_first(&manager->components); n; n = c_rbnode_next(n)) {
                Component *c = c_container_of(n, Component, rb);

                r = component_send_root_nodes(c);
                if (r < 0)
                        return r;
        }

        /* dispatch all the pending messages, installing the new root handles */
        r = peer_process(manager->peer);
        if (r < 0)
                return r;

        return 0;
}

static int manager_instantiate_dependencies(Manager *manager) {
        CRBNode *n;
        int r;

        for (n = c_rbtree_first(&manager->components); n; n = c_rbnode_next(n)) {
                Component *c = c_container_of(n, Component, rb);

                /* request dependencies */
                r = component_request_dependencies(c);
                if (r < 0)
                        return r;

                /* process request */
                r = peer_process(manager->peer);
                if (r < 0)
                        return r;

                /* process reply */
                r = peer_process(c->peer);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int manager_spawn_components(Manager *manager) {
        CRBNode *n;
        int r;

        for (n = c_rbtree_first(&manager->components); n; n = c_rbnode_next(n)) {
                Component *c = c_container_of(n, Component, rb);

                r = component_spawn(c);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int main_activator(void) {
        _c_cleanup_(manager_unrefp) Manager *manager = NULL;
        _c_cleanup_(component_freep) Component *foo = NULL, *bar = NULL, *baz = NULL;
        const char *foo_deps[] = { "org.bus1.bar.Read", "org.bus1.baz" };
        const char *foo_roots[] = { "org.bus1.foo" };
        const char *bar_roots[] = { "org.bus1.bar.Read", "org.bus1.bar.ReadWrite" };
        const char *baz_roots[] = { "org.bus1.baz" };
        int r;

        r = manager_new(&manager);
        if (r < 0)
                return r;

        r = component_new(manager, &foo, "org.bus1.foo", foo_roots, 1, foo_deps, 2);
        if (r < 0)
                return r;

        r = component_new(manager, &bar, "org.bus1.bar", bar_roots, 2, NULL, 0);
        if (r < 0)
                return r;

        r = component_new(manager, &baz, "org.bus1.baz", baz_roots, 1, NULL, 0);
        if (r < 0)
                return r;

        r = manager_instantiate_root_handles(manager);
        if (r < 0)
                return r;

        r = manager_instantiate_dependencies(manager);
        if (r < 0)
                return r;

        r = manager_spawn_components(manager);
        if (r < 0)
                return r;

        return 0;
}

static int ping(B1Node *node, void *userdata, B1Message *ping) {
        _c_cleanup_(b1_message_unrefp) B1Message *pong = NULL;
        bool *done = userdata;
        B1Peer *peer;
        int r;

        assert(done);

        fprintf(stderr, "PING!\n!");

        *done = true;

        peer = b1_node_get_peer(node);
        assert(peer);

        r = b1_message_new_reply(peer, &pong, "()");
        if (r < 0)
                return r;

        return b1_message_reply(ping, pong);
}

static int pong(B1ReplySlot *slot, void *userdata, B1Message *pong) {
        unsigned int *count = userdata;
        const char *error;

        assert(count);

        error = b1_message_get_error(pong);
        if (error) {
                fprintf(stderr, "ping failed: %s\n", error);

                return 1;
        }

        fprintf(stderr, "PONG!\n");

        (*count)++;

        return 1;
}

static int main_foo(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *seed = NULL;
        _c_cleanup_(b1_interface_unrefp) B1Interface *root_interface = NULL;
        _c_cleanup_(b1_node_freep) B1Node *root_node = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *request = NULL;
        _c_cleanup_(b1_reply_slot_freep) B1ReplySlot *slot = NULL;
        B1Handle *bar_read, *baz;
        const char *name;
        uint32_t offset;
        unsigned int count = 0;
        int r;

        /* set CLOEXEC on the passed in fd again */
        r = fcntl(B1_PEER_FD, F_SETFD, FD_CLOEXEC);
        if (r < 0)
                return -errno;

        /* instantiate peer from the passed in fd */
        r = b1_peer_new_from_fd(&peer, B1_PEER_FD);
        if (r < 0)
                return r;

        /* fetch the seed pinned in the kernel */
        r = b1_peer_recv_seed(peer, &seed);
        if (r < 0)
                return r;

        /* read out all the pedendencies and their handles */
        r = b1_message_enter(seed, "a");
        if (r < 0)
                return r;

        r = b1_message_read(seed, "(su)", &name, &offset);
        if (r < 0)
                return r;

        if (strcmp(name, "org.bus1.bar.Read") != 0)
                return -EINVAL;

        r = b1_message_get_handle(seed, offset, &bar_read);
        if (r < 0)
                return r;

        r = b1_message_read(seed, "(su)", &name, &offset);
        if (r < 0)
                return r;

        if (strcmp(name, "org.bus1.baz") != 0)
                return -EINVAL;

        r = b1_message_get_handle(seed, offset, &baz);
        if (r < 0)
                return r;

        r = b1_message_exit(seed, "a");
        if (r < 0)
                return r;

        /* instantiate root nodes in peer */
        r = b1_message_dispatch(seed);
        if (r < 0)
                return r;

        /* implement root node */
        r = b1_interface_new(&root_interface, "org.bus1.foo");
        if (r < 0)
                return r;

        r = b1_peer_implement(peer, &root_node, NULL, root_interface);
        if (r < 0)
                return r;

        /* do a method call */
        r = b1_message_new_call(peer, &request, "org.bus1.baz", "Ping", "()", "()", &slot, pong, &count);
        if (r < 0)
                return r;

        r = b1_message_send(request, &baz, 1);
        if (r < 0)
                return r;

        r = b1_message_send(request, &bar_read, 1);
        if (r < 0)
                return r;

        for (;;) {
                _c_cleanup_(b1_message_unrefp) B1Message *reply = NULL;
                struct pollfd pfd = {
                        .fd = b1_peer_get_fd(peer),
                        .events = POLLIN,
                };

                r = poll(&pfd, 1, 100);
                if (r < 0)
                        return -errno;
                else if (r == 0)
                        return -ETIMEDOUT;

                r = b1_peer_recv(peer, &reply);
                if (r < 0)
                        return r;

                r = b1_message_dispatch(reply);
                if (r < 0)
                        return r;

                if (count == 2)
                        break;
        }

        return 0;
}

static int main_bar(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *seed = NULL;
        _c_cleanup_(b1_interface_unrefp) B1Interface *root_interface_read = NULL;
        _c_cleanup_(b1_interface_unrefp) B1Interface *root_interface_write = NULL;
        _c_cleanup_(b1_node_freep) B1Node *root_node_read = NULL;
        _c_cleanup_(b1_node_freep) B1Node *root_node_write = NULL;
        bool done = false;
        int r;

        /* set CLOEXEC on the passed in fd again */
        r = fcntl(B1_PEER_FD, F_SETFD, FD_CLOEXEC);
        if (r < 0)
                return -errno;

        /* instantiate peer from the passed in fd */
        r = b1_peer_new_from_fd(&peer, B1_PEER_FD);
        if (r < 0)
                return r;

        /* fetch the seed pinned in the kernel */
        r = b1_peer_recv_seed(peer, &seed);
        if (r < 0)
                return r;

        /* instantiate root nodes in peer */
        r = b1_message_dispatch(seed);
        if (r < 0)
                return r;

        /* implement root nodes */
        r = b1_interface_new(&root_interface_read, "org.bus1.bar.Read");
        if (r < 0)
                return r;

        r = b1_peer_implement(peer, &root_node_read, NULL, root_interface_read);
        if (r < 0)
                return r;

        r = b1_interface_new(&root_interface_write, "org.bus1.bar.ReadWrite");
        if (r < 0)
                return r;

        r = b1_peer_implement(peer, &root_node_write, &done, root_interface_write);
        if (r < 0)
                return r;

        for (;;) {
                _c_cleanup_(b1_message_unrefp) B1Message *reply = NULL;
                struct pollfd pfd = {
                        .fd = b1_peer_get_fd(peer),
                        .events = POLLIN,
                };

                r = poll(&pfd, 1, 100);
                if (r < 0)
                        return -errno;
                else if (r == 0)
                        return -ETIMEDOUT;

                fprintf(stderr, "bar: something happened\n");

                r = b1_peer_recv(peer, &reply);
                if (r < 0)
                        return r;

                r = b1_message_dispatch(reply);
                if (r < 0)
                        return r;

                if (done)
                        break;
        }

        return 0;
}

static int main_baz(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *seed = NULL;
        _c_cleanup_(b1_interface_unrefp) B1Interface *root_interface = NULL;
        _c_cleanup_(b1_node_freep) B1Node *root_node = NULL;
        bool done = false;
        int r;

        /* set CLOEXEC on the passed in fd again */
        r = fcntl(B1_PEER_FD, F_SETFD, FD_CLOEXEC);
        if (r < 0)
                return -errno;

        /* instantiate peer from the passed in fd */
        r = b1_peer_new_from_fd(&peer, B1_PEER_FD);
        if (r < 0)
                return r;

        /* fetch the seed pinned in the kernel */
        r = b1_peer_recv_seed(peer, &seed);
        if (r < 0)
                return r;

        /* instantiate root nodes in peer */
        r = b1_message_dispatch(seed);
        if (r < 0)
                return r;

        /* implement root node */
        r = b1_interface_new(&root_interface, "org.bus1.baz");
        if (r < 0)
                return r;

        r = b1_interface_add_member(root_interface, "Ping()", "()", "()", ping);
        if (r < 0)
                return r;

        r = b1_peer_implement(peer, &root_node, &done, root_interface);
        if (r < 0)
                return r;

        for (;;) {
                _c_cleanup_(b1_message_unrefp) B1Message *reply = NULL;
                struct pollfd pfd = {
                        .fd = b1_peer_get_fd(peer),
                        .events = POLLIN,
                };

                r = poll(&pfd, 1, 100);
                if (r < 0)
                        return -errno;
                else if (r == 0)
                        return -ETIMEDOUT;

                r = b1_peer_recv(peer, &reply);
                if (r < 0)
                        return r;

                r = b1_message_dispatch(reply);
                if (r < 0)
                        return r;

                if (done)
                        break;
        }

        return 0;
}

int main(int argc, char **argv) {
        int r;

        if (argc == 1) {
                r = main_activator();
                if (r < 0) {
                        fprintf(stderr, "Activator failed: %s\n", strerror(-r));
                        return EXIT_FAILURE;
                }
        } else if (argc == 2) {
                if (strcmp(argv[1], "org.bus1.foo") == 0)
                        r = main_foo();
                else if (strcmp(argv[1], "org.bus1.bar") == 0)
                        r = main_bar();
                else if (strcmp(argv[1], "org.bus1.baz") == 0)
                        r = main_baz();
                else {
                        fprintf(stderr, "Unrecognized component '%s'\n", argv[1]);
                        return EXIT_FAILURE;
                }
                if (r < 0) {
                        fprintf(stderr, "Component '%s' failed: %s\n", argv[1], strerror(-r));
                        return EXIT_FAILURE;
                }
        } else {
                fprintf(stderr, "This binary takes none or one argumnets\n");
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}
