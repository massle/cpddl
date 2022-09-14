/***
 * cpddl
 * -------
 * Copyright (c)2017 Daniel Fiser <danfis@danfis.cz>,
 * AI Center, Department of Computer Science,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_COMMON_H__
#define __PDDL_COMMON_H__

#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pddl/config.h>

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


#define PDDL_MIN(x, y) ((x) < (y) ? (x) : (y)) /*!< minimum */
#define PDDL_MAX(x, y) ((x) > (y) ? (x) : (y)) /*!< maximum */

/**
 * Swaps {a} and {b} using given temporary variable {tmp}.
 */
#define PDDL_SWAP(a, b, tmp) \
    do { \
        (tmp) = (a); \
        (a) = (b); \
        (b) = (tmp); \
    } while (0)


typedef struct pddl pddl_t;
typedef struct pddl_strips pddl_strips_t;

/** Type for holding number of objects */
typedef uint16_t pddl_obj_size_t;
/** Type for holding number of action parameters */
typedef uint16_t pddl_action_param_size_t;

typedef int pddl_obj_id_t;

/** Constant for undefined object ID.
 *  It should be always defined as something negative so we can test object
 *  ID with >= 0 and < 0. */
#define PDDL_OBJ_ID_UNDEF ((pddl_obj_id_t)-1)

/** Dead-end (infinity) cost */
#define PDDL_COST_DEAD_END (INT_MAX / 2)
/** Maximum cost that can be assigned */
#define PDDL_COST_MAX ((INT_MAX / 2) - 1)
/** Minimum cost */
#define PDDL_COST_MIN ((INT_MIN / 2) + 1)
/** Zeroize given struct */
#define PDDL_ZEROIZE(SPTR) bzero((SPTR), sizeof(*(SPTR)))


/**
 * Type for storing state ID.
 */
typedef unsigned int pddl_state_id_t;

#define PDDL_NO_STATE_ID ((pddl_state_id_t)-1)


/**
 * Type of one word in buffer of packed variable values.
 * Bear in mind that the word's size must be big enough to store the whole
 * range of the biggest variable.
 */
typedef uint32_t pddl_fdr_packer_word_t;

/**
 * Number of bits in packed word
 */
#define PDDL_FDR_PACKER_WORD_BITS (8u * sizeof(pddl_fdr_packer_word_t))

/**
 * Word with only highest bit set (i.e., 0x80000...)
 */
#define PDDL_FDR_PACKER_WORD_SET_HI_BIT \
    (((pddl_fdr_packer_word_t)1u) << (PDDL_FDR_PACKER_WORD_BITS - 1u))

/**
 * Word with all bits set (i.e., 0xffff...)
 */
#define PDDL_FDR_PACKER_WORD_SET_ALL_BITS ((pddl_fdr_packer_word_t)-1)


extern const char *pddl_version;

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_COMMON_H__ */
