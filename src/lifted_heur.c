/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/lifted_heur.h"
#include "pddl/lifted_heur_relaxed.h"
#include "_lifted_heur.h"
#include "internal.h"

void pddlLiftedHeurDel(pddl_lifted_heur_t *h)
{
    h->del_fn(h);
}

pddl_cost_t pddlLiftedHeurEstimate(pddl_lifted_heur_t *h,
                                   const pddl_iset_t *state,
                                   const pddl_ground_atoms_t *gatoms)
{
    return h->estimate_fn(h, state, gatoms);
}

static void blindDel(pddl_lifted_heur_t *h)
{
    _pddlLiftedHeurFree(h);
    FREE(h);
}

static pddl_cost_t blindEstimate(pddl_lifted_heur_t *h,
                                 const pddl_iset_t *state,
                                 const pddl_ground_atoms_t *gatoms)
{
    return pddl_cost_zero;
}

pddl_lifted_heur_t *pddlLiftedHeurBlind(void)
{
    pddl_lifted_heur_t *h = ALLOC(pddl_lifted_heur_t);
    _pddlLiftedHeurInit(h, blindDel, blindEstimate);
    return h;
}

struct hrelax {
    pddl_lifted_heur_t h;
    pddl_lifted_heur_relaxed_t hrelax;
};
typedef struct hrelax hrelax_t;

static void hrelaxDel(pddl_lifted_heur_t *_h)
{
    hrelax_t *h = pddl_container_of(_h, hrelax_t, h);
    _pddlLiftedHeurFree(&h->h);
    pddlLiftedHeurRelaxedFree(&h->hrelax);
    FREE(h);
}

static pddl_cost_t hrelaxEstimate(pddl_lifted_heur_t *_h,
                                const pddl_iset_t *state,
                                const pddl_ground_atoms_t *gatoms)
{
    hrelax_t *h = pddl_container_of(_h, hrelax_t, h);
    return pddlLiftedHeurRelaxedEvalState(&h->hrelax, state, gatoms);
}

pddl_lifted_heur_t *pddlLiftedHeurHMax(const pddl_t *pddl, pddl_err_t *err)
{
    hrelax_t *h = ALLOC(hrelax_t);
    _pddlLiftedHeurInit(&h->h, hrelaxDel, hrelaxEstimate);
    pddlLiftedHMaxInit(&h->hrelax, pddl, 0, err);
    return &h->h;
}

pddl_lifted_heur_t *pddlLiftedHeurHAdd(const pddl_t *pddl, pddl_err_t *err)
{
    hrelax_t *h = ALLOC(hrelax_t);
    _pddlLiftedHeurInit(&h->h, hrelaxDel, hrelaxEstimate);
    pddlLiftedHAddInit(&h->hrelax, pddl, 0, err);
    return &h->h;
}

pddl_lifted_heur_t *pddlLiftedHeurHFFMax(const pddl_t *pddl, pddl_err_t *err)
{
    hrelax_t *h = ALLOC(hrelax_t);
    _pddlLiftedHeurInit(&h->h, hrelaxDel, hrelaxEstimate);
    pddlLiftedHFFMaxInit(&h->hrelax, pddl, err);
    return &h->h;
}

pddl_lifted_heur_t *pddlLiftedHeurHFFAdd(const pddl_t *pddl, pddl_err_t *err)
{
    hrelax_t *h = ALLOC(hrelax_t);
    _pddlLiftedHeurInit(&h->h, hrelaxDel, hrelaxEstimate);
    pddlLiftedHFFAddInit(&h->hrelax, pddl, err);
    return &h->h;
}


struct homomorph {
    pddl_lifted_heur_t h;
    pddl_homomorphism_heur_t *hom;
};
typedef struct homomorph homomorph_t;

static void homomorphDel(pddl_lifted_heur_t *_h)
{
    homomorph_t *h = pddl_container_of(_h, homomorph_t, h);
    _pddlLiftedHeurFree(&h->h);
    FREE(h);
}

static pddl_cost_t homomorphEstimate(pddl_lifted_heur_t *_h,
                                const pddl_iset_t *state,
                                const pddl_ground_atoms_t *gatoms)
{
    homomorph_t *h = pddl_container_of(_h, homomorph_t, h);
    pddl_cost_t c = pddl_cost_zero;
    c.cost = pddlHomomorphismHeurEval(h->hom, state, gatoms);
    return c;
}

pddl_lifted_heur_t *pddlLiftedHeurHomomorphism(pddl_homomorphism_heur_t *hom)
{
    homomorph_t *h = ALLOC(homomorph_t);
    _pddlLiftedHeurInit(&h->h, homomorphDel, homomorphEstimate);
    h->hom = hom;
    return &h->h;
}
