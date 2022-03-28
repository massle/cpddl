/***
 * cpddl
 * -------
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_OP_MUTEX_SYM_REDUNDANT_H__
#define __PDDL_OP_MUTEX_SYM_REDUNDANT_H__

#include <boruvka/err.h>
#include <pddl/op_mutex_pair.h>
#include <pddl/sym.h>
#include <pddl/strips.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Computes a set of redundant operators using fixpoint computation with a
 * set of operator mutexes and symmetries.
 */
void pddlOpMutexSymRedundantFixpoint(pddl_iset_t *redundant,
                                     const pddl_strips_t *strips,
                                     const pddl_strips_sym_t *sym,
                                     const pddl_op_mutex_pairs_t *op_mutex,
                                     bor_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_OP_MUTEX_SYM_REDUNDANT_H__ */
