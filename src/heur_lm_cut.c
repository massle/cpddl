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

#include "pddl/lm_cut.h"
#include "_heur.h"

struct pddl_heur_lmc {
    pddl_heur_t heur;
    pddl_lm_cut_t lmc;
};
typedef struct pddl_heur_lmc pddl_heur_lmc_t;

static void heurDel(pddl_heur_t *_h)
{
    pddl_heur_lmc_t *h = bor_container_of(_h, pddl_heur_lmc_t, heur);
    _pddlHeurFree(&h->heur);
    pddlLMCutFree(&h->lmc);
    BOR_FREE(h);
}

static int heurEstimate(pddl_heur_t *_h,
                        const pddl_fdr_state_space_node_t *node,
                        const pddl_fdr_state_space_t *state_space)
{
    pddl_heur_lmc_t *h = bor_container_of(_h, pddl_heur_lmc_t, heur);
    return pddlLMCut(&h->lmc, node->state, NULL, NULL);
}

pddl_heur_t *pddlHeurLMCut(const pddl_fdr_t *fdr, bor_err_t *err)
{
    pddl_heur_lmc_t *h = BOR_ALLOC(pddl_heur_lmc_t);
    bzero(h, sizeof(*h));
    pddlLMCutInit(&h->lmc, fdr, 0, 0);
    _pddlHeurInit(&h->heur, heurDel, heurEstimate);
    return &h->heur;
}
