/***
 * cpddl
 * -------
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
 * FAI Group at Saarland University, and
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

#ifndef __PDDL_SYMBOLIC_SPLIT_GOAL_H__
#define __PDDL_SYMBOLIC_SPLIT_GOAL_H__

#include <pddl/bdds.h>
#include <pddl/mg_strips.h>
#include <pddl/symbolic_vars.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void pddlSymbolicSplitGoalByPot(const pddl_iset_t *goal,
                                const pddl_mgroups_t *mgroups,
                                const pddl_mutex_pairs_t *mutex,
                                const double *pot,
                                pddl_symbolic_vars_t *symb_vars,
                                pddl_bdd_manager_t *mgr,
                                pddl_bdds_t *bdds,
                                pddl_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SYMBOLIC_SPLIT_GOAL_H__ */
