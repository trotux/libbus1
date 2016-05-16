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
#include <stdlib.h>
#include "org.bus1/b1-peer.h"

struct B1Interface {
        unsigned long n_ref;
        bool implemented;

        char *name;

        CRBTree members;
};

typedef struct B1Member {
        CRBNode rb;
        char *name;
        char *type_input;
        char *type_output;
        B1NodeFn fn;
} B1Member;

B1Member *b1_interface_get_member(B1Interface *interface, const char *name);
