#pragma once

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
 * Syscall helpers
 * XXX: Add description
 */

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * b1_sys_memfd_create() - wrapper for memfd_create(2) syscall
 * @name:       name for memfd inode
 * @flags:      memfd flags
 *
 * This is a wrapper for the memfd_create(2) syscall. Currently, no user-space
 * wrapper is exported by any libc.
 *
 * Return: New memfd file-descriptor on success, -1 on failure.
 */
int b1_sys_memfd_create(const char *name, unsigned int flags);

#ifdef __cplusplus
}
#endif
