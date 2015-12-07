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
 * Shared Helpers
 * This is the dumping ground for anything that is not categorized, yet, or
 * which we aren't sure where to put. There is no real structure here, but
 * rather a collection of random stuff.
 *
 * Try hard not to use, nor extend this header. Instead, move things into their
 * own headers/sources to not cause mass updates.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "c-macro.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XXX: move to separate header
 * XXX: document
 *
 * Bitmap helpers
 */

#include <string.h>

/**
 * c_bitmap_test() - test bit in bitmap
 * @bitmap:     bitmap
 * @bit:        bit to test
 *
 * This tests whether bit @bit is set in the bitmap @bitmap. The bitmap is
 * treated as an array of bytes, and @bit is the index of the bit to test (thus
 * starting at 0).
 *
 * The caller is responsible for range checks. This function assumes the bitmap
 * is big enough to hold bit @bit.
 *
 * Return: True if the bit is set, false if not.
 */
static inline bool c_bitmap_test(const void *bitmap, unsigned int bit) {
        return *((const uint8_t *)bitmap + bit / 8) & (1 << (bit % 8));
}

static inline void c_bitmap_set(void *bitmap, unsigned int bit) {
        *((uint8_t *)bitmap + bit / 8) |= (1 << (bit % 8));
}

static inline void c_bitmap_clear(void *bitmap, unsigned int bit) {
        *((uint8_t *)bitmap + bit / 8) &= ~(1 << (bit % 8));
}

static inline void c_bitmap_set_all(void *bitmap, unsigned int n_bits) {
        memset(bitmap, 0xff, n_bits / 8);
}

static inline void c_bitmap_clear_all(void *bitmap, unsigned int n_bits) {
        memset(bitmap, 0, n_bits / 8);
}

/*
 * XXX: move to separate header
 * XXX: write test suite
 *
 * String helpers
 */

#include <string.h>

_c_pure_ static inline bool c_str_equal(const char *a, const char *b) {
        return (!a || !b) ? (a == b) : !strcmp(a, b);
}

_c_pure_ static inline char *c_str_prefix(const char *str, const char *prefix) {
        size_t l = strlen(prefix);
        return !strncmp(str, prefix, l) ? (char *)str + l : NULL;
}

/*
 * XXX: move to separate header
 * XXX: write test suite
 *
 * Micro-second helpers
 */

#include <sys/time.h>
#include <time.h>
#include <assert.h>

/* stores up to 584,942.417355 years */
typedef uint64_t c_usec;

#define c_usec_from_nsec(_nsec) ((_nsec) / UINT64_C(1000))
#define c_usec_from_msec(_msec) ((_msec) * UINT64_C(1000))
#define c_usec_from_sec(_sec) c_usec_from_msec((_sec) * UINT64_C(1000))
#define c_usec_from_timespec(_ts) (c_usec_from_sec((_ts)->tv_sec) + c_usec_from_nsec((_ts)->tv_nsec))
#define c_usec_from_timeval(_tv) (c_usec_from_sec((_tv)->tv_sec) + (_tv)->tv_usec)

static inline c_usec c_usec_from_clock(clockid_t clock) {
        struct timespec ts;
        int r;

        r = clock_gettime(clock, &ts);
        assert(r >= 0);
        return c_usec_from_timespec(&ts);
}

#ifdef __cplusplus
}
#endif
