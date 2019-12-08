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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_SEARCH_ASTAR_H__
#define __PDDL_SEARCH_ASTAR_H__

#include <pddl/open_list.h>
#include <pddl/fdr.h>
#include <pddl/fdr_state_space.h>
#include <pddl/fdr_app_op.h>
#include <pddl/heur.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define PDDL_SEARCH_CONT 0
#define PDDL_SEARCH_UNSOLVABLE 1
#define PDDL_SEARCH_FOUND 2

struct pddl_search_stat {
    size_t steps; /*!< Number of calls to *Step() */
    size_t expanded; /*!< Number of times expansions of states */
    size_t evaluated; /*!< Number of times heuristic function is evaluated */
    size_t generated; /*!< Number of different states created so far */
    size_t open; /*!< Number of states currently in the open list */
    size_t closed; /*!< Number of closed states so far */
    size_t reopen; /*!< Number of times a state was re-opened. */
    size_t dead_end; /*!< Number of states detected as dead-end states */
    int last_f_value;
};
typedef struct pddl_search_stat pddl_search_stat_t;

struct pddl_search_astar {
    const pddl_fdr_t *fdr;
    pddl_heur_t *heur;
    bor_err_t *err;
    pddl_fdr_state_space_t state_space;
    pddl_open_list_t *list;
    pddl_fdr_app_op_t app_op;

    pddl_state_id_t goal_state_id;

    bor_iset_t applicable;
    pddl_fdr_state_space_node_t cur_node;
    pddl_fdr_state_space_node_t next_node;
    pddl_search_stat_t _stat;
};
typedef struct pddl_search_astar pddl_search_astar_t;

pddl_search_astar_t *pddlSearchAStar(const pddl_fdr_t *fdr,
                                     pddl_heur_t *heur,
                                     bor_err_t *err);
void pddlSearchAStarDel(pddl_search_astar_t *astar);
int pddlSearchAStarInitStep(pddl_search_astar_t *astar);
int pddlSearchAStarStep(pddl_search_astar_t *astar);

void pddlSearchAStarStat(const pddl_search_astar_t *astar,
                         pddl_search_stat_t *stat);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SEARCH_ASTAR_H__ */
