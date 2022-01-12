/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_SEARCH_LIFTED_H__
#define __PDDL_SEARCH_LIFTED_H__

#include <pddl/search.h>
#include <pddl/homomorphism_heur.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_lifted_plan {
    char **plan;
    int plan_len;
    int plan_cost;
    int plan_alloc;
};
typedef struct pddl_lifted_plan pddl_lifted_plan_t;

typedef struct pddl_search_lifted pddl_search_lifted_t;

pddl_search_lifted_t *pddlSearchLiftedAStar(const pddl_t *pddl,
                                            pddl_homomorphism_heur_t *heur,
                                            bor_err_t *err);
pddl_search_lifted_t *pddlSearchLiftedGBFS(const pddl_t *pddl,
                                           pddl_homomorphism_heur_t *heur,
                                           bor_err_t *err);
pddl_search_lifted_t *pddlSearchLiftedLazy(const pddl_t *pddl,
                                           pddl_homomorphism_heur_t *heur,
                                           bor_err_t *err);

void pddlSearchLiftedDel(pddl_search_lifted_t *s);
int pddlSearchLiftedInitStep(pddl_search_lifted_t *s);
int pddlSearchLiftedStep(pddl_search_lifted_t *s);

void pddlSearchLiftedStat(const pddl_search_lifted_t *s,
                          pddl_search_stat_t *stat);
void pddlSearchLiftedStatLog(const pddl_search_lifted_t *s, bor_err_t *err);

const pddl_lifted_plan_t *pddlSearchLiftedPlan(const pddl_search_lifted_t *s);
void pddlSearchLiftedPlanPrint(const pddl_search_lifted_t *s, FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SEARCH_LIFTED_H__ */
