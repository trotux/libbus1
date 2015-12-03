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
 * C_SYS_UIO_FASTIOV - number of iovecs that can be handled in fast-path
 *
 * This is equivalent to the linux-defined UIO_FASTIOV as defined in
 * <linux/uio.h>. However, POSIX doesn't define it and the linux header clashes
 * with <sys/uio.h>, so we provide a replacement here.
 */
#define C_SYS_UIO_FASTIOV 8

/**
 * c_sys_memfd_create() - wrapper for memfd_create(2) syscall
 * @name:       name for memfd inode
 * @flags:      memfd flags
 *
 * This is a wrapper for the memfd_create(2) syscall. Currently, no user-space
 * wrapper is exported by any libc.
 *
 * Return: New memfd file-descriptor on success, -1 on failure.
 */
int c_sys_memfd_create(const char *name, unsigned int flags);

#ifdef __cplusplus
}
#endif
