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
#include <string.h>

typedef struct Manager {
        unsigned n_ref;
        B1Peer *peer;
        CRBTree components; /* currently only used as a list */
        B1Interface *component_interface;
} Manager;

typedef struct Component {
        Manager *manager;
        const char *name;
        CRBNode rb;
        B1Peer *peer;
        B1Node *node;
        B1Handle *handle;
        char **dependencies;
        size_t n_dependencies;
} Component;

static Manager *manager_unref(Manager *m) {
        if (!m)
                return NULL;

        assert(m->n_ref > 0);
        -- m->n_ref;

        if (m->n_ref != 0)
                return NULL;

        assert(!c_rbtree_first(&m->components));
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

        *managerp = manager;
        manager = NULL;
        return 0;
}

static void component_free(Component *c) {
        if (!c)
                return;

        c_rbtree_remove_init(&c->manager->components, &c->rb);

        manager_unref(c->manager);
        b1_peer_unref(c->peer);
        b1_node_free(c->node);
        b1_handle_unref(c->handle);

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
                         const char **dependencies, size_t n_dependencies) {
        _c_cleanup_(component_freep) Component *component = NULL;
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

        component = calloc(1, sizeof(*component) + sizeof(char*) * n_dependencies + n_names);
        if (!component)
                return -ENOMEM;
        component->name = (void*)(component + 1);
        component->n_dependencies = n_dependencies;
        component->dependencies = (char **)(stpcpy((char*)component->name, name) + 1);
        buf = (char*)(component->dependencies + n_dependencies);
        for (unsigned int i = 0; i < n_dependencies; ++i) {
                component->dependencies[i] = buf;
                buf = stpcpy(buf, name) + 1;
        }

        c_rbnode_init(&component->rb);
        component->manager = manager_ref(manager);

        r = b1_node_new(manager->peer, &component->node, component);
        if (r < 0)
                return r;

        r = b1_node_implement(component->node, manager->component_interface);
        if (r < 0)
                return r;

        r = b1_peer_clone(manager->peer, &component->peer,
                          b1_node_get_handle(component->node),
                          &component->handle);
        if (r < 0)
                return r;

        c_rbtree_add(&manager->components, p, slot, &component->rb);

        *componentp = component;
        component = NULL;
        return 0;
}

int main(void) {
        _c_cleanup_(manager_unrefp) Manager *manager = NULL;
        _c_cleanup_(component_freep) Component *foo = NULL, *bar = NULL;
        const char *foo_deps[] = { "org.bus1.bar", "org.bus1.baz" };
        int r;

        r = manager_new(&manager);
        if (r < 0)
                return EXIT_FAILURE;

        r = component_new(manager, &foo, "org.bus1.foo", foo_deps, 2);
        if (r < 0)
                return r;

        r = component_new(manager, &bar, "org.bus1.bar", NULL, 0);
        if (r < 0)
                return r;

        return EXIT_SUCCESS;
}
