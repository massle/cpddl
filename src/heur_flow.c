/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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

#include <boruvka/extarr.h>
#include "pddl/hflow.h"
#include "_heur.h"

struct pddl_heur_flow {
    pddl_heur_t heur;
    pddl_hflow_t flow;
    bor_extarr_t *cache; // TODO: Refactor and generalize
};
typedef struct pddl_heur_flow pddl_heur_flow_t;

static void heurDel(pddl_heur_t *_h)
{
    pddl_heur_flow_t *h = bor_container_of(_h, pddl_heur_flow_t, heur);
    _pddlHeurFree(&h->heur);
    pddlHFlowFree(&h->flow);
    borExtArrDel(h->cache);
    BOR_FREE(h);
}

static int heurEstimate(pddl_heur_t *_h,
                        const pddl_fdr_state_space_node_t *node,
                        const pddl_fdr_state_space_t *state_space)
{
    pddl_heur_flow_t *h = bor_container_of(_h, pddl_heur_flow_t, heur);
    int *hval = borExtArrGet(h->cache, node->id);
    if (*hval == PDDL_COST_MAX)
        *hval = pddlHFlow(&h->flow, node->state, NULL);
    return *hval;
}

pddl_heur_t *pddlHeurFlow(const pddl_fdr_t *fdr, bor_err_t *err)
{
    pddl_heur_flow_t *h = BOR_ALLOC(pddl_heur_flow_t);
    bzero(h, sizeof(*h));
    pddlHFlowInit(&h->flow, fdr, 0);
    _pddlHeurInit(&h->heur, heurDel, heurEstimate);
    int init = PDDL_COST_MAX;
    h->cache = borExtArrNew2(sizeof(int), 1024, 1024 * 1024, NULL, &init);
    return &h->heur;
}

