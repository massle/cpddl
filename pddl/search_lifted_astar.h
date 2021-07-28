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

#ifndef __PDDL_SEARCH_LIFTED_ASTAR_H__
#define __PDDL_SEARCH_LIFTED_ASTAR_H__

#include <pddl/open_list.h>
#include <pddl/strips_state_space.h>
#include <pddl/strips_maker.h>
#include <pddl/sql_grounder.h>
#include <pddl/fdr_app_op.h>
#include <pddl/heur.h>
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

struct pddl_search_lifted_astar {
    const pddl_t *pddl;
    pddl_homomorphism_heur_t *heur;
    bor_err_t *err;
    pddl_sql_grounder_t *grounder;
    pddl_strips_maker_t strips;
    pddl_strips_state_space_t state_space;
    pddl_open_list_t *list;

    bor_iset_t applicable;
    pddl_strips_state_space_node_t cur_node;
    pddl_strips_state_space_node_t next_node;
    pddl_search_stat_t _stat;

    bor_iset_t goal;
    int goal_is_unreachable;
    pddl_lifted_plan_t plan;
};
typedef struct pddl_search_lifted_astar pddl_search_lifted_astar_t;

pddl_search_lifted_astar_t *pddlSearchLiftedAStar(
                                const pddl_t *pddl,
                                pddl_homomorphism_heur_t *heur,
                                bor_err_t *err);
void pddlSearchLiftedAStarDel(pddl_search_lifted_astar_t *astar);
int pddlSearchLiftedAStarInitStep(pddl_search_lifted_astar_t *astar);
int pddlSearchLiftedAStarStep(pddl_search_lifted_astar_t *astar);

void pddlSearchLiftedAStarStat(const pddl_search_lifted_astar_t *astar,
                               pddl_search_stat_t *stat);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SEARCH_LIFTED_ASTAR_H__ */
