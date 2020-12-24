/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_SEARCH_LAZY_H__
#define __PDDL_SEARCH_LAZY_H__

#include <pddl/open_list.h>
#include <pddl/fdr.h>
#include <pddl/fdr_state_space.h>
#include <pddl/fdr_app_op.h>
#include <pddl/heur.h>
#include <pddl/search.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_search_lazy {
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
typedef struct pddl_search_lazy pddl_search_lazy_t;

pddl_search_lazy_t *pddlSearchLazy(const pddl_fdr_t *fdr,
                                   pddl_heur_t *heur,
                                   bor_err_t *err);
void pddlSearchLazyDel(pddl_search_lazy_t *lazy);
int pddlSearchLazyInitStep(pddl_search_lazy_t *lazy);
int pddlSearchLazyStep(pddl_search_lazy_t *lazy);

void pddlSearchLazyStat(const pddl_search_lazy_t *lazy,
                        pddl_search_stat_t *stat);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SEARCH_LAZY_H__ */
