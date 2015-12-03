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
 * Macros
 * This header contains macros useful across our codebase. This includes
 * pre-processor macros and a *very* limited set of inlined functions that are
 * used throughout the code-base.
 *
 * As this header is included all over the place, make sure to only add stuff
 * that really belongs all-over-the-place.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "c-public.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * C_TYPE_MATCH() - match two variables/types for unqualified equality
 * @_a:         first variable/type
 * @_b:         second variable/type
 *
 * Compare two types, or types of two variables, for equality. Note that type
 * qualifiers are not respected by this comparison. Hence, only the actual
 * underlying types are compared.
 *
 * Return: 1 if both unqualified types are equal, 0 if not.
 */
#define C_TYPE_MATCH(_a, _b) __builtin_types_compatible_p(__typeof__(_a), __typeof__(_b))

/**
 * C_TYPE_IS_ARRAY() - evaluate whether given variable is of an array type
 * @_a:         variable/type to evaluate
 *
 * This function checks whether a given variable is an array type. Note that
 * the passed argument must either be an array or pointer, otherwise, this will
 * generate a syntax error.
 *
 * Note that "&a[0]" degrades an array to a pointer, and as such compares
 * unequal to "a" if it is an array. This is unique to array types.
 *
 * Return: 1 if it is an array type, 0 if not.
 */
#define C_TYPE_IS_ARRAY(_a) (!C_TYPE_MATCH(__typeof__(_a), &(*(__typeof__(_a)*)0)[0]))

/**
 * C_CC_ASSERT_MSG() - compile time assertion
 * @_cond:      condition
 * @_msg:       message to make the compiler print
 *
 * This is a compile-time assertion that can be used in any (constant)
 * expression. If @_cond evalutes to true, this is equivalent to a void
 * expression. If @_cond is false, this will cause a compiler error and print
 * @_msg into the compile log.
 *
 * XXX: Find some gcc hack to print @_msg while keeping the macro a constant
 * expression.
 *
 * Return: This macro evaluates to a void expression.
 */
#define C_CC_ASSERT_MSG(_cond, _msg) ((void)C_CC_ASSERT1_MSG((_cond), _msg))

/**
 * C_CC_ASSERT1_MSG() - compile time assertion
 * @_cond:      condition
 * @_msg:       message to make the compiler print
 *
 * This is the same as C_CC_ASSERT_MSG(), but evaluates to constant 1.
 *
 * Return: This macro evaluates to constant 1.
 */
#define C_CC_ASSERT1_MSG(_cond, _msg) (sizeof(int[!(_cond) * -1]) * 0 + 1)

/**
 * C_CC_ASSERT() - compile time assertion
 * @_cond:      condition
 *
 * Same as C_CC_ASSERT_MSG() but prints the condition as error message.
 *
 * Return: This macro evaluates to a void expression.
 */
#define C_CC_ASSERT(_cond) ((void)C_CC_ASSERT1(_cond))

/**
 * C_CC_ASSERT1() - compile time assertion
 * @_cond:      condition
 *
 * This is the same as C_CC_ASSERT(), but evaluates to constant 1.
 *
 * Return: This macro evaluates to constant 1.
 */
#define C_CC_ASSERT1(_cond) C_CC_ASSERT1_MSG((_cond), #_cond)

/**
 * C_CC_ASSERT_TO() - compile time assertion with explicit return value
 * @_cond:      condition to assert
 * @_expr:      expression to yield
 *
 * This is equivalent to C_CC_ASSERT1(_cond), but yields a return value of
 * @_expr, rather than constant 1.
 *
 * In case the compile-time assertion is false, this causes a compile-time
 * error and *also* evaluates as a void expression (and as such usually causes
 * a followup compile time error).
 *
 * Note that usually you'd do something like:
 *     (ASSERT(cond), expr)
 * thus using the comma-operator to yield a specific value. However,
 * suprisingly STD-C does *not* define the comma operator as constant
 * expression. Hence, we have to use C_CC_IF() to yield the same result.
 *
 * Return: This macro evaluates to @_expr.
 */
#define C_CC_ASSERT_TO(_cond, _expr) C_CC_IF(C_CC_ASSERT1(_cond), (_expr), ((void)0))

/**
 * C_CC_STATIC_ASSERT() - static compile time assertion
 * @_cond:      condition
 *
 * This is equivalent to C_CC_ASSERT(), but evaluates to a statement instead
 * of an expression. This allows usage in file-context, where STD-C does not
 * allow plain expressions. If you need a custom compiler message to print, use
 * static_assert() directly (it's STD-C11).
 *
 * Return: This macro evaluates to a statement.
 */
#define C_CC_STATIC_ASSERT(_cond) static_assert((_cond), #_cond)

/**
 * C_CC_ARRAY_SIZE() - calculate number of array elements at compile time
 * @_x:         array to calculate size of
 *
 * Return: Evaluates to a constant integer expression
 */
#define C_CC_ARRAY_SIZE(_x) C_CC_ASSERT_TO(C_TYPE_IS_ARRAY(_x), sizeof(_x) / sizeof((_x)[0]))

/**
 * C_CC_DECIMAL_MAX() - calculate maximum length of the decimal
 *                       representation of an integer
 * @_type: integer variable/type
 *
 * This calculates the bytes required for the decimal representation of an
 * integer of the given type. It accounts for a possible +/- prefix, but it
 * does *NOT* include the trailing terminating zero byte.
 *
 * Return: Evaluates to a constant integer expression
 */
#define C_CC_DECIMAL_MAX(_type)                                                \
        _Generic((_type){ 0 },                                                  \
                char:                   C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed char:            C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned char:          C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed short:           C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned short:         C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed int:             C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned int:           C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed long:            C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned long:          C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed long long:       C_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned long long:     C_INTERNAL_CC_DECIMAL_MAX(_type))
#define C_INTERNAL_CC_DECIMAL_MAX(_type)               \
        (1 + (sizeof(_type) <= 1 ?  3 :                 \
              sizeof(_type) <= 2 ?  5 :                 \
              sizeof(_type) <= 4 ? 10 :                 \
              C_CC_ASSERT_TO(sizeof(_type) <= 8, 20)))

/**
 * container_of() - cast a member of a structure out to the containing structure
 * @_ptr:       pointer to the member or NULL
 * @_type:      type of the container struct this is embedded in
 * @_member:    name of the member within the struct
 */
#define c_container_of(_ptr, _type, _member) c_internal_container_of(C_CC_UNIQUE, (_ptr), _type, _member)
#define c_internal_container_of(_uniq, _ptr, _type, _member)                                   \
        __extension__ ({                                                                        \
                const __typeof__( ((_type*)0)->_member ) *C_VAR(A, _uniq) = (_ptr);            \
                (_ptr) ? (_type*)( (char*)C_VAR(A, _uniq) - offsetof(_type, _member) ) : NULL; \
        })

/**
 * c_max() - compute maximum of two values
 * @_a:         value A
 * @_b:         value B
 *
 * Calculate the maximum of both passed values. Both arguments are evaluated
 * exactly once, under all circumstances. Furthermore, if both values are
 * constant expressions, the result will be constant as well.
 *
 * Return: Maximum of both values is returned.
 */
#define c_max(_a, _b) c_internal_max(C_CC_UNIQUE, (_a), C_CC_UNIQUE, (_b))
#define c_internal_max(_aq, _a, _bq, _b)                                                       \
        C_CC_IF(                                                                               \
                (C_CC_IS_CONST(_a) && C_CC_IS_CONST(_b)),                                     \
                ((_a) > (_b) ? (_a) : (_b)),                                                    \
                __extension__ ({                                                                \
                        const __auto_type C_VAR(A, _aq) = (_a);                                \
                        const __auto_type C_VAR(B, _bq) = (_b);                                \
                        C_VAR(A, _aq) > C_VAR(B, _bq) ? C_VAR(A, _aq) : C_VAR(B, _bq);      \
                }))

/**
 * c_min() - compute minimum of two values
 * @_a:         value A
 * @_b:         value B
 *
 * Calculate the minimum of both passed values. Both arguments are evaluated
 * exactly once, under all circumstances. Furthermore, if both values are
 * constant expressions, the result will be constant as well.
 *
 * Return: Minimum of both values is returned.
 */
#define c_min(_a, _b) c_internal_min(C_CC_UNIQUE, (_a), C_CC_UNIQUE, (_b))
#define c_internal_min(_aq, _a, _bq, _b)                                                       \
        C_CC_IF(                                                                               \
                (C_CC_IS_CONST(_a) && C_CC_IS_CONST(_b)),                                     \
                ((_a) < (_b) ? (_a) : (_b)),                                                    \
                __extension__ ({                                                                \
                        const __auto_type C_VAR(A, _aq) = (_a);                                \
                        const __auto_type C_VAR(B, _bq) = (_b);                                \
                        C_VAR(A, _aq) < C_VAR(B, _bq) ? C_VAR(A, _aq) : C_VAR(B, _bq);      \
                }))

/**
 * c_less_by() - calculate clamped difference of two values
 * @_a:         minuend
 * @_b:         subtrahend
 *
 * Calculate [_a - _b], but clamp the result to 0. Both arguments are evaluated
 * exactly once, under all circumstances. Furthermore, if both values are
 * constant expressions, the result will be constant as well.
 *
 * Return: This computes [_a - _b], if [_a > _b]. Otherwise, 0 is returned.
 */
#define c_less_by(_a, _b) c_internal_less_by(C_CC_UNIQUE, (_a), C_CC_UNIQUE, (_b))
#define c_internal_less_by(_aq, _a, _bq, _b)                                                   \
        C_CC_IF(                                                                               \
                (C_CC_IS_CONST(_a) && C_CC_IS_CONST(_b)),                                     \
                ((_a) > (_b) ? ((_a) - (_b)) : 0),                                              \
                __extension__ ({                                                                \
                        const __auto_type C_VAR(A, _aq) = (_a);                                \
                        const __auto_type C_VAR(B, _bq) = (_b);                                \
                        C_VAR(A, _aq) > C_VAR(B, _bq) ? C_VAR(A, _aq) - C_VAR(B, _bq) : 0;  \
                }))

/**
 * c_clamp() - clamp value to lower and upper boundary
 * @_x:         value to clamp
 * @_low:       lower boundary
 * @_high:      higher boundary
 *
 * This clamps @_x to the lower and higher bounds given as @_low and @_high.
 * All arguments are evaluated exactly once, and yield a constant expression if
 * all arguments are constant as well.
 *
 * Return: Clamped integer value.
 */
#define c_clamp(_x, _low, _high) c_internal_clamp(C_CC_UNIQUE, (_x), C_CC_UNIQUE, (_low), C_CC_UNIQUE, (_high))
#define c_internal_clamp(_xq, _x, _lowq, _low, _highq, _high)          \
        C_CC_IF(                                                                               \
                (C_CC_IS_CONST(_x) && C_CC_IS_CONST(_low) && C_CC_IS_CONST(_high)),          \
                ((_x) > (_high) ?                                                               \
                        (_high) :                                                               \
                        (_x) < (_low) ?                                                         \
                        (_low) :                                                                \
                        (_x)),                                                                  \
                __extension__ ({                                                                \
                        const __auto_type C_VAR(X, _xq) = (_x);                                \
                        const __auto_type C_VAR(LOW, _lowq) = (_low);                          \
                        const __auto_type C_VAR(HIGH, _highq) = (_high);                       \
                                C_VAR(X, _xq) > C_VAR(HIGH, _highq) ?                         \
                                        C_VAR(HIGH, _highq) :                                  \
                                        C_VAR(X, _xq) < C_VAR(LOW, _lowq) ?                   \
                                                C_VAR(LOW, _lowq) :                            \
                                                C_VAR(X, _xq);                                 \
                }))

/**
 * c_negative_errno() - return negative errno
 *
 * This helper should be used to shut up gcc if you know 'errno' is valid (ie.,
 * errno is > 0). Instead of "return -errno;", use
 * "return c_negative_errno();" It will suppress bogus gcc warnings in case
 * it assumes 'errno' might be 0 (or <0) and thus the caller's error-handling
 * might not be triggered.
 *
 * This helper should be avoided whenever possible. However, occasionally we
 * really want to shut up gcc (especially with static/inline functions). In
 * those cases, gcc usually cannot deduce that some error paths are guaranteed
 * to be taken. Hence, making the return value explicit allows gcc to better
 * optimize the code.
 *
 * Note that you really should never use this helper to work around broken libc
 * calls or syscalls, not setting 'errno' correctly.
 *
 * Return: Negative error code is returned.
 */
static inline int c_negative_errno(void) {
        return _c_likely_(errno > 0) ? -errno : -EINVAL;
}

/**
 * c_math_clz() - count leading zeroes
 * @_val:       value to count leading zeroes of
 *
 * This counts the leading zeroes of the binary representation of @_val. Note
 * that @_val must be of an integer type greater than, or equal to, 'unsigned
 * int'. Also note that a value of 0 produces an undefined result (see your CPU
 * instruction manual for details why).
 *
 * This macro evaluates the argument exactly once, and if the input is
 * constant, it also evaluates to a constant expression.
 *
 * Note that this macro calculates the number of leading zeroes within the
 * scope of the integer type of @_val. That is, if the input is a 32bit type
 * with value 1, it yields 31. But if it is a 64bit type with the same value 1,
 * it yields 63.
 *
 * Return: Evaluates to an 'int', the number of leading zeroes.
 */
#define c_math_clz(_val)                                       \
        _Generic((_val),                                        \
                unsigned int: __builtin_clz(_val),              \
                unsigned long: __builtin_clzl(_val),            \
                unsigned long long: __builtin_clzll(_val))

/**
 * c_math_align_to() - align value to
 * @_val:       value to align
 * @_to:        align to multiple of this
 *
 * This aligns @_val to a multiple of @_to. If @_val is already a multiple of
 * @_to, @_val is returned unchanged. This function operates within the
 * boundaries of the type of @_val and @_to. Make sure to cast them if needed.
 *
 * The arguments of this macro are evaluated exactly once. If both arguments
 * are a constant expression, this also yields a constant return value.
 *
 * Note that @_to must be a power of 2. In case @_to is a constant expression,
 * this macro places a compile-time assertion on the popcount of @_to, to
 * verify it is a power of 2.
 *
 * Return: @_val aligned to a multiple of @_to
 */
#define c_math_align_to(_val, _to) c_internal_math_align_to((_val), C_CC_UNIQUE, (_to))
#define c_internal_math_align_to(_val, _toq, _to)                                                      \
        C_CC_IF(                                                                                       \
                C_CC_IS_CONST(_to),                                                                    \
                C_CC_ASSERT_TO(__builtin_popcountll(C_CC_IF(C_CC_IS_CONST(_to), (_to), 1)) == 1,     \
                                (((_val) + (_to) - 1) & ~((_to) - 1))),                                 \
                __extension__ ({                                                                        \
                        const __auto_type C_VAR(to, _toq) = (_to);                                     \
                        ((_val) + C_VAR(to, _toq) - 1) & ~(C_VAR(to, _toq) - 1);                      \
                }))

/**
 * c_math_align() - align to native size
 * @_val:       value to align
 *
 * This is the same as c_math_align_to((_val), __SIZEOF_POINTER__).
 *
 * Return: @_val aligned to the native size
 */
#define c_math_align(_val) c_math_align_to((_val), __SIZEOF_POINTER__)

/**
 * c_math_align8() - align value to multiple of 8
 * @_val:       value to align
 *
 * This is the same as c_math_align_to((_val), 8).
 *
 * Return: @_val aligned to a multiple of 8.
 */
#define c_math_align8(_val) c_math_align_to((_val), 8)

/**
 * c_math_align_power2() - align value to next power of 2
 * @_val:       value to align
 *
 * This aligns @_val to the next higher power of 2. If it already is a power of
 * 2, the value is returned unchanged. 0 is treated as power of 2 (so 0 yields
 * 0). Furthermore, on overflow, this yields 0 as well.
 *
 * Note that this always operates within the bounds of the type of @_val.
 *
 * Return: @_val aligned to the next higher power of 2
 */
#define c_math_align_power2(_val) c_internal_math_align_power2(C_CC_UNIQUE, (_val))
#define c_internal_math_align_power2(_vq, _v)                                                          \
        __extension__ ({                                                                                \
                __auto_type C_VAR(v, _vq) = (_v);                                                      \
                /* cannot use ?: as gcc cannot do const-folding then (apparently..) */                  \
                if (C_VAR(v, _vq) == 1) /* clz(0) is undefined */                                      \
                        C_VAR(v, _vq) = 1;                                                             \
                else if (c_math_clz(C_VAR(v, _vq) - 1) < 1) /* shift overflow is undefined */         \
                        C_VAR(v, _vq) = 0;                                                             \
                else                                                                                    \
                        C_VAR(v, _vq) = ((__typeof__(C_VAR(v, _vq)))1) <<                             \
                                        (sizeof(C_VAR(v, _vq)) * 8 - c_math_clz(C_VAR(v, _vq) - 1)); \
                C_VAR(v, _vq);                                                                         \
        })

#ifdef __cplusplus
}
#endif
