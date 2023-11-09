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

struct hmax {
    pddl_lifted_heur_t h;
    pddl_lifted_hmax_t hmax;
};
typedef struct hmax hmax_t;

static void hmaxDel(pddl_lifted_heur_t *_h)
{
    hmax_t *h = pddl_container_of(_h, hmax_t, h);
    _pddlLiftedHeurFree(&h->h);
    pddlLiftedHMaxFree(&h->hmax);
    FREE(h);
}

static pddl_cost_t hmaxEstimate(pddl_lifted_heur_t *_h,
                                const pddl_iset_t *state,
                                const pddl_ground_atoms_t *gatoms)
{
    hmax_t *h = pddl_container_of(_h, hmax_t, h);
    return pddlLiftedHMax(&h->hmax, state, gatoms);
}

pddl_lifted_heur_t *pddlLiftedHeurHMax(const pddl_t *pddl, pddl_err_t *err)
{
    hmax_t *h = ALLOC(hmax_t);
    _pddlLiftedHeurInit(&h->h, hmaxDel, hmaxEstimate);
    pddlLiftedHMaxInit(&h->hmax, pddl, 0, err);
    return &h->h;
}

struct hadd {
    pddl_lifted_heur_t h;
    pddl_lifted_hadd_t hadd;
};
typedef struct hadd hadd_t;

static void haddDel(pddl_lifted_heur_t *_h)
{
    hadd_t *h = pddl_container_of(_h, hadd_t, h);
    _pddlLiftedHeurFree(&h->h);
    pddlLiftedHAddFree(&h->hadd);
    FREE(h);
}

static pddl_cost_t haddEstimate(pddl_lifted_heur_t *_h,
                                const pddl_iset_t *state,
                                const pddl_ground_atoms_t *gatoms)
{
    hadd_t *h = pddl_container_of(_h, hadd_t, h);
    return pddlLiftedHAdd(&h->hadd, state, gatoms);
}

pddl_lifted_heur_t *pddlLiftedHeurHAdd(const pddl_t *pddl, pddl_err_t *err)
{
    hadd_t *h = ALLOC(hadd_t);
    _pddlLiftedHeurInit(&h->h, haddDel, haddEstimate);
    pddlLiftedHAddInit(&h->hadd, pddl, 0, err);
    return &h->h;
}

struct hff_max {
    pddl_lifted_heur_t h;
    pddl_lifted_hff_max_t hff_max;
};
typedef struct hff_max hff_max_t;

static void hffMaxDel(pddl_lifted_heur_t *_h)
{
    hff_max_t *h = pddl_container_of(_h, hff_max_t, h);
    _pddlLiftedHeurFree(&h->h);
    pddlLiftedHFFMaxFree(&h->hff_max);
    FREE(h);
}

static pddl_cost_t hffMaxEstimate(pddl_lifted_heur_t *_h,
                                  const pddl_iset_t *state,
                                  const pddl_ground_atoms_t *gatoms)
{
    hff_max_t *h = pddl_container_of(_h, hff_max_t, h);
    return pddlLiftedHFFMax(&h->hff_max, state, gatoms);
}

pddl_lifted_heur_t *pddlLiftedHeurHFFMax(const pddl_t *pddl, pddl_err_t *err)
{
    hff_max_t *h = ALLOC(hff_max_t);
    _pddlLiftedHeurInit(&h->h, hffMaxDel, hffMaxEstimate);
    pddlLiftedHFFMaxInit(&h->hff_max, pddl, err);
    return &h->h;
}

struct hff_add {
    pddl_lifted_heur_t h;
    pddl_lifted_hff_add_t hff_add;
};
typedef struct hff_add hff_add_t;

static void hffAddDel(pddl_lifted_heur_t *_h)
{
    hff_add_t *h = pddl_container_of(_h, hff_add_t, h);
    _pddlLiftedHeurFree(&h->h);
    pddlLiftedHFFAddFree(&h->hff_add);
    FREE(h);
}

static pddl_cost_t hffAddEstimate(pddl_lifted_heur_t *_h,
                                  const pddl_iset_t *state,
                                  const pddl_ground_atoms_t *gatoms)
{
    hff_add_t *h = pddl_container_of(_h, hff_add_t, h);
    return pddlLiftedHFFAdd(&h->hff_add, state, gatoms);
}

pddl_lifted_heur_t *pddlLiftedHeurHFFAdd(const pddl_t *pddl, pddl_err_t *err)
{
    hff_add_t *h = ALLOC(hff_add_t);
    _pddlLiftedHeurInit(&h->h, hffAddDel, hffAddEstimate);
    pddlLiftedHFFAddInit(&h->hff_add, pddl, err);
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
