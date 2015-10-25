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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bus1-macro.h"

/* test B1_TYPE_* helpers */
static void test_type(void) {
        int foo, bar, array[16] = {};
        long baz;

        /* test type-matching with mixed explicit types and variables */
        B1_CC_ASSERT(B1_TYPE_MATCH(foo, int));
        B1_CC_ASSERT(B1_TYPE_MATCH(int, foo));
        B1_CC_ASSERT(B1_TYPE_MATCH(int, int));
        B1_CC_ASSERT(B1_TYPE_MATCH(foo, foo));
        B1_CC_ASSERT(B1_TYPE_MATCH(foo, bar));
        B1_CC_ASSERT(B1_TYPE_MATCH(bar, foo));
        B1_CC_ASSERT(B1_TYPE_MATCH(baz, long));
        B1_CC_ASSERT(!B1_TYPE_MATCH(bar, long));
        B1_CC_ASSERT(!B1_TYPE_MATCH(long, bar));
        B1_CC_ASSERT(!B1_TYPE_MATCH(int, long));
        B1_CC_ASSERT(!B1_TYPE_MATCH(int, baz));
        B1_CC_ASSERT(!B1_TYPE_MATCH(baz, int));
        B1_CC_ASSERT(!B1_TYPE_MATCH(foo, baz));

        /* test type-matching with qualifiers */
        B1_CC_ASSERT(B1_TYPE_MATCH(int, const int));
        B1_CC_ASSERT(B1_TYPE_MATCH(const int, const int));
        B1_CC_ASSERT(B1_TYPE_MATCH(const int, int));
        B1_CC_ASSERT(!B1_TYPE_MATCH(int, int*));
        B1_CC_ASSERT(!B1_TYPE_MATCH(int[], int*));
        B1_CC_ASSERT(!B1_TYPE_MATCH(int*, int**));
        B1_CC_ASSERT(B1_TYPE_MATCH(int*, int*));
        B1_CC_ASSERT(B1_TYPE_MATCH(int**, int**));
        B1_CC_ASSERT(B1_TYPE_MATCH(int[], int[]));

        /* test array verification */
        B1_CC_ASSERT(B1_TYPE_IS_ARRAY(int[]));
        B1_CC_ASSERT(B1_TYPE_IS_ARRAY(int*[]));
        B1_CC_ASSERT(!B1_TYPE_IS_ARRAY(int*));
        B1_CC_ASSERT(!B1_TYPE_IS_ARRAY(int**));

        /* silence 'unused variable' warnings */
        foo = 1;
        bar = 1;
        baz = 1;
        assert(foo == bar);
        assert(foo == baz);
        assert(sizeof(array) == sizeof(*array) * 16);
}

/* test static assertions with/without messages on file-context */
B1_CC_STATIC_ASSERT(sizeof(int) > 0);
static_assert(sizeof(int) <= sizeof(long), "Custom error message");

/* test B1_CC_* helpers */
static void test_cc(int non_constant_expr) {
        int foo = -16;
        int bar[8];

        /*
         * Test static assertions with/without messsages on function-context.
         * Those assert-helpers evaluate to a statement, and as such must be
         * valid in function-context.
         */
        B1_CC_STATIC_ASSERT(true);
        static_assert(true, "Custom error message");

        /*
         * Test static assertion in expressions with/without messages. Those
         * helpers evaluate to constant expressions, and as such can be used in
         * any expression.
         * We also explicitly initialize static variables to test whether the
         * values are true constant expressions.
         */
        foo = (B1_CC_ASSERT(true), 5);
        foo = (B1_CC_ASSERT_MSG(true, "Custom error message"), foo + 1);
        B1_CC_ASSERT(B1_CC_ASSERT1(8) == 1);
        assert(foo == 6);

        {
                /* constant expression */
                static int sub = B1_CC_ASSERT_TO(true, 16);
                assert(sub == 16);
        }

        /*
         * Test compile-time conditions. The B1_CC_IF() macro allows evaluation
         * at compile-time, and as such yields exactly one of the code-blocks
         * passed to it (depending on whether the expression is true).
         */
        foo = B1_CC_IF(false, foo + 0, foo + 1);
        foo = B1_CC_IF(true, foo + 4, foo + 8);
        assert(foo == 11);

        /*
         * Test constant-expr checks.
         * The B1_CC_IS_CONST() macro allows verifying whether an expression is
         * constant. The return value of the macro itself is constant, and as
         * such can be used for constant expressions itself.
         */
        B1_CC_ASSERT(B1_CC_IS_CONST(5));
        B1_CC_ASSERT(!B1_CC_IS_CONST(non_constant_expr));
        B1_CC_ASSERT(B1_CC_IS_CONST(B1_CC_IS_CONST(non_constant_expr)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(foo++)); /* *NOT* evaluated */
        assert(foo == 11);

        /*
         * Test stringify/concatenation helpers. Also make sure to test that
         * the passed arguments are evaluated first, before they're stringified
         * and/or concatenated.
         */
#define TEST_TOKEN foobar
        assert(!strcmp("foobar", B1_CC_STRINGIFY(foobar)));
        assert(!strcmp("foobar", B1_CC_STRINGIFY(TEST_TOKEN)));
        assert(!strcmp("foobar", B1_CC_STRINGIFY(B1_CC_CONCATENATE(foo, bar))));
        assert(!strcmp("foobarfoobar", B1_CC_STRINGIFY(B1_CC_CONCATENATE(TEST_TOKEN, foobar))));
        assert(!strcmp("foobarfoobar", B1_CC_STRINGIFY(B1_CC_CONCATENATE(foobar, TEST_TOKEN))));
#undef TEST_TOKEN

        /*
         * Test array-size helper. This simply computes the number of elements
         * of an array, instead of the binary size.
         */
        B1_CC_ASSERT(B1_CC_ARRAY_SIZE(bar) == 8);
        B1_CC_ASSERT(B1_CC_IS_CONST(B1_CC_ARRAY_SIZE(bar)));

        /*
         * Test unique compile-time value. The B1_CC_UNIQUE value evaluates to
         * a compile-time unique value for each time it is used. Hence, it can
         * never compare equal to itself, furthermore, it's evaluated at
         * compile-time, not pre-processor time!
         */
#define TEST_UNIQUE_MACRO B1_CC_UNIQUE
        assert(B1_CC_UNIQUE != B1_CC_UNIQUE);
        assert(TEST_UNIQUE_MACRO != TEST_UNIQUE_MACRO);
        assert(B1_CC_UNIQUE != TEST_UNIQUE_MACRO);
#undef TEST_UNIQUE_MACRO

        /*
         * Test decimal-representation calculator. Make sure it is
         * type-independent and just uses the size of the type to calculate how
         * many bytes are needed to print that integer in decimal form. Also
         * verify that it is a constant expression.
         */
        B1_CC_ASSERT(B1_CC_DECIMAL_MAX(int32_t) == 11);
        B1_CC_ASSERT(B1_CC_DECIMAL_MAX(uint32_t) == 11);
        B1_CC_ASSERT(B1_CC_DECIMAL_MAX(uint64_t) == 21);
        B1_CC_ASSERT(B1_CC_IS_CONST(B1_CC_DECIMAL_MAX(int32_t)));
}

/* test un-categorized macro helpers */
static void test_misc(int non_constant_expr) {
        int foo;

        /*
         * Test b1_container_of(). We cannot test for type-safety, nor for
         * other invalid uses, as they'd require negative compile-testing.
         * However, we can test that the macro yields the correct values under
         * normal use.
         */
        {
                struct foobar {
                        int a;
                        int b;
                } sub = {};

                B1_CC_ASSERT(&sub == b1_container_of(&sub.a, struct foobar, a));
                B1_CC_ASSERT(&sub == b1_container_of(&sub.b, struct foobar, b));
        }

        /*
         * Test min/max macros. Especially check that macro arguments are never
         * evaluated multiple times, and if both arguments are constant, the
         * return value is constant as well.
         */
        foo = 0;
        assert(b1_max(1, 5) == 5);
        assert(b1_max(-1, 5) == 5);
        assert(b1_max(-1, -5) == -1);
        assert(b1_max(foo++, -1) == 0);
        assert(foo == 1);
        assert(b1_max(foo++, foo++) > 0);
        assert(foo == 3);

        B1_CC_ASSERT(B1_CC_IS_CONST(b1_max(1, 5)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_max(1, non_constant_expr)));

        {
                static int sub = b1_max(1, 5);
                assert(sub == 5);
        }

        foo = 0;
        assert(b1_min(1, 5) == 1);
        assert(b1_min(-1, 5) == -1);
        assert(b1_min(-1, -5) == -5);
        assert(b1_min(foo++, 1) == 0);
        assert(foo == 1);
        assert(b1_min(foo++, foo++) > 0);
        assert(foo == 3);

        B1_CC_ASSERT(B1_CC_IS_CONST(b1_min(1, 5)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_min(1, non_constant_expr)));

        {
                static int sub = b1_min(1, 5);
                assert(sub == 1);
        }

        /*
         * Test b1_less_by(), b1_clamp(). Make sure they
         * evaluate arguments exactly once, and yield a constant expression,
         * if all arguments are constant.
         */
        foo = 8;
        assert(b1_less_by(1, 5) == 0);
        assert(b1_less_by(5, 1) == 4);
        assert(b1_less_by(foo++, 1) == 7);
        assert(foo == 9);
        assert(b1_less_by(foo++, foo++) >= 0);
        assert(foo == 11);

        B1_CC_ASSERT(B1_CC_IS_CONST(b1_less_by(1, 5)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_less_by(1, non_constant_expr)));

        foo = 8;
        assert(b1_clamp(foo, 1, 5) == 5);
        assert(b1_clamp(foo, 9, 20) == 9);
        assert(b1_clamp(foo++, 1, 5) == 5);
        assert(foo == 9);
        assert(b1_clamp(foo++, foo++, foo++) >= 0);
        assert(foo == 12);

        B1_CC_ASSERT(B1_CC_IS_CONST(b1_clamp(0, 1, 5)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_clamp(1, 0, non_constant_expr)));

        /*
         * Test b1_negative_errno(). Do this by writing a code-path where gcc
         * couldn't assume that 'errno' is negative, but the helper does
         * provide such clue.
         */
        errno = ENOSYS;
        {
                int uninitialized, sub = b1_negative_errno();

                if (sub < 0)
                        assert(sub == -errno);
                else
                        assert(uninitialized == 0); /* never evaluated */
        }
}

/* test b1_math_* helpers */
static void test_math(int non_constant_expr) {
        int i, j, foo;

        /*
         * Count Leading Zeroes: The b1_math_clz() macro is a type-generic
         * variant of clz(). It counts leading zeroes of an integer. The result
         * highly depends on the integer-width of the input. Make sure it
         * selects the correct implementation.
         * Also note: clz(0) is undefined!
         */
        B1_CC_ASSERT(b1_math_clz(UINT32_C(1)) == 31);
        B1_CC_ASSERT(b1_math_clz(UINT64_C(1)) == 63);

        B1_CC_ASSERT(b1_math_clz(UINT32_C(-1)) == 0);
        B1_CC_ASSERT(b1_math_clz(UINT32_C(-1) + 2) == 31);

        B1_CC_ASSERT(b1_math_clz((uint64_t)UINT32_C(-1)) == 32);
        B1_CC_ASSERT(b1_math_clz((uint64_t)UINT32_C(-1) + 2) == 31);

        /* make sure it's compile-time constant */
        B1_CC_ASSERT(B1_CC_IS_CONST(b1_math_clz(1U)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_math_clz((unsigned int)non_constant_expr)));
        {
                static const int sub = b1_math_clz(1U);
                B1_CC_ASSERT(sub == 31);
        }

        /*
         * Div Round Up: Normal division, but round up to next integer, instead
         * of clipping. Also verify that it does not suffer from the integer
         * overflow in the prevalant, alternative implementation:
         *      [(x + y - 1) / y].
         */
#define TEST_ALT_DIV(_x, _y) (((_x) + (_y) - 1) / (_y))
        foo = 8;
        assert(b1_math_div_round_up(0, 5) == 0);
        assert(b1_math_div_round_up(1, 5) == 1);
        assert(b1_math_div_round_up(5, 5) == 1);
        assert(b1_math_div_round_up(6, 5) == 2);
        assert(b1_math_div_round_up(foo++, 1) == 8);
        assert(foo == 9);
        assert(b1_math_div_round_up(foo++, foo++) >= 0);
        assert(foo == 11);

        B1_CC_ASSERT(B1_CC_IS_CONST(b1_math_div_round_up(1, 5)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_math_div_round_up(1, non_constant_expr)));

        /* alternative calculation is [(x + y - 1) / y], but it may overflow */
        for (i = 0; i <= 0xffff; ++i) {
                for (j = 1; j <= 0xff; ++j)
                        assert(b1_math_div_round_up(i, j) == TEST_ALT_DIV(i, j));
                for (j = 0xff00; j <= 0xffff; ++j)
                        assert(b1_math_div_round_up(i, j) == TEST_ALT_DIV(i, j));
        }

        /* make sure it doesn't suffer from high overflow */
        assert(UINT32_C(0xfffffffa) % 10 == 0);
        assert(UINT32_C(0xfffffffa) / 10 == UINT32_C(429496729));
        assert(b1_math_div_round_up(UINT32_C(0xfffffffa), 10) == UINT32_C(429496729));
        assert(TEST_ALT_DIV(UINT32_C(0xfffffffa), 10) == 0); /* overflow */

        assert(UINT32_C(0xfffffffd) % 10 == 3);
        assert(UINT32_C(0xfffffffd) / 10 == UINT32_C(429496729));
        assert(b1_math_div_round_up(UINT32_C(0xfffffffd), 10) == UINT32_C(429496730));
        assert(TEST_ALT_DIV(UINT32_C(0xfffffffd), 10) == 0);
#undef TEST_ALT_DIV

        /*
         * Align to multiple of: Test the alignment macro. Check that it does
         * not suffer from incorrect integer overflows, neither should it
         * exceed the boundaries of the input type.
         */
        assert(b1_math_align_to(UINT32_C(0), 1) == 0);
        assert(b1_math_align_to(UINT32_C(0), 2) == 0);
        assert(b1_math_align_to(UINT32_C(0), 4) == 0);
        assert(b1_math_align_to(UINT32_C(0), 8) == 0);
        assert(b1_math_align_to(UINT32_C(1), 8) == 8);

        assert(b1_math_align_to(UINT32_C(0xffffffff), 8) == 0);
        assert(b1_math_align_to(UINT32_C(0xfffffff1), 8) == 0xfffffff8);
        assert(b1_math_align_to(UINT32_C(0xfffffff1), 8) == 0xfffffff8);

        B1_CC_ASSERT(B1_CC_IS_CONST(b1_math_align_to(16, 8)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_math_align_to(non_constant_expr, 8)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_math_align_to(16, non_constant_expr)));
        B1_CC_ASSERT(!B1_CC_IS_CONST(b1_math_align_to(16, non_constant_expr ? 8 : 16)));
        B1_CC_ASSERT(B1_CC_IS_CONST(b1_math_align_to(16, 7 + 1)));
        assert(b1_math_align_to(15, non_constant_expr ? 8 : 16) == 16);

        for (i = 0; i < 0xffff; ++i) {
                assert(b1_math_align(i) == b1_math_align_to(i, (int)sizeof(void*)));
                assert(b1_math_align8(i) == b1_math_align_to(i, 8));
        }

        /*
         * Align Power2: The b1_math_align_power2() macro aligns passed values to the
         * next power of 2. Special cases: 0->0, overflow->0
         * Also make sure it never performs an up-cast on overflow.
         */
        assert(b1_math_align_power2(UINT32_C(0)) == 0);
        assert(b1_math_align_power2(UINT32_C(0x80000001)) == 0);
        assert(b1_math_align_power2(UINT64_C(0)) == 0);
        assert(b1_math_align_power2(UINT64_C(0x8000000000000001)) == 0);

        assert(b1_math_align_power2((uint64_t)UINT32_C(0)) == 0);
        assert(b1_math_align_power2((uint64_t)UINT32_C(0x80000001)) == UINT64_C(0x100000000));

        assert(b1_math_align_power2(UINT32_C(1)) == 1);
        assert(b1_math_align_power2(UINT32_C(2)) == 2);
        assert(b1_math_align_power2(UINT32_C(3)) == 4);
        assert(b1_math_align_power2(UINT32_C(4)) == 4);
        assert(b1_math_align_power2(UINT32_C(5)) == 8);
        assert(b1_math_align_power2(UINT32_C(0x80000000)) == UINT32_C(0x80000000));
}

/* test b1_bitmap_* helpers */
static void test_bitmap(void) {
        uint8_t bitmap[] = {
                0xff, 0x00,
                0x80, 0xf0,
                0x04, 0xff,
                0x00, 0x00,

                0xff, 0xff,
                0x00, 0x00,
                0x00, 0x00,
                0xff, 0xff,
        };
        unsigned int i, j;

        /*
         * Verify the bitmap test/set functions work correctly, given our
         * pre-initialized bitmap.
         */

        /* 0-15 */
        for (i = 0; i < 8; ++i)
                assert(b1_bitmap_test(bitmap, i));
        for ( ; i < 16; ++i)
                assert(!b1_bitmap_test(bitmap, i));

        /* 16-31 */
        for ( ; i < 23; ++i)
                assert(!b1_bitmap_test(bitmap, i));
        assert(b1_bitmap_test(bitmap, i++));
        for ( ; i < 28; ++i)
                assert(!b1_bitmap_test(bitmap, i));
        for ( ; i < 32; ++i)
                assert(b1_bitmap_test(bitmap, i));

        /* 32-47 */
        for ( ; i < 34; ++i)
                assert(!b1_bitmap_test(bitmap, i));
        assert(b1_bitmap_test(bitmap, i++));
        for ( ; i < 40; ++i)
                assert(!b1_bitmap_test(bitmap, i));
        for ( ; i < 48; ++i)
                assert(b1_bitmap_test(bitmap, i));

        /* 48-63 */
        for ( ; i < 64; ++i)
                assert(!b1_bitmap_test(bitmap, i));

        /* 64-79 */
        for ( ; i < 80; ++i)
                assert(b1_bitmap_test(bitmap, i));

        /* 80-95 */
        for ( ; i < 96; ++i)
                assert(!b1_bitmap_test(bitmap, i));

        /* 96-111 */
        for ( ; i < 112; ++i)
                assert(!b1_bitmap_test(bitmap, i));

        /* 112-127 */
        for ( ; i < 128; ++i)
                assert(b1_bitmap_test(bitmap, i));

        /*
         * Verify that set_all/clear_all works correctly on our bitmap. Make
         * sure to run both twice, to verify it works even on uninitialized
         * maps.
         */

        b1_bitmap_set_all(bitmap, sizeof(bitmap) * 8);
        for (i = 0; i < sizeof(bitmap) * 8; ++i)
                assert(b1_bitmap_test(bitmap, i));

        b1_bitmap_clear_all(bitmap, sizeof(bitmap) * 8);
        for (i = 0; i < sizeof(bitmap) * 8; ++i)
                assert(!b1_bitmap_test(bitmap, i));

        b1_bitmap_set_all(bitmap, sizeof(bitmap) * 8);
        for (i = 0; i < sizeof(bitmap) * 8; ++i)
                assert(b1_bitmap_test(bitmap, i));

        b1_bitmap_clear_all(bitmap, sizeof(bitmap) * 8);
        for (i = 0; i < sizeof(bitmap) * 8; ++i)
                assert(!b1_bitmap_test(bitmap, i));

        /*
         * Verify that set/clear affect only a single bit. We do this by
         * setting/clearing just a single bit, while keeping all other set to
         * the inverse. Then we check that just a single bit was flipped.
         */

        b1_bitmap_clear_all(bitmap, sizeof(bitmap) * 8);
        for (i = 0; i < sizeof(bitmap) * 8; ++i) {
                b1_bitmap_set(bitmap, i);
                for (j = 0; j < sizeof(bitmap) * 8; ++j)
                        assert(b1_bitmap_test(bitmap, j) == (i == j));
                b1_bitmap_clear(bitmap, i);
                for (j = 0; j < sizeof(bitmap) * 8; ++j)
                        assert(!b1_bitmap_test(bitmap, j));
        }

        b1_bitmap_set_all(bitmap, sizeof(bitmap) * 8);
        for (i = 0; i < sizeof(bitmap) * 8; ++i) {
                b1_bitmap_clear(bitmap, i);
                for (j = 0; j < sizeof(bitmap) * 8; ++j)
                        assert(b1_bitmap_test(bitmap, j) == (i != j));
                b1_bitmap_set(bitmap, i);
                for (j = 0; j < sizeof(bitmap) * 8; ++j)
                        assert(b1_bitmap_test(bitmap, j));
        }
}

int main(int argc, char **argv) {
        test_type();
        test_cc(argc);
        test_misc(argc);
        test_math(argc);
        test_bitmap();

        return 0;
}
