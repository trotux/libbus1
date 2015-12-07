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

#undef NDEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bus1/c-macro.h"

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

/* test static assertions on file-context */
static_assert(sizeof(int) <= sizeof(long), "Custom error message");

/* test C_CC_* helpers */
static void test_cc(int non_constant_expr) {
        int foo;
        int bar[8];

        /*
         * Test compile-time conditions. The C_CC_IF() macro allows evaluation
         * at compile-time, and as such yields exactly one of the code-blocks
         * passed to it (depending on whether the expression is true).
         */
        foo = 6;
        foo = C_CC_IF(false, foo + 0, foo + 1);
        foo = C_CC_IF(true, foo + 4, foo + 8);
        assert(foo == 11);

        /*
         * Test constant-expr checks.
         * The C_CC_IS_CONST() macro allows verifying whether an expression is
         * constant. The return value of the macro itself is constant, and as
         * such can be used for constant expressions itself.
         */
        foo = 11;
        C_CC_ASSERT(C_CC_IS_CONST(5));
        C_CC_ASSERT(!C_CC_IS_CONST(non_constant_expr));
        C_CC_ASSERT(C_CC_IS_CONST(C_CC_IS_CONST(non_constant_expr)));
        C_CC_ASSERT(!C_CC_IS_CONST(foo++)); /* *NOT* evaluated */
        assert(foo == 11);

        /*
         * Test static assertions on function-context.
         * Those assert-helpers evaluate to a statement, and as such must be
         * valid in function-context.
         */
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
        C_CC_ASSERT(C_ARRAY_SIZE(bar) == 8);
        C_CC_ASSERT(C_CC_IS_CONST(C_ARRAY_SIZE(bar)));

        /*
         * Test decimal-representation calculator. Make sure it is
         * type-independent and just uses the size of the type to calculate how
         * many bytes are needed to print that integer in decimal form. Also
         * verify that it is a constant expression.
         */
        C_CC_ASSERT(C_DECIMAL_MAX(int32_t) == 11);
        C_CC_ASSERT(C_DECIMAL_MAX(uint32_t) == 11);
        C_CC_ASSERT(C_DECIMAL_MAX(uint64_t) == 21);
        C_CC_ASSERT(C_CC_IS_CONST(C_DECIMAL_MAX(int32_t)));

        /*
         * Test stringify/concatenation helpers. Also make sure to test that
         * the passed arguments are evaluated first, before they're stringified
         * and/or concatenated.
         */
#define TEST_TOKEN foobar
        assert(!strcmp("foobar", C_STRINGIFY(foobar)));
        assert(!strcmp("foobar", C_STRINGIFY(TEST_TOKEN)));
        assert(!strcmp("foobar", C_STRINGIFY(C_CONCATENATE(foo, bar))));
        assert(!strcmp("foobarfoobar", C_STRINGIFY(C_CONCATENATE(TEST_TOKEN, foobar))));
        assert(!strcmp("foobarfoobar", C_STRINGIFY(C_CONCATENATE(foobar, TEST_TOKEN))));
#undef TEST_TOKEN

        /*
         * Test unique compile-time value. The C_CC_UNIQUE value evaluates to
         * a compile-time unique value for each time it is used. Hence, it can
         * never compare equal to itself, furthermore, it's evaluated at
         * compile-time, not pre-processor time!
         */
#define TEST_UNIQUE_MACRO C_CC_UNIQUE
        assert(C_CC_UNIQUE != C_CC_UNIQUE);
        assert(TEST_UNIQUE_MACRO != TEST_UNIQUE_MACRO);
        assert(C_CC_UNIQUE != TEST_UNIQUE_MACRO);
#undef TEST_UNIQUE_MACRO

        /*
         * Test C_VAR() macro. It's sole purpose is to create a valid C
         * identifier given a single argument (which itself must be a valid
         * identifier).
         * Just test that we can declare variables with it and use it in
         * expressions.
         */
        {
                int C_VAR(sub, UNIQUE) = 5;
                /* make sure the variable name does not clash */
                int sub = 12, subUNIQUE = 12, UNIQUEsub = 12;

                assert(7 + C_VAR(sub, UNIQUE) == sub);
                assert(sub == subUNIQUE);
                assert(sub == UNIQUEsub);
        }
        {
                /*
                 * Make sure both produce different names, even though they're
                 * exactly the same expression.
                 */
                _c_unused_ int C_VAR(sub, C_CC_UNIQUE), C_VAR(sub, C_CC_UNIQUE);
        }
        {
                /* verify C_VAR() with single argument works line-based */
                int C_VAR(sub); C_VAR(sub) = 5; assert(C_VAR(sub) == 5);
        }
        {
                /* verify C_VAR() with no argument works line-based */
                int C_VAR(); C_VAR() = 5; assert(C_VAR() == 5);
        }
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

/* test math helpers */
static void test_math(int non_constant_expr) {
        int i, j, foo;

        /*
         * Count Leading Zeroes: The c_clz() macro is a type-generic
         * variant of clz(). It counts leading zeroes of an integer. The result
         * highly depends on the integer-width of the input. Make sure it
         * selects the correct implementation.
         * Also note: clz(0) is undefined!
         */
        C_CC_ASSERT(c_clz(UINT32_C(1)) == 31);
        C_CC_ASSERT(c_clz(UINT64_C(1)) == 63);

        C_CC_ASSERT(c_clz(UINT32_C(-1)) == 0);
        C_CC_ASSERT(c_clz(UINT32_C(-1) + 2) == 31);

        C_CC_ASSERT(c_clz((uint64_t)UINT32_C(-1)) == 32);
        C_CC_ASSERT(c_clz((uint64_t)UINT32_C(-1) + 2) == 31);

        /* make sure it's compile-time constant */
        C_CC_ASSERT(C_CC_IS_CONST(c_clz(1U)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_clz((unsigned int)non_constant_expr)));
        {
                static const int sub = c_clz(1U);
                C_CC_ASSERT(sub == 31);
        }

        /*
         * Align to multiple of: Test the alignment macro. Check that it does
         * not suffer from incorrect integer overflows, neither should it
         * exceed the boundaries of the input type.
         */
        assert(c_align_to(UINT32_C(0), 1) == 0);
        assert(c_align_to(UINT32_C(0), 2) == 0);
        assert(c_align_to(UINT32_C(0), 4) == 0);
        assert(c_align_to(UINT32_C(0), 8) == 0);
        assert(c_align_to(UINT32_C(1), 8) == 8);

        assert(c_align_to(UINT32_C(0xffffffff), 8) == 0);
        assert(c_align_to(UINT32_C(0xfffffff1), 8) == 0xfffffff8);
        assert(c_align_to(UINT32_C(0xfffffff1), 8) == 0xfffffff8);

        C_CC_ASSERT(C_CC_IS_CONST(c_align_to(16, 8)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_align_to(non_constant_expr, 8)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_align_to(16, non_constant_expr)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_align_to(16, non_constant_expr ? 8 : 16)));
        C_CC_ASSERT(C_CC_IS_CONST(c_align_to(16, 7 + 1)));
        assert(c_align_to(15, non_constant_expr ? 8 : 16) == 16);

        for (i = 0; i < 0xffff; ++i) {
                assert(c_align(i) == c_align_to(i, (int)sizeof(void*)));
                assert(c_align8(i) == c_align_to(i, 8));
        }

        /*
         * Align Power2: The c_align_power2() macro aligns passed values to the
         * next power of 2. Special cases: 0->0, overflow->0
         * Also make sure it never performs an up-cast on overflow.
         */
        assert(c_align_power2(UINT32_C(0)) == 0);
        assert(c_align_power2(UINT32_C(0x80000001)) == 0);
        assert(c_align_power2(UINT64_C(0)) == 0);
        assert(c_align_power2(UINT64_C(0x8000000000000001)) == 0);

        assert(c_align_power2((uint64_t)UINT32_C(0)) == 0);
        assert(c_align_power2((uint64_t)UINT32_C(0x80000001)) == UINT64_C(0x100000000));

        assert(c_align_power2(UINT32_C(1)) == 1);
        assert(c_align_power2(UINT32_C(2)) == 2);
        assert(c_align_power2(UINT32_C(3)) == 4);
        assert(c_align_power2(UINT32_C(4)) == 4);
        assert(c_align_power2(UINT32_C(5)) == 8);
        assert(c_align_power2(UINT32_C(0x80000000)) == UINT32_C(0x80000000));

        /*
         * Div Round Up: Normal division, but round up to next integer, instead
         * of clipping. Also verify that it does not suffer from the integer
         * overflow in the prevalant, alternative implementation:
         *      [(x + y - 1) / y].
         */
#define TEST_ALT_DIV(_x, _y) (((_x) + (_y) - 1) / (_y))
        foo = 8;
        assert(c_div_round_up(0, 5) == 0);
        assert(c_div_round_up(1, 5) == 1);
        assert(c_div_round_up(5, 5) == 1);
        assert(c_div_round_up(6, 5) == 2);
        assert(c_div_round_up(foo++, 1) == 8);
        assert(foo == 9);
        assert(c_div_round_up(foo++, foo++) >= 0);
        assert(foo == 11);

        C_CC_ASSERT(C_CC_IS_CONST(c_div_round_up(1, 5)));
        C_CC_ASSERT(!C_CC_IS_CONST(c_div_round_up(1, non_constant_expr)));

        /* alternative calculation is [(x + y - 1) / y], but it may overflow */
        for (i = 0; i <= 0xffff; ++i) {
                for (j = 1; j <= 0xff; ++j)
                        assert(c_div_round_up(i, j) == TEST_ALT_DIV(i, j));
                for (j = 0xff00; j <= 0xffff; ++j)
                        assert(c_div_round_up(i, j) == TEST_ALT_DIV(i, j));
        }

        /* make sure it doesn't suffer from high overflow */
        assert(UINT32_C(0xfffffffa) % 10 == 0);
        assert(UINT32_C(0xfffffffa) / 10 == UINT32_C(429496729));
        assert(c_div_round_up(UINT32_C(0xfffffffa), 10) == UINT32_C(429496729));
        assert(TEST_ALT_DIV(UINT32_C(0xfffffffa), 10) == 0); /* overflow */

        assert(UINT32_C(0xfffffffd) % 10 == 3);
        assert(UINT32_C(0xfffffffd) / 10 == UINT32_C(429496729));
        assert(c_div_round_up(UINT32_C(0xfffffffd), 10) == UINT32_C(429496730));
        assert(TEST_ALT_DIV(UINT32_C(0xfffffffd), 10) == 0);
#undef TEST_ALT_DIV
}

int main(int argc, char **argv) {
        test_type();
        test_cc(argc);
        test_misc(argc);
        test_math(argc);
        return 0;
}
