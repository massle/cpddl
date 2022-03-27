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
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include "pddl/hmax.h"
#include "alloc.h"
#include "_heur.h"

struct pddl_heur_hmax {
    pddl_heur_t heur;
    const pddl_fdr_t *fdr;
    pddl_hmax_t hmax;
};
typedef struct pddl_heur_hmax pddl_heur_hmax_t;

static void heurDel(pddl_heur_t *_h)
{
    pddl_heur_hmax_t *h = bor_container_of(_h, pddl_heur_hmax_t, heur);
    _pddlHeurFree(&h->heur);
    pddlHMaxFree(&h->hmax);
    FREE(h);
}

static int heurEstimate(pddl_heur_t *_h,
                        const pddl_fdr_state_space_node_t *node,
                        const pddl_fdr_state_space_t *state_space)
{
    pddl_heur_hmax_t *h = bor_container_of(_h, pddl_heur_hmax_t, heur);
    return pddlHMax(&h->hmax, node->state, &h->fdr->var);
}

pddl_heur_t *pddlHeurHMax(const pddl_fdr_t *fdr, bor_err_t *err)
{
    pddl_heur_hmax_t *h = ALLOC(pddl_heur_hmax_t);
    bzero(h, sizeof(*h));
    pddlHMaxInit(&h->hmax, fdr);
    h->fdr = fdr;
    _pddlHeurInit(&h->heur, heurDel, heurEstimate);
    return &h->heur;
}

