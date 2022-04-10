/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_CORE_H__
#define __PDDL_CORE_H__

#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Returns offset of member in given type (struct).
 */
#define pddl_offsetof(TYPE, MEMBER) offsetof(TYPE, MEMBER)
/*#define pddl_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)*/

/**
 * Returns container of given member
 */
#define pddl_container_of(ptr, type, member) \
    ((type *)( (char *)ptr - pddl_offsetof(type, member)))

/**
 * Marks inline function.
 */
#ifdef __GNUC__
#  ifdef PDDL_DEBUG
#    define _pddl_inline static __attribute__((unused))
#  else /* PDDL_DEBUG */
#    ifdef __NO_INLINE__
#      define _pddl_inline static __attribute__((unused))
#    else /* __NO_INLINE */
#      define _pddl_inline static inline __attribute__((always_inline,unused))
#    endif /* __NO_INLINE */
#  endif /* PDDL_DEBUG */
#else /* __GNUC__ */
# define _pddl_inline static inline
#endif /* __GNUC__ */

/**
 * __prefetch(x)  - prefetches the cacheline at "x" for read
 * __prefetchw(x) - prefetches the cacheline at "x" for write
 */
#ifdef __GNUC__
# define _pddl_prefetch(x) __builtin_prefetch(x)
# define _pddl_prefetchw(x) __builtin_prefetch(x,1)
#else /* __GNUC__ */
# define _pddl_prefetch(x)
# define _pddl_prefetchw(x)
#endif /* __GNUC__ */

/**
 * Using this macros you can specify is it's likely or unlikely that branch
 * will be used.
 * Comes from linux header file ./include/compiler.h
 */
#ifdef __GNUC__
# define pddl_likely(x) __builtin_expect(!!(x), 1)
# define pddl_unlikely(x) __builtin_expect(!!(x), 0)
#else /* __GNUC__ */
# define pddl_likely(x) !!(x)
# define pddl_unlikely(x) !!(x)
#endif /* __GNUC__ */

#ifdef __GNUC__
# define pddl_aligned(x) __attribute__ ((aligned(x)))
# define pddl_packed __attribute__ ((packed))
#else /* __GNUC__ */
# define pddl_aligned(x)
# define pddl_packed
#endif /* __GNUC__ */


#ifdef __GNUC__
# define PDDL_UNUSED(f) f __attribute__((unused))
#else /* __GNUC__ */
# define PDDL_UNUSED(f)
#endif /* __GNUC__ */

#ifdef __ICC
/* disable unused parameter warning */
# pragma warning(disable:869)
/* disable annoying "operands are evaluated in unspecified order" warning */
# pragma warning(disable:981)
#endif /* __ICC */


typedef double pddl_real_t;

# define PDDL_EPS 1E-10
/*# define PDDL_EPS DBL_EPSILON*/
# define PDDL_REAL_MAX DBL_MAX
# define PDDL_REAL_MIN DBL_MIN

#define PDDL_SQRT(x)     (sqrt(x))

# define PDDL_FABS(x)     (fabs(x))
# define PDDL_FMAX(x, y)  (fmax((x), (y)))
# define PDDL_FMIN(x, y)  (fmin((x), (y)))
# define PDDL_CBRT(x)     (cbrt(x)
# define PDDL_COS(x)      (cos(x))
# define PDDL_SIN(x)      (sin(x))
# define PDDL_ACOS(x)     (acos(x))
# define PDDL_ASIN(x)     (asin(x))
# define PDDL_ATAN2(y, x) (atan2(y, x))
# define PDDL_ATAN(x)     (atan(x))
# define PDDL_POW(x, y)   (pow((x), (y)))
# define PDDL_EXP(x)      (exp(x))


#define PDDL_MIN(x, y) ((x) < (y) ? (x) : (y)) /*!< minimum */
#define PDDL_MAX(x, y) ((x) > (y) ? (x) : (y)) /*!< maximum */
#define PDDL_SQ(x)     ((x) * (x))             /*!< square */
#define PDDL_POWL(x, y) (powl((x), (y)))       /*!< power function */

/**
 * Swaps {a} and {b} using given temporary variable {tmp}.
 */
#define PDDL_SWAP(a, b, tmp) \
    do { \
        (tmp) = (a); \
        (a) = (b); \
        (b) = (tmp); \
    } while (0)



/**
 * Returns true if val is zero.
 */
_pddl_inline int pddlIsZero(pddl_real_t val)
{
    return PDDL_FABS(val) < PDDL_EPS;
}

/**
 * Returns sign of value.
 */
_pddl_inline int pddlSign(pddl_real_t val)
{
    if (pddlIsZero(val)){
        return 0;
    }else if (val < 0.){
        return -1;
    }
    return 1;
}

/**
 * Returns true if a and b equal.
 */
_pddl_inline int pddlEq(pddl_real_t _a, pddl_real_t _b)
{
    pddl_real_t ab;

    ab = PDDL_FABS(_a - _b);
    if (ab < PDDL_EPS)
        return 1;

    pddl_real_t a, b;
    a = PDDL_FABS(_a);
    b = PDDL_FABS(_b);
    if (b > a){
        return ab < PDDL_EPS * b;
    }else{
        return ab < PDDL_EPS * a;
    }
}

/**
 * Returns true if a and b not equal.
 */
_pddl_inline int pddlNEq(pddl_real_t a, pddl_real_t b)
{
    return !pddlEq(a, b);
}


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_CORE_H__ */
