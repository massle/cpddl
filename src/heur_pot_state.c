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
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include "alloc.h"
#include "_heur.h"

struct pot_func {
    pddl_pot_solution_t sol;
    int *state; /*!< State for which potential heuristic was computed */
    bor_list_t conn; /*!< Connector to the list of potential functions */
};
typedef struct pot_func pot_func_t;

static pot_func_t *potFuncNew(const pddl_fdr_t *fdr,
                              const int *state,
                              bor_list_t *list)
{
    pot_func_t *f = ALLOC(pot_func_t);
    pddlPotSolutionInit(&f->sol);
    f->state = ALLOC_ARR(int, fdr->var.var_size);
    borListInit(&f->conn);

    pddl_pot_t pot;
    pddlPotInitFDR(&pot, fdr);
    pddlPotSetObjFDRState(&pot, &fdr->var, state);

    pddlPotSolve(&pot, &f->sol);
    pddlPotFree(&pot);

    memcpy(f->state, state, sizeof(int) * fdr->var.var_size);
    borListAppend(list, &f->conn);

    return f;
}

static void potFuncDel(pot_func_t *f)
{
    pddlPotSolutionFree(&f->sol);
    FREE(f->state);
    borListDel(&f->conn);
    FREE(f);
}

// TODO: Refactor with hpot

static int potFuncHeur(const pot_func_t *f,
                       const int *state,
                       const pddl_fdr_vars_t *vars)
{
    return pddlPotSolutionEvalFDRState(&f->sol, vars, state);
}


struct heur {
    pddl_heur_t heur;
    const pddl_fdr_t *fdr;
    bor_err_t *err;
    bor_extarr_t *func_state;
    bor_list_t func_list;
};
typedef struct heur heur_t;
#define HEUR(H) bor_container_of((H), heur_t, heur)

static void heurDel(pddl_heur_t *_h)
{
    heur_t *h = HEUR(_h);
    _pddlHeurFree(&h->heur);
    borExtArrDel(h->func_state);
    while (!borListEmpty(&h->func_list)){
        bor_list_t *l = borListNext(&h->func_list);
        pot_func_t *f = BOR_LIST_ENTRY(l, pot_func_t, conn);
        potFuncDel(f);
    }
    FREE(h);
}

static int recompute(const heur_t *h,
                     const pddl_fdr_state_space_node_t *node,
                     const pddl_fdr_state_space_t *state_space)
{
    if (node->parent_id == PDDL_NO_STATE_ID)
        return 1;

    const pot_func_t **prev_p = borExtArrGet(h->func_state, node->parent_id);
    int diff = 0;
    for (int var = 0; var < h->fdr->var.var_size; ++var){
        if (node->state[var] != (*prev_p)->state[var])
            ++diff;
    }
    if (diff >= h->fdr->var.var_size * 0.5)
        return 1;
    return 0;
}

static int heurEstimate(pddl_heur_t *_h,
                        const pddl_fdr_state_space_node_t *node,
                        const pddl_fdr_state_space_t *state_space)
{
    heur_t *h = HEUR(_h);
    pot_func_t **pf = borExtArrGet(h->func_state, node->id);
    pot_func_t *f = *pf;
    if (f == NULL){
        if (recompute(h, node, state_space)){
            f = potFuncNew(h->fdr, node->state, &h->func_list);
        }else{
            pot_func_t **prev = borExtArrGet(h->func_state, node->parent_id);
            f = *prev;
        }
    }
    *pf = f;

    return potFuncHeur(f, node->state, &h->fdr->var);
}

pddl_heur_t *pddlHeurPotState(const pddl_fdr_t *fdr, bor_err_t *err)
{
    heur_t *h = ALLOC(heur_t);
    bzero(h, sizeof(*h));
    _pddlHeurInit(&h->heur, heurDel, heurEstimate);
    h->fdr = fdr;
    h->err = err;
    pot_func_t *f_init = NULL;
    h->func_state = borExtArrNew2(sizeof(pot_func_t *),
                                  1024, 1024 * 1024, NULL, &f_init);
    borListInit(&h->func_list);
    return &h->heur;
}


