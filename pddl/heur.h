/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_HEUR_H__
#define __PDDL_HEUR_H__

#include <pddl/fdr_state_space.h>
#include <pddl/hpot.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct pddl_heur pddl_heur_t;

/**
 * Blind heuristic returning estimate 0 for every state.
 */
pddl_heur_t *pddlHeurBlind(void);

/**
 * Potential heuristic.
 */
pddl_heur_t *pddlHeurPot(const pddl_fdr_t *fdr,
                         const pddl_hpot_config_t *cfg,
                         bor_err_t *err);

/**
 * TODO
 */
pddl_heur_t *pddlHeurPotState(const pddl_fdr_t *fdr, bor_err_t *err);

/**
 * Flow heuristic
 */
pddl_heur_t *pddlHeurFlow(const pddl_fdr_t *fdr, bor_err_t *err);

/**
 * LM-Cut heuristic
 */
pddl_heur_t *pddlHeurLMCut(const pddl_fdr_t *fdr, bor_err_t *err);

/**
 * h^max heuristic
 */
pddl_heur_t *pddlHeurHMax(const pddl_fdr_t *fdr, bor_err_t *err);

/**
 * h^add heuristic
 */
pddl_heur_t *pddlHeurHAdd(const pddl_fdr_t *fdr, bor_err_t *err);

/**
 * Destructor
 */
void pddlHeurDel(pddl_heur_t *h);

/**
 * Computes and returns a heuristic estimate.
 */
int pddlHeurEstimate(pddl_heur_t *h,
                     const pddl_fdr_state_space_node_t *node,
                     const pddl_fdr_state_space_t *state_space);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_HEUR_H__ */
