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
 * Tests for <bus1-public.h>
 * Bunch of tests for macros provided by the public macro header. The header
 * contains stuff from several different categories, which just share that
 * they are included by nearly everyone else. Thus, the tests here resemble the
 * tests of the respective category of each functionality.
 *
 * If you move stuff to bus1-public.h, move the respective test here as well.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bus1-macro.h"
#include "bus1-public.h"

/* test public B1_CC_* helpers */
static void test_cc(int non_constant_expr) {
        int foo;

        /*
         * Test compile-time conditions. The B1_CC_IF() macro allows evaluation
         * at compile-time, and as such yields exactly one of the code-blocks
         * passed to it (depending on whether the expression is true).
         */
        foo = 6;
        foo = B1_CC_IF(false, foo + 0, foo + 1);
        foo = B1_CC_IF(true, foo + 4, foo + 8);
        assert(foo == 11);

        /*
         * Test constant-expr checks.
         * The B1_CC_IS_CONST() macro allows verifying whether an expression is
         * constant. The return value of the macro itself is constant, and as
         * such can be used for constant expressions itself.
         */
        foo = 11;
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
         * Test B1_VAR() macro. It's sole purpose is to create a valid C
         * identifier given a single argument (which itself must be a valid
         * identifier).
         * Just test that we can declare variables with it and use it in
         * expressions.
         */
        {
                int B1_VAR(sub, UNIQUE) = 5;
                /* make sure the variable name does not clash */
                int sub = 12, subUNIQUE = 12, UNIQUEsub = 12;

                assert(7 + B1_VAR(sub, UNIQUE) == sub);
                assert(sub == subUNIQUE);
                assert(sub == UNIQUEsub);
        }
}

/* test public b1_math_* helpers */
static void test_math(int non_constant_expr) {
        int i, j, foo;

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
}

int main(int argc, char **argv) {
        test_cc(argc);
        test_math(argc);
        return 0;
}
