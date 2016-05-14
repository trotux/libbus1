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
 * Performance Test
 * XXX: Add description, and rename to something more appropriate
 *      than test-perf.
 */

#undef NDEBUG
#include <c-macro.h>
#include <c-sys.h>
#include <c-usec.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#define TEST_BUFSIZE (4096LL * 4096LL) /* 4096 pages */

typedef struct {
        uint32_t arg1;
        uint32_t arg2;
        uint64_t arg3;
        uint64_t size;
        uint8_t blob[];
} TestMessage;

static void test_message_write1(int fd, void *map, const TestMessage *args) {
        memcpy(map, args, sizeof(*args) + args->size);
}

static void test_message_write2(int fd, void *map, const TestMessage *args) {
        TestMessage *m = map;

        m->arg1 = args->arg1;
        m->arg2 = args->arg2;
        m->arg3 = args->arg3;
        m->size = args->size;
        memcpy(m->blob, args->blob, args->size);
}

static void test_message_write3(int fd, void *map, const TestMessage *args) {
        TestMessage *m;
        uint64_t size;
        int r;

        size = sizeof(*m) + args->size;
        m = alloca(size);
        m->arg1 = args->arg1;
        m->arg2 = args->arg2;
        m->arg3 = args->arg3;
        m->size = args->size;
        memcpy(m->blob, args->blob, args->size);

        r = pwrite(fd, m, size, 0);
        assert(r >= 0 && (uint64_t)r == size);
}

static void test_message_write4(int fd, void *map, const TestMessage *args) {
        struct iovec vec[2];
        TestMessage *m;
        int r;

        m = alloca(sizeof(*m));
        m->arg1 = args->arg1;
        m->arg2 = args->arg2;
        m->arg3 = args->arg3;
        m->size = args->size;

        vec[0].iov_base = (void *)m;
        vec[0].iov_len = sizeof(*m);
        vec[1].iov_base = (void *)args->blob;
        vec[1].iov_len = args->size;

        r = pwritev(fd, vec, 2, 0);
        assert(r >= 0 && (uint64_t)r == sizeof(*m) + args->size);
}

static void test_message_write5(int fd, void *map, const TestMessage *args) {
        struct iovec vec[5];
        int r;

        vec[0].iov_base = (void *)&args->arg1;
        vec[0].iov_len = sizeof(args->arg1);
        vec[1].iov_base = (void *)&args->arg2;
        vec[1].iov_len = sizeof(args->arg2);
        vec[2].iov_base = (void *)&args->arg3;
        vec[2].iov_len = sizeof(args->arg3);
        vec[3].iov_base = (void *)&args->size;
        vec[3].iov_len = sizeof(args->size);
        vec[4].iov_base = (void *)args->blob;
        vec[4].iov_len = args->size;

        r = pwritev(fd, vec, 5, 0);
        assert(r >= 0 && (uint64_t)r == sizeof(TestMessage) + args->size);
}

static void (* const test_xmitters[]) (int fd, void *map, const TestMessage *args) = {
        test_message_write1,
        test_message_write2,
        test_message_write3,
        test_message_write4,
        test_message_write5,
};

static void test_message_validate(const void *map, const TestMessage *args) {
        const TestMessage *m = map;

        assert(args->arg1 == m->arg1);
        assert(args->arg2 == m->arg2);
        assert(args->arg3 == m->arg3);
        assert(args->size == m->size);
        assert(!memcmp(args->blob, m->blob, args->size));
}

static void test_message_xmit(int fd, void *map, const TestMessage *args, unsigned int xmitter) {
        assert(xmitter < C_ARRAY_SIZE(test_xmitters));

        test_xmitters[xmitter](fd, map, args);
        test_message_validate(map, args);
}

static void test_xmit(int fd, void *map, unsigned int xmitter, uint64_t times, uint64_t size) {
        static struct {
                TestMessage m;
                uint8_t blob[4096 * 4096];
        } m = {
                .m = {
                        .arg1 = UINT32_C(0xabcdabcd),
                        .arg2 = UINT32_C(0xffffffff),
                        .arg3 = UINT64_C(0xff00ff00ff00ff00),
                },
                .blob = {},
        };
        uint64_t i;

        assert(size <= sizeof(m.blob));

        m.m.size = size;
        for (i = 0; i < times; ++i)
                test_message_xmit(fd, map, &m.m, xmitter);
}

static void test_run_one(int fd, void *map, unsigned int xmitter, uint64_t times, uint64_t size) {
        uint64_t start_usec, end_usec;

        fprintf(stderr, "Run: times:%" PRIu64 " size:%" PRIu64 "\n", times, size);

        /* do some test runs to initialize caches; don't account them */
        memset(map, 0, TEST_BUFSIZE);
        test_xmit(fd, map, xmitter, times / 10, size);

        /* do real tests and measure time */
        start_usec = c_usec_from_clock(CLOCK_THREAD_CPUTIME_ID);
        test_xmit(fd, map, xmitter, times, size);
        end_usec = c_usec_from_clock(CLOCK_THREAD_CPUTIME_ID);

        /* print result table */
        printf("%" PRIu64 " %d %" PRIu64 "\n", size, xmitter, end_usec - start_usec);
}

static void test_run_all(int fd, void *map, unsigned int xmitter, uint64_t times) {
        unsigned int size;

        /*
         * Run test suite with different blob-sizes. First we start from size 1
         * to 128, doubling on each iteration. Then from 128 to 64k we just add
         * 128 each round to get better details.
         */

        for (size = 1; size <= 128; size <<= 1)
                test_run_one(fd, map, xmitter, times, size);
        for ( ; size <= 4096 << 4; size += 128)
                test_run_one(fd, map, xmitter, times, size);
}

static void test_transaction(unsigned int xmitter) {
        int memfd, r;
        uint8_t *map;
        long i;

        /* create memfd and pre-allocate buffer space */
        memfd = c_sys_memfd_create("test-file", MFD_CLOEXEC);
        assert(memfd >= 0);

        r = fallocate(memfd, 0, 0, TEST_BUFSIZE);
        assert(r >= 0);

        map = mmap(NULL, TEST_BUFSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
        assert(map != MAP_FAILED);

        /* access the whole buffer to do some random caching and fault in pages */
        for (i = 0; i < TEST_BUFSIZE; ++i)
                assert(map[i] == 0);

        /* run tests; each one 10k times */
        test_run_all(memfd, map, xmitter, 10UL * 1000UL);

        /* cleanup */
        munmap(map, TEST_BUFSIZE);
        close(memfd);
}

int main(int argc, char **argv) {
        unsigned int xmitter;

        if (argc != 2) {
                fprintf(stderr, "Usage: %s <#xmitter>\n", program_invocation_short_name);
                return 77;
        }

        xmitter = atoi(argv[1]);
        if (xmitter >= C_ARRAY_SIZE(test_xmitters)) {
                fprintf(stderr, "Invalid xmitter (available: %zu)\n", C_ARRAY_SIZE(test_xmitters));
                return 77;
        }

        test_transaction(xmitter);
        return 0;
}
