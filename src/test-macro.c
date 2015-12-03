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
 * Tests for <c-macro.h>
 * Bunch of tests for all macros exported by c-macro.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c-macro.h"

/* test C_TYPE_* helpers */
static void test_type(void) {
        int foo, bar, array[16] = {};
        long baz;

        /* test type-matching with mixed explicit types and variables */
        C_CC_ASSERT(C_TYPE_MATCH(foo, int));
        C_CC_ASSERT(C_TYPE_MATCH(int, foo));
        C_CC_ASSERT(C_TYPE_MATCH(int, int));
        C_CC_ASSERT(C_TYPE_MATCH(foo, foo));
        C_CC_ASSERT(C_TYPE_MATCH(foo, bar));
        C_CC_ASSERT(C_TYPE_MATCH(bar, foo));
        C_CC_ASSERT(C_TYPE_MATCH(baz, long));
        C_CC_ASSERT(!C_TYPE_MATCH(bar, long));
        C_CC_ASSERT(!C_TYPE_MATCH(long, bar));
        C_CC_ASSERT(!C_TYPE_MATCH(int, long));
        C_CC_ASSERT(!C_TYPE_MATCH(int, baz));
        C_CC_ASSERT(!C_TYPE_MATCH(baz, int));
        C_CC_ASSERT(!C_TYPE_MATCH(foo, baz));

        /* test type-matching with qualifiers */
        C_CC_ASSERT(C_TYPE_MATCH(int, const int));
        C_CC_ASSERT(C_TYPE_MATCH(const int, const int));
        C_CC_ASSERT(C_TYPE_MATCH(const int, int));
        C_CC_ASSERT(!C_TYPE_MATCH(int, int*));
        C_CC_ASSERT(!C_TYPE_MATCH(int[], int*));
        C_CC_ASSERT(!C_TYPE_MATCH(int*, int**));
        C_CC_ASSERT(C_TYPE_MATCH(int*, int*));
        C_CC_ASSERT(C_TYPE_MATCH(int**, int**));
        C_CC_ASSERT(C_TYPE_MATCH(int[], int[]));

        /* test array verification */
        C_CC_ASSERT(C_TYPE_IS_ARRAY(int[]));
        C_CC_ASSERT(C_TYPE_IS_ARRAY(int*[]));
        C_CC_ASSERT(!C_TYPE_IS_ARRAY(int*));
        C_CC_ASSERT(!C_TYPE_IS_ARRAY(int**));

        /* silence 'unused variable' warnings */
        foo = 1;
        bar = 1;
        baz = 1;
        assert(foo == bar);
        assert(foo == baz);
        assert(sizeof(array) == sizeof(*array) * 16);
}

/* test static assertions with/without messages on file-context */
C_CC_STATIC_ASSERT(sizeof(int) > 0);
static_assert(sizeof(int) <= sizeof(long), "Custom error message");

/* test C_CC_* helpers */
static void test_cc(int non_constant_expr) {
        int foo = -16;
        int bar[8];

        /*
         * Test static assertions with/without messsages on function-context.
         * Those assert-helpers evaluate to a statement, and as such must be
         * valid in function-context.
         */
        C_CC_STATIC_ASSERT(true);
        static_assert(true, "Custom error message");

        /*
         * Test static assertion in expressions with/without messages. Those
         * helpers evaluate to constant expressions, and as such can be used in
         * any expression.
         * We also explicitly initialize static variables to test whether the
         * values are true constant expressions.
         */
        foo = (C_CC_ASSERT(true), 5);
        foo = (C_CC_ASSERT_MSG(true, "Custom error message"), foo + 1);
        C_CC_ASSERT(C_CC_ASSERT1(8) == 1);
        assert(foo == 6);

        {
                /* constant expression */
                static int sub = C_CC_ASSERT_TO(true, 16);
                assert(sub == 16);
        }

        /*
         * Test array-size helper. This simply computes the number of elements
         * of an array, instead of the binary size.
         */
        C_CC_ASSERT(C_CC_ARRAY_SIZE(bar) == 8);
        C_CC_ASSERT(C_CC_IS_CONST(C_CC_ARRAY_SIZE(bar)));

        /*
         * Test decimal-representation calculator. Make sure it is
         * type-independent and just uses the size of the type to calculate how
         * many bytes are needed to print that integer in decimal form. Also
         * verify that it is a constant expression.
         */
        C_CC_ASSERT(C_CC_DECIMAL_MAX(int32_t) == 11);
        C_CC_ASSERT(C_CC_DECIMAL_MAX(uint32_t) == 11);
        C_CC_ASSERT(C_CC_DECIMAL_MAX(uint64_t) == 21);
        C_CC_ASSERT(C_CC_IS_CONST(C_CC_DECIMAL_MAX(int32_t)));
}

/* test un-categorized macro helpers */
static void test_misc(int non_constant_expr) {
        int foo;

        /*
         * Test c_container_of(). We cannot test for type-safety, nor for
         * other invalid uses, as they'd require negative compile-testing.
         * However, we can test that the macro yields the correct values under
         * normal use.
         */
        {
                struct foobar {
                        int a;
                        int b;
                } sub = {};

                C_CC_ASSERT(&sub == c_container_of(&sub.a, struct foobar, a));
                C_CC_ASSERT(&sub == c_container_of(&sub.b, struct foobar, b));
        }

        /*
         * Test min/max macros. Especially check that macro arguments are never
         * evaluated multiple times, and if both arguments are constant, the
         * return value is constant as well.
         */
        foo = 0;
        assert(c_max(1, 5) == 5);
        assert(c_max(-1, 5) == 5);
        assert(c_max(-1, -5) == -1);
        assert(c_max(foo++, -1) == 0);
        assert(foo == 1);
        assert(c_max(foo++, foo++) > 0);
        assert(foo == 3);

        C_CC_ASSERT(C_CC_IS_CONST(c_max(1, 5)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_max(1, non_constant_expr)));

        {
                static int sub = c_max(1, 5);
                assert(sub == 5);
        }

        foo = 0;
        assert(c_min(1, 5) == 1);
        assert(c_min(-1, 5) == -1);
        assert(c_min(-1, -5) == -5);
        assert(c_min(foo++, 1) == 0);
        assert(foo == 1);
        assert(c_min(foo++, foo++) > 0);
        assert(foo == 3);

        C_CC_ASSERT(C_CC_IS_CONST(c_min(1, 5)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_min(1, non_constant_expr)));

        {
                static int sub = c_min(1, 5);
                assert(sub == 1);
        }

        /*
         * Test c_less_by(), c_clamp(). Make sure they
         * evaluate arguments exactly once, and yield a constant expression,
         * if all arguments are constant.
         */
        foo = 8;
        assert(c_less_by(1, 5) == 0);
        assert(c_less_by(5, 1) == 4);
        assert(c_less_by(foo++, 1) == 7);
        assert(foo == 9);
        assert(c_less_by(foo++, foo++) >= 0);
        assert(foo == 11);

        C_CC_ASSERT(C_CC_IS_CONST(c_less_by(1, 5)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_less_by(1, non_constant_expr)));

        foo = 8;
        assert(c_clamp(foo, 1, 5) == 5);
        assert(c_clamp(foo, 9, 20) == 9);
        assert(c_clamp(foo++, 1, 5) == 5);
        assert(foo == 9);
        assert(c_clamp(foo++, foo++, foo++) >= 0);
        assert(foo == 12);

        C_CC_ASSERT(C_CC_IS_CONST(c_clamp(0, 1, 5)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_clamp(1, 0, non_constant_expr)));

        /*
         * Test c_negative_errno(). Do this by writing a code-path where gcc
         * couldn't assume that 'errno' is negative, but the helper does
         * provide such clue.
         */
        errno = ENOSYS;
        {
                int uninitialized, sub = c_negative_errno();

                if (sub < 0)
                        assert(sub == -errno);
                else
                        assert(uninitialized == 0); /* never evaluated */
        }
}

/* test c_math_* helpers */
static void test_math(int non_constant_expr) {
        int i;

        /*
         * Count Leading Zeroes: The c_math_clz() macro is a type-generic
         * variant of clz(). It counts leading zeroes of an integer. The result
         * highly depends on the integer-width of the input. Make sure it
         * selects the correct implementation.
         * Also note: clz(0) is undefined!
         */
        C_CC_ASSERT(c_math_clz(UINT32_C(1)) == 31);
        C_CC_ASSERT(c_math_clz(UINT64_C(1)) == 63);

        C_CC_ASSERT(c_math_clz(UINT32_C(-1)) == 0);
        C_CC_ASSERT(c_math_clz(UINT32_C(-1) + 2) == 31);

        C_CC_ASSERT(c_math_clz((uint64_t)UINT32_C(-1)) == 32);
        C_CC_ASSERT(c_math_clz((uint64_t)UINT32_C(-1) + 2) == 31);

        /* make sure it's compile-time constant */
        C_CC_ASSERT(C_CC_IS_CONST(c_math_clz(1U)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_math_clz((unsigned int)non_constant_expr)));
        {
                static const int sub = c_math_clz(1U);
                C_CC_ASSERT(sub == 31);
        }

        /*
         * Align to multiple of: Test the alignment macro. Check that it does
         * not suffer from incorrect integer overflows, neither should it
         * exceed the boundaries of the input type.
         */
        assert(c_math_align_to(UINT32_C(0), 1) == 0);
        assert(c_math_align_to(UINT32_C(0), 2) == 0);
        assert(c_math_align_to(UINT32_C(0), 4) == 0);
        assert(c_math_align_to(UINT32_C(0), 8) == 0);
        assert(c_math_align_to(UINT32_C(1), 8) == 8);

        assert(c_math_align_to(UINT32_C(0xffffffff), 8) == 0);
        assert(c_math_align_to(UINT32_C(0xfffffff1), 8) == 0xfffffff8);
        assert(c_math_align_to(UINT32_C(0xfffffff1), 8) == 0xfffffff8);

        C_CC_ASSERT(C_CC_IS_CONST(c_math_align_to(16, 8)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_math_align_to(non_constant_expr, 8)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_math_align_to(16, non_constant_expr)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_math_align_to(16, non_constant_expr ? 8 : 16)));
        C_CC_ASSERT(C_CC_IS_CONST(c_math_align_to(16, 7 + 1)));
        assert(c_math_align_to(15, non_constant_expr ? 8 : 16) == 16);

        for (i = 0; i < 0xffff; ++i) {
                assert(c_math_align(i) == c_math_align_to(i, (int)sizeof(void*)));
                assert(c_math_align8(i) == c_math_align_to(i, 8));
        }

        /*
         * Align Power2: The c_math_align_power2() macro aligns passed values to the
         * next power of 2. Special cases: 0->0, overflow->0
         * Also make sure it never performs an up-cast on overflow.
         */
        assert(c_math_align_power2(UINT32_C(0)) == 0);
        assert(c_math_align_power2(UINT32_C(0x80000001)) == 0);
        assert(c_math_align_power2(UINT64_C(0)) == 0);
        assert(c_math_align_power2(UINT64_C(0x8000000000000001)) == 0);

        assert(c_math_align_power2((uint64_t)UINT32_C(0)) == 0);
        assert(c_math_align_power2((uint64_t)UINT32_C(0x80000001)) == UINT64_C(0x100000000));

        assert(c_math_align_power2(UINT32_C(1)) == 1);
        assert(c_math_align_power2(UINT32_C(2)) == 2);
        assert(c_math_align_power2(UINT32_C(3)) == 4);
        assert(c_math_align_power2(UINT32_C(4)) == 4);
        assert(c_math_align_power2(UINT32_C(5)) == 8);
        assert(c_math_align_power2(UINT32_C(0x80000000)) == UINT32_C(0x80000000));
}

int main(int argc, char **argv) {
        test_type();
        test_cc(argc);
        test_misc(argc);
        test_math(argc);
        return 0;
}
