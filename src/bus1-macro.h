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
 * XXX: Add description
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

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
 * Shortcuts for gcc attributes. See GCC manual for details. We do not prefix
 * them as they're 1-on-1 mappings to the GCC equivalents.
 */
#define _align_(_x) __attribute__((__aligned__(_x)))
#define _alignas_(_x) __attribute__((__aligned__(__alignof(_x))))
#define _alloc_(...) __attribute__((__alloc_size__(__VA_ARGS__)))
#define _cleanup_(_x) __attribute__((__cleanup__(_x)))
#define _const_ __attribute__((__const__))
#define _deprecated_ __attribute__((__deprecated__))
#define _hidden_ __attribute__((__visibility__("hidden")))
#define _likely_(_x) (__builtin_expect(!!(_x), 1))
#define _malloc_ __attribute__((__malloc__))
#define _packed_ __attribute__((__packed__))
#define _printf_(_a, _b) __attribute__((__format__(printf, _a, _b)))
#define _public_ __attribute__((__visibility__("default")))
#define _pure_ __attribute__((__pure__))
#define _sentinel_ __attribute__((__sentinel__))
#define _unlikely_(_x) (__builtin_expect(!!(_x), 0))
#define _unused_ __attribute__((__unused__))
#define _weak_ __attribute__((__weak__))
#define _weakref_(_x) __attribute__((__weakref__(#_x)))

/**
 * B1_TYPE_MATCH() - match two variables/types for unqualified equality
 * @_a:         first variable/type
 * @_b:         second variable/type
 *
 * Compare two types, or types of two variables, for equality. Note that type
 * qualifiers are not respected by this comparison. Hence, only the actual
 * underlying types are compared.
 *
 * Return: 1 if both unqualified types are equal, 0 if not.
 */
#define B1_TYPE_MATCH(_a, _b) __builtin_types_compatible_p(__typeof__(_a), __typeof__(_b))

/**
 * B1_TYPE_IS_ARRAY() - evaluate whether given variable is of an array type
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
#define B1_TYPE_IS_ARRAY(_a) (!B1_TYPE_MATCH(__typeof__(_a), &(*(__typeof__(_a)*)0)[0]))

/**
 * B1_CC_IF() - conditional expression at compile time
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
#define B1_CC_IF(_cond, _if, _else) __builtin_choose_expr(!!(_cond), _if, _else)

/**
 * B1_CC_IS_CONST() - check whether a value is known at compile time
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
 * in combination with B1_CC_IF() to avoid multiple evaluations of macro
 * parameters.
 *
 * Return: 1 if constant, 0 if not.
 */
#define B1_CC_IS_CONST(_expr) __builtin_constant_p(_expr)

/**
 * B1_CC_ASSERT_MSG() - compile time assertion
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
#define B1_CC_ASSERT_MSG(_cond, _msg) ((void)B1_CC_ASSERT1_MSG((_cond), _msg))

/**
 * B1_CC_ASSERT1_MSG() - compile time assertion
 * @_cond:      condition
 * @_msg:       message to make the compiler print
 *
 * This is the same as B1_CC_ASSERT_MSG(), but evaluates to constant 1.
 *
 * Return: This macro evaluates to constant 1.
 */
#define B1_CC_ASSERT1_MSG(_cond, _msg) (sizeof(int[!(_cond) * -1]) * 0 + 1)

/**
 * B1_CC_ASSERT() - compile time assertion
 * @_cond:      condition
 *
 * Same as B1_CC_ASSERT_MSG() but prints the condition as error message.
 *
 * Return: This macro evaluates to a void expression.
 */
#define B1_CC_ASSERT(_cond) ((void)B1_CC_ASSERT1(_cond))

/**
 * B1_CC_ASSERT1() - compile time assertion
 * @_cond:      condition
 *
 * This is the same as B1_CC_ASSERT(), but evaluates to constant 1.
 *
 * Return: This macro evaluates to constant 1.
 */
#define B1_CC_ASSERT1(_cond) B1_CC_ASSERT1_MSG((_cond), #_cond)

/**
 * B1_CC_ASSERT_TO() - compile time assertion with explicit return value
 * @_cond:      condition to assert
 * @_expr:      expression to yield
 *
 * This is equivalent to B1_CC_ASSERT1(_cond), but yields a return value of
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
 * expression. Hence, we have to use B1_CC_IF() to yield the same result.
 *
 * Return: This macro evaluates to @_expr.
 */
#define B1_CC_ASSERT_TO(_cond, _expr) B1_CC_IF(B1_CC_ASSERT1(_cond), (_expr), ((void)0))

/**
 * B1_CC_STATIC_ASSERT() - static compile time assertion
 * @_cond:      condition
 *
 * This is equivalent to B1_CC_ASSERT(), but evaluates to a statement instead
 * of an expression. This allows usage in file-context, where STD-C does not
 * allow plain expressions. If you need a custom compiler message to print, use
 * static_assert() directly (it's STD-C11).
 *
 * Return: This macro evaluates to a statement.
 */
#define B1_CC_STATIC_ASSERT(_cond) static_assert((_cond), #_cond)

/**
 * B1_CC_STRINGIFY() - stringify a token, but evaluate it first
 * @_x:         token to evaluate and stringify
 *
 * Return: Evaluates to a constant string literal
 */
#define B1_CC_STRINGIFY(_x) B1_INTERNAL_CC_STRINGIFY(_x)
#define B1_INTERNAL_CC_STRINGIFY(_x) #_x

/**
 * B1_CC_CONCATENATE() - concatenate two tokens, but evaluate them first
 * @_x:         first token
 * @_y:         second token
 *
 * Return: Evaluates to a constant identifier
 */
#define B1_CC_CONCATENATE(_x, _y) B1_INTERNAL_CC_CONCATENATE(_x, _y)
#define B1_INTERNAL_CC_CONCATENATE(_x, _y) _x ## _y

/**
 * B1_CC_ARRAY_SIZE() - calculate number of array elements at compile time
 * @_x:         array to calculate size of
 *
 * Return: Evaluates to a constant integer expression
 */
#define B1_CC_ARRAY_SIZE(_x) B1_CC_ASSERT_TO(B1_TYPE_IS_ARRAY(_x), sizeof(_x) / sizeof((_x)[0]))

/**
 * B1_CC_DECIMAL_MAX() - calculate maximum length of the decimal
 *                       representation of an integer
 * @_type: integer variable/type
 *
 * This calculates the bytes required for the decimal representation of an
 * integer of the given type. It accounts for a possible +/- prefix, but it
 * does *NOT* include the trailing terminating zero byte.
 *
 * Return: Evaluates to a constant integer expression
 */
#define B1_CC_DECIMAL_MAX(_type)                                                \
        _Generic((_type){ 0 },                                                  \
                char:                   B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed char:            B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned char:          B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed short:           B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned short:         B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed int:             B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned int:           B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed long:            B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned long:          B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                signed long long:       B1_INTERNAL_CC_DECIMAL_MAX(_type),      \
                unsigned long long:     B1_INTERNAL_CC_DECIMAL_MAX(_type))
#define B1_INTERNAL_CC_DECIMAL_MAX(_type)               \
        (1 + (sizeof(_type) <= 1 ?  3 :                 \
              sizeof(_type) <= 2 ?  5 :                 \
              sizeof(_type) <= 4 ? 10 :                 \
              B1_CC_ASSERT_TO(sizeof(_type) <= 8, 20)))

/**
 * B1_CC_UNIQUE - generate unique compile-time integer
 *
 * This evaluates to a unique compile-time integer. Each occurrence of this
 * macro in the *preprocessed* C-code resolves to a different, unique integer.
 * Internally, it uses the __COUNTER__ gcc extension, and as such all
 * occurrences generate a dense set of integers.
 *
 * Return: This evaluates to an integer literal
 */
#define B1_CC_UNIQUE __COUNTER__

/**
 * B1_VAR() - generate unique variable name
 * @_x:         name of variable
 * @_uniq:      unique prefix, usually provided by @B1_CC_UNIQUE
 *
 * This macro shall be used to generate unique variable names, that will not be
 * shadowed by recursive macro invocations. It is effectively a
 * B1_CC_CONCATENATE of both arguments, but also provides a globally separated
 * prefix and makes the code better readable.
 *
 * This helper may be used by macro implementations that might reasonable well
 * be called in a stacked fasion, like:
 *     b1_max(foo, b1_max(bar, baz))
 * Such a stacked call of b1_max() might cause compiler warnings of shadowed
 * variables in the definition of b1_max(). By using B1_VAR(), such warnings
 * can be silenced as each evaluation of b1_max() uses unique variable names.
 *
 * Return: This evaluates to a constant identifier
 */
#define B1_VAR(_x, _uniq) B1_CC_CONCATENATE(b1_var_unique_prefix_, B1_CC_CONCATENATE(_uniq, _x))

/**
 * container_of() - cast a member of a structure out to the containing structure
 * @_ptr:       pointer to the member or NULL
 * @_type:      type of the container struct this is embedded in
 * @_member:    name of the member within the struct
 */
#define b1_container_of(_ptr, _type, _member) b1_internal_container_of(B1_CC_UNIQUE, (_ptr), _type, _member)
#define b1_internal_container_of(_uniq, _ptr, _type, _member)                                   \
        __extension__ ({                                                                        \
                const __typeof__( ((_type*)0)->_member ) *B1_VAR(A, _uniq) = (_ptr);            \
                (_ptr) ? (_type*)( (char*)B1_VAR(A, _uniq) - offsetof(_type, _member) ) : NULL; \
        })

/**
 * b1_max() - compute maximum of two values
 * @_a:         value A
 * @_b:         value B
 *
 * Calculate the maximum of both passed values. Both arguments are evaluated
 * exactly once, under all circumstances. Furthermore, if both values are
 * constant expressions, the result will be constant as well.
 *
 * Return: Maximum of both values is returned.
 */
#define b1_max(_a, _b) b1_internal_max(B1_CC_UNIQUE, (_a), B1_CC_UNIQUE, (_b))
#define b1_internal_max(_aq, _a, _bq, _b)                                                       \
        B1_CC_IF(                                                                               \
                (B1_CC_IS_CONST(_a) && B1_CC_IS_CONST(_b)),                                     \
                ((_a) > (_b) ? (_a) : (_b)),                                                    \
                __extension__ ({                                                                \
                        const __auto_type B1_VAR(A, _aq) = (_a);                                \
                        const __auto_type B1_VAR(B, _bq) = (_b);                                \
                        B1_VAR(A, _aq) > B1_VAR(B, _bq) ? B1_VAR(A, _aq) : B1_VAR(B, _bq);      \
                }))

/**
 * b1_min() - compute minimum of two values
 * @_a:         value A
 * @_b:         value B
 *
 * Calculate the minimum of both passed values. Both arguments are evaluated
 * exactly once, under all circumstances. Furthermore, if both values are
 * constant expressions, the result will be constant as well.
 *
 * Return: Minimum of both values is returned.
 */
#define b1_min(_a, _b) b1_internal_min(B1_CC_UNIQUE, (_a), B1_CC_UNIQUE, (_b))
#define b1_internal_min(_aq, _a, _bq, _b)                                                       \
        B1_CC_IF(                                                                               \
                (B1_CC_IS_CONST(_a) && B1_CC_IS_CONST(_b)),                                     \
                ((_a) < (_b) ? (_a) : (_b)),                                                    \
                __extension__ ({                                                                \
                        const __auto_type B1_VAR(A, _aq) = (_a);                                \
                        const __auto_type B1_VAR(B, _bq) = (_b);                                \
                        B1_VAR(A, _aq) < B1_VAR(B, _bq) ? B1_VAR(A, _aq) : B1_VAR(B, _bq);      \
                }))

/**
 * b1_less_by() - calculate clamped difference of two values
 * @_a:         minuend
 * @_b:         subtrahend
 *
 * Calculate [_a - _b], but clamp the result to 0. Both arguments are evaluated
 * exactly once, under all circumstances. Furthermore, if both values are
 * constant expressions, the result will be constant as well.
 *
 * Return: This computes [_a - _b], if [_a > _b]. Otherwise, 0 is returned.
 */
#define b1_less_by(_a, _b) b1_internal_less_by(B1_CC_UNIQUE, (_a), B1_CC_UNIQUE, (_b))
#define b1_internal_less_by(_aq, _a, _bq, _b)                                                   \
        B1_CC_IF(                                                                               \
                (B1_CC_IS_CONST(_a) && B1_CC_IS_CONST(_b)),                                     \
                ((_a) > (_b) ? ((_a) - (_b)) : 0),                                              \
                __extension__ ({                                                                \
                        const __auto_type B1_VAR(A, _aq) = (_a);                                \
                        const __auto_type B1_VAR(B, _bq) = (_b);                                \
                        B1_VAR(A, _aq) > B1_VAR(B, _bq) ? B1_VAR(A, _aq) - B1_VAR(B, _bq) : 0;  \
                }))

/**
 * b1_clamp() - clamp value to lower and upper boundary
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
#define b1_clamp(_x, _low, _high) b1_internal_clamp(B1_CC_UNIQUE, (_x), B1_CC_UNIQUE, (_low), B1_CC_UNIQUE, (_high))
#define b1_internal_clamp(_xq, _x, _lowq, _low, _highq, _high)          \
        B1_CC_IF(                                                                               \
                (B1_CC_IS_CONST(_x) && B1_CC_IS_CONST(_low) && B1_CC_IS_CONST(_high)),          \
                ((_x) > (_high) ?                                                               \
                        (_high) :                                                               \
                        (_x) < (_low) ?                                                         \
                        (_low) :                                                                \
                        (_x)),                                                                  \
                __extension__ ({                                                                \
                        const __auto_type B1_VAR(X, _xq) = (_x);                                \
                        const __auto_type B1_VAR(LOW, _lowq) = (_low);                          \
                        const __auto_type B1_VAR(HIGH, _highq) = (_high);                       \
                                B1_VAR(X, _xq) > B1_VAR(HIGH, _highq) ?                         \
                                        B1_VAR(HIGH, _highq) :                                  \
                                        B1_VAR(X, _xq) < B1_VAR(LOW, _lowq) ?                   \
                                                B1_VAR(LOW, _lowq) :                            \
                                                B1_VAR(X, _xq);                                 \
                }))

/**
 * b1_negative_errno() - return negative errno
 *
 * This helper should be used to shut up gcc if you know 'errno' is valid (ie.,
 * errno is > 0). Instead of "return -errno;", use
 * "return b1_negative_errno();" It will suppress bogus gcc warnings in case
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
static inline int b1_negative_errno(void) {
        return _likely_(errno > 0) ? -errno : -EINVAL;
}

/**
 * b1_math_clz() - count leading zeroes
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
#define b1_math_clz(_val)                                       \
        _Generic((_val),                                        \
                unsigned int: __builtin_clz(_val),              \
                unsigned long: __builtin_clzl(_val),            \
                unsigned long long: __builtin_clzll(_val))

/**
 * b1_math_div_round_up() - calculate integer quotient but round up
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
#define b1_math_div_round_up(_x, _y) b1_internal_math_div_round_up(B1_CC_UNIQUE, (_x), B1_CC_UNIQUE, (_y))
#define b1_internal_math_div_round_up(_xq, _x, _yq, _y)                 \
        B1_CC_IF(                                                       \
                (B1_CC_IS_CONST(_x) && B1_CC_IS_CONST(_y)),             \
                ((_x) / (_y) + !!((_x) % (_y))),                        \
                __extension__ ({                                        \
                        const __auto_type B1_VAR(X, _xq) = (_x);        \
                        const __auto_type B1_VAR(Y, _yq) = (_y);        \
                        (B1_VAR(X, _xq) / B1_VAR(Y, _yq) +              \
                         !!(B1_VAR(X, _xq) % B1_VAR(Y, _yq)));          \
                }))

/**
 * b1_math_align_to() - align value to
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
#define b1_math_align_to(_val, _to) b1_internal_math_align_to((_val), B1_CC_UNIQUE, (_to))
#define b1_internal_math_align_to(_val, _toq, _to)                                                      \
        B1_CC_IF(                                                                                       \
                B1_CC_IS_CONST(_to),                                                                    \
                B1_CC_ASSERT_TO(__builtin_popcountll(B1_CC_IF(B1_CC_IS_CONST(_to), (_to), 1)) == 1,     \
                                (((_val) + (_to) - 1) & ~((_to) - 1))),                                 \
                __extension__ ({                                                                        \
                        const __auto_type B1_VAR(to, _toq) = (_to);                                     \
                        ((_val) + B1_VAR(to, _toq) - 1) & ~(B1_VAR(to, _toq) - 1);                      \
                }))

/**
 * b1_math_align() - align to native size
 * @_val:       value to align
 *
 * This is the same as b1_math_align_to((_val), __SIZEOF_POINTER__).
 *
 * Return: @_val aligned to the native size
 */
#define b1_math_align(_val) b1_math_align_to((_val), __SIZEOF_POINTER__)

/**
 * b1_math_align8() - align value to multiple of 8
 * @_val:       value to align
 *
 * This is the same as b1_math_align_to((_val), 8).
 *
 * Return: @_val aligned to a multiple of 8.
 */
#define b1_math_align8(_val) b1_math_align_to((_val), 8)

/**
 * b1_math_align_power2() - align value to next power of 2
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
#define b1_math_align_power2(_val) b1_internal_math_align_power2(B1_CC_UNIQUE, (_val))
#define b1_internal_math_align_power2(_vq, _v)                                                          \
        __extension__ ({                                                                                \
                __auto_type B1_VAR(v, _vq) = (_v);                                                      \
                /* cannot use ?: as gcc cannot do const-folding then (apparently..) */                  \
                if (B1_VAR(v, _vq) == 1) /* clz(0) is undefined */                                      \
                        B1_VAR(v, _vq) = 1;                                                             \
                else if (b1_math_clz(B1_VAR(v, _vq) - 1) < 1) /* shift overflow is undefined */         \
                        B1_VAR(v, _vq) = 0;                                                             \
                else                                                                                    \
                        B1_VAR(v, _vq) = ((__typeof__(B1_VAR(v, _vq)))1) <<                             \
                                        (sizeof(B1_VAR(v, _vq)) * 8 - b1_math_clz(B1_VAR(v, _vq) - 1)); \
                B1_VAR(v, _vq);                                                                         \
        })

/*
 * XXX: move to separate header
 * XXX: document
 *
 * Bitmap helpers
 */

#include <string.h>

/**
 * b1_bitmap_test() - test bit in bitmap
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
static inline bool b1_bitmap_test(const void *bitmap, unsigned int bit) {
        return *((const uint8_t *)bitmap + bit / 8) & (1 << (bit % 8));
}

static inline void b1_bitmap_set(void *bitmap, unsigned int bit) {
        *((uint8_t *)bitmap + bit / 8) |= (1 << (bit % 8));
}

static inline void b1_bitmap_clear(void *bitmap, unsigned int bit) {
        *((uint8_t *)bitmap + bit / 8) &= ~(1 << (bit % 8));
}

static inline void b1_bitmap_set_all(void *bitmap, unsigned int n_bits) {
        memset(bitmap, 0xff, n_bits / 8);
}

static inline void b1_bitmap_clear_all(void *bitmap, unsigned int n_bits) {
        memset(bitmap, 0, n_bits / 8);
}

/*
 * XXX: move to separate header
 * XXX: write test suite
 *
 * String helpers
 */

#include <string.h>

_pure_ static inline bool b1_str_equal(const char *a, const char *b) {
        return (!a || !b) ? (a == b) : !strcmp(a, b);
}

_pure_ static inline char *b1_str_prefix(const char *str, const char *prefix) {
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

/* stores up to 584,942.417355 years */
typedef uint64_t b1_usec;

#define b1_usec_from_nsec(_nsec) ((_nsec) / UINT64_C(1000))
#define b1_usec_from_msec(_msec) ((_msec) * UINT64_C(1000))
#define b1_usec_from_sec(_sec) b1_usec_from_msec((_sec) * UINT64_C(1000))
#define b1_usec_from_timespec(_ts) (b1_usec_from_sec((_ts)->tv_sec) + b1_usec_from_nsec((_ts)->tv_nsec))
#define b1_usec_from_timeval(_tv) (b1_usec_from_sec((_tv)->tv_sec) + (_tv)->tv_usec)

static inline b1_usec b1_usec_from_clock(clockid_t clock) {
        struct timespec ts;
        int r;

        r = clock_gettime(clock, &ts);
        assert(r >= 0);
        return b1_usec_from_timespec(&ts);
}

#ifdef __cplusplus
}
#endif
