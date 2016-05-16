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

#include <assert.h>
#include <c-macro.h>
#include <c-rbtree.h>
#include <errno.h>
#include "interface.h"
#include <stdlib.h>
#include <string.h>

static int members_compare(CRBTree *t, void *k, CRBNode *n) {
        B1Member *member = c_container_of(n, B1Member, rb);
        const char *name = k;

        return strcmp(name, member->name);
}

B1Member *b1_interface_get_member(B1Interface *interface, const char *name) {
        CRBNode *n;

        assert(interface);
        assert(name);

        n = c_rbtree_find_node(&interface->members, members_compare, name);
        if (!n)
                return NULL;

        return c_container_of(n, B1Member, rb);
}

/**
 * b1_interface_new() - create new interface
 * @interfacep:         pointer to new interface object
 * @name:               interface name
 *
 * An interface is a named collection of methods, that can be associated with
 * nodes. A method is invoked over the bus by making a method call to a node
 * and supplying the interafce and method names.
 *
 * Return: 0 on success, negative error code on failure.
 */
_c_public_ int b1_interface_new(B1Interface **interfacep, const char *name) {
        _c_cleanup_(b1_interface_unrefp) B1Interface *interface = NULL;
        size_t n_name;

        assert(interfacep);
        assert(name);

        n_name = strlen(name) + 1;
        interface = malloc(sizeof(*interface) + n_name);
        if (!interface)
                return -ENOMEM;

        interface->n_ref = 1;
        interface->implemented = false;
        interface->name = (void *)(interface + 1);
        interface->members = (CRBTree){};
        memcpy(interface->name, name, n_name);

        *interfacep = interface;
        interface = NULL;
        return 0;
}

/**
 * b1_interface_ref() - acquire reference
 * @interface:          interface to acquire reference to, or NULL
 *
 * Acquire a single new reference to the given interface. The caller must
 * already own a reference.
 *
 * If NULL is passed, this is a no-op.
 *
 * Return: @interface is returned.
 */
_c_public_ B1Interface *b1_interface_ref(B1Interface *interface) {
        if (interface) {
                assert(interface->n_ref > 0);
                ++interface->n_ref;
        }
        return interface;
}

/**
 * b1_interface_unref() - release reference
 * @interface:          interface to release reference to, or NULL
 *
 * Release a single reference to @interface. If this is the last reference, the
 * interface is destroyed.
 *
 * If NULL is passed, this is a no-op.
 *
 * Return: NULL is returned.
 */
_c_public_ B1Interface *b1_interface_unref(B1Interface *interface) {
        CRBNode *node;

        if (!interface)
                return NULL;

        assert(interface->n_ref > 0);

        if (--interface->n_ref > 0)
                return NULL;

        while ((node = c_rbtree_first(&interface->members))) {
                B1Member *member = c_container_of(node, B1Member, rb);

                c_rbtree_remove(&interface->members, node);
                free(member);
        }

        free(interface);

        return NULL;
}

/**
 * b1_interface_add_member() - add member to interface
 * @interface:          interface to operate on
 * @name:               member name
 * @type_input:         type of the expected input argument
 * @type_output:        type of the produced output arguments
 * @fn:                 function implementing the member
 *
 * This adds a new member function to the given interface. The member function
 * name must not already exist, or this will fail.
 *
 * @type_input must describe the input types expected by the callback, which
 * will be pre-validated by the library on all input. @type_output describes
 * the types expected to be produced as a result, and is verified by the
 * library as well.
 *
 * Return: 0 on succes, or a negative error code on failure.
 */
_c_public_ int b1_interface_add_member(B1Interface *interface,
                                       const char *name,
                                       const char *type_input,
                                       const char *type_output,
                                       B1NodeFn fn) {
        size_t n_name, n_type_input, n_type_output;
        B1Member *member;
        CRBNode **slot, *p;

        assert(interface);
        assert(name);
        assert(type_input);
        assert(type_output);
        assert(fn);

        if (interface->implemented)
                return -EBUSY;

        slot = c_rbtree_find_slot(&interface->members, members_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        n_name = strlen(name) + 1;
        n_type_input = strlen(type_input) + 1;
        n_type_output = strlen(type_output) + 1;
        member = malloc(sizeof(*member) + n_name + n_type_input + n_type_output);
        if (!member)
                return -ENOMEM;

        c_rbnode_init(&member->rb);
        member->name = (void *)(member + 1);
        member->type_input = member->name + n_name;
        member->type_output = member->type_input + n_type_input;
        member->fn = fn;

        memcpy(member->name, name, n_name);
        memcpy(member->type_input, type_input, n_type_input);
        memcpy(member->type_output, type_output, n_type_output);
        c_rbtree_add(&interface->members, p, slot, &member->rb);

        return 0;
}
