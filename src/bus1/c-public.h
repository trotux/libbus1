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
 * Public Macros
 * Top level header that provides macros needed in public APIs. This header
 * does *NOT* implement a specific subsystem, but rather contains macros from
 * other subsystems, which are needed in public headers. That is, whenever you
 * need a macro in your public header, move it here. If you merely want to
 * expose your API, do it in your own header. This header just contains stuff
 * that is include in *any* public header.
 *
 * KEEP THIS HEADER CLEAN! Only move stuff here, if you really need it in your
 * public header. This usually implies that it is a compile-time macro (at
 * least partially), or a compile-time constant. Any runtime macros/functions
 * should rather be put into your source file.
 */

#include <limits.h>
#include <stddef.h>
/* must not depend on any other c-header */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * We require:
 *   sizeof(void*) == sizeof(long)
 *   sizeof(long) == 4 || sizeof(long) == 8
 *   sizeof(int) == 4
 * The linux kernel requires the same from the toolchain, so this should work
 * just fine.
 */
#if __SIZEOF_POINTER__ != __SIZEOF_LONG__
#  error "sizeof(void*) != sizeof(long)"
#elif __SIZEOF_LONG__ != 4 && __SIZEOF_LONG__ != 8
#  error "sizeof(long) != 4 && sizeof(long) != 8"
#elif __SIZEOF_INT__ != 4
#  error "sizeof(int) != 4"
#endif

/*
 * Shortcuts for gcc attributes. See GCC manual for details. They're 1-to-1
 * mappings to the GCC equivalents. No additional magic here.
 */
#define _c_align_(_x) __attribute__((__aligned__(_x)))
#define _c_alignas_(_x) __attribute__((__aligned__(__alignof(_x))))
#define _c_alloc_(...) __attribute__((__alloc_size__(__VA_ARGS__)))
#define _c_cleanup_(_x) __attribute__((__cleanup__(_x)))
#define _c_const_ __attribute__((__const__))
#define _c_deprecated_ __attribute__((__deprecated__))
#define _c_hidden_ __attribute__((__visibility__("hidden")))
#define _c_likely_(_x) (__builtin_expect(!!(_x), 1))
#define _c_malloc_ __attribute__((__malloc__))
#define _c_packed_ __attribute__((__packed__))
#define _c_printf_(_a, _b) __attribute__((__format__(printf, _a, _b)))
#define _c_public_ __attribute__((__visibility__("default")))
#define _c_pure_ __attribute__((__pure__))
#define _c_sentinel_ __attribute__((__sentinel__))
#define _c_unlikely_(_x) (__builtin_expect(!!(_x), 0))
#define _c_unused_ __attribute__((__unused__))
#define _c_weak_ __attribute__((__weak__))
#define _c_weakref_(_x) __attribute__((__weakref__(#_x)))

/**
 * C_CC_IF() - conditional expression at compile time
 * @_cond:      condition
 * @_if:        if-clause
 * @_else:      else-clause
 *
 * This is a compile-time if-else-statement. Depending on whether the constant
 * expression @_cond is true or false, this evaluates to the passed clause. The
 * other clause is *not* evaluated, however, it may be checked for syntax
 * errors and *constant* expressions are evaluated.
 *
 * Return: Evaluates to either if-clause or else-clause, depending on whether
 *         the condition is true. The other clause is *not* evaluated.
 */
#define C_CC_IF(_cond, _if, _else) __builtin_choose_expr(!!(_cond), _if, _else)

/**
 * C_CC_IS_CONST() - check whether a value is known at compile time
 * @_expr:      expression
 *
 * This checks whether the value of @_expr is known at compile time. Note that
 * a negative result does not mean that it is *NOT* known. However, it means
 * that it cannot be guaranteed to be constant at compile time. Hence, false
 * negatives are possible.
 *
 * This macro *always* evaluates to a constant expression, regardless whether
 * the passed expression is constant.
 *
 * The passed in expression is *never* evaluated. Hence, it can safely be used
 * in combination with C_CC_IF() to avoid multiple evaluations of macro
 * parameters.
 *
 * Return: 1 if constant, 0 if not.
 */
#define C_CC_IS_CONST(_expr) __builtin_constant_p(_expr)

/**
 * C_CC_STRINGIFY() - stringify a token, but evaluate it first
 * @_x:         token to evaluate and stringify
 *
 * Return: Evaluates to a constant string literal
 */
#define C_CC_STRINGIFY(_x) C_INTERNAL_CC_STRINGIFY(_x)
#define C_INTERNAL_CC_STRINGIFY(_x) #_x

/**
 * C_CC_CONCATENATE() - concatenate two tokens, but evaluate them first
 * @_x:         first token
 * @_y:         second token
 *
 * Return: Evaluates to a constant identifier
 */
#define C_CC_CONCATENATE(_x, _y) C_INTERNAL_CC_CONCATENATE(_x, _y)
#define C_INTERNAL_CC_CONCATENATE(_x, _y) _x ## _y

/**
 * C_CC_UNIQUE - generate unique compile-time integer
 *
 * This evaluates to a unique compile-time integer. Each occurrence of this
 * macro in the *preprocessed* C-code resolves to a different, unique integer.
 * Internally, it uses the __COUNTER__ gcc extension, and as such all
 * occurrences generate a dense set of integers.
 *
 * Return: This evaluates to an integer literal
 */
#define C_CC_UNIQUE __COUNTER__

/**
 * C_VAR() - generate unique variable name
 * @_x:         name of variable
 * @_uniq:      unique prefix, usually provided by @C_CC_UNIQUE
 *
 * This macro shall be used to generate unique variable names, that will not be
 * shadowed by recursive macro invocations. It is effectively a
 * C_CC_CONCATENATE of both arguments, but also provides a globally separated
 * prefix and makes the code better readable.
 *
 * This helper may be used by macro implementations that might reasonable well
 * be called in a stacked fasion, like:
 *     c_max(foo, c_max(bar, baz))
 * Such a stacked call of c_max() might cause compiler warnings of shadowed
 * variables in the definition of c_max(). By using C_VAR(), such warnings
 * can be silenced as each evaluation of c_max() uses unique variable names.
 *
 * Return: This evaluates to a constant identifier
 */
#define C_VAR(_x, _uniq) C_CC_CONCATENATE(c_internal_var_unique_, C_CC_CONCATENATE(_uniq, _x))

/**
 * c_math_div_round_up() - calculate integer quotient but round up
 * @_x:         dividend
 * @_y:         divisor
 *
 * Calculates [x / y] but rounds up the result to the next integer. All
 * arguments are evaluated exactly once, and yield a constant expression if all
 * arguments are constant.
 *
 * Note:
 * [(x + y - 1) / y] suffers from an integer overflow, even though the
 * computation should be possible in the given type. Therefore, we use
 * [x / y + !!(x % y)]. Note that on most CPUs a division returns both the
 * quotient and the remainder, so both should be equally fast. Furthermore, if
 * the divisor is a power of two, the compiler will optimize it, anyway.
 *
 * Return: The quotient is returned.
 */
#define c_math_div_round_up(_x, _y) c_internal_math_div_round_up(C_CC_UNIQUE, (_x), C_CC_UNIQUE, (_y))
#define c_internal_math_div_round_up(_xq, _x, _yq, _y)                 \
        C_CC_IF(                                                       \
                (C_CC_IS_CONST(_x) && C_CC_IS_CONST(_y)),             \
                ((_x) / (_y) + !!((_x) % (_y))),                        \
                __extension__ ({                                        \
                        const __auto_type C_VAR(X, _xq) = (_x);        \
                        const __auto_type C_VAR(Y, _yq) = (_y);        \
                        (C_VAR(X, _xq) / C_VAR(Y, _yq) +              \
                         !!(C_VAR(X, _xq) % C_VAR(Y, _yq)));          \
                }))

#ifdef __cplusplus
}
#endif
