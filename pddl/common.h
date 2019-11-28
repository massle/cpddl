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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_COMMON_H__
#define __PDDL_COMMON_H__

#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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


/**
 * Type for storing state ID.
 */
typedef unsigned long pddl_state_id_t;


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

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_COMMON_H__ */
