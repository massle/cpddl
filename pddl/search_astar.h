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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define PDDL_SEARCH_CONT 0
#define PDDL_SEARCH_UNSOLVABLE 1
#define PDDL_SEARCH_FOUND 2

struct pddl_search_astar {
    const pddl_fdr_t *fdr;
    int pathmax; /*!< Use pathmax correction */
    pddl_fdr_state_space_t state_space;
    pddl_open_list_t *list;
    pddl_fdr_app_op_t app_op;

    pddl_state_id_t goal_state_id;

    bor_iset_t applicable;
    pddl_fdr_state_space_node_t cur_node;
    pddl_fdr_state_space_node_t next_node;
};
typedef struct pddl_search_astar pddl_search_astar_t;

pddl_search_astar_t *pddlSearchAStar(const pddl_fdr_t *fdr,
                                     int use_pathmax);
void pddlSearchAStarDel(pddl_search_astar_t *astar);
int pddlSearchAStarInitStep(pddl_search_astar_t *astar);
int pddlSearchAStarStep(pddl_search_astar_t *astar);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SEARCH_ASTAR_H__ */
