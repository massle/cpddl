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

#include "_heur.h"

struct pot_func {
    double *pot; /*!< Potential function */
    int *state; /*!< State for which potential heuristic was computed */
    bor_list_t conn; /*!< Connector to the list of potential functions */
};
typedef struct pot_func pot_func_t;

static pot_func_t *potFuncNew(const pddl_fdr_t *fdr,
                              const int *state,
                              bor_list_t *list)
{
    pot_func_t *f = BOR_ALLOC(pot_func_t);
    f->pot = BOR_ALLOC_ARR(double, fdr->var.global_id_size);
    f->state = BOR_ALLOC_ARR(int, fdr->var.var_size);
    borListInit(&f->conn);

    pddl_pot_t pot;
    pddlPotInitFDR(&pot, fdr);
    pddlPotSetObjFDRState(&pot, &fdr->var, state);
    double *pfunc = BOR_ALLOC_ARR(double, pot.var_size);
    pddlPotSolve(&pot, pfunc, pot.var_size, NULL, NULL, 0);
    memcpy(f->pot, pfunc, sizeof(double) * fdr->var.global_id_size);
    pddlPotFree(&pot);

    memcpy(f->state, state, sizeof(int) * fdr->var.var_size);
    borListAppend(list, &f->conn);

    return f;
}

static void potFuncDel(pot_func_t *f)
{
    BOR_FREE(f->pot);
    BOR_FREE(f->state);
    borListDel(&f->conn);
    BOR_FREE(f);
}

// TODO: Refactor with hpot
#define ROUND_EPS 0.001
static int roundOff(double z)
{
    return ceil(z - ROUND_EPS);
}

static int potFuncHeur(const pot_func_t *f,
                       const int *state,
                       const pddl_fdr_vars_t *vars)
{
    double p = 0;
    for (int var = 0; var < vars->var_size; ++var)
        p += f->pot[vars->var[var].val[state[var]].global_id];
    if (p < 0.)
        return 0;
    if (p > 1E8)
        return PDDL_COST_DEAD_END;
    return roundOff(p);
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
    BOR_FREE(h);
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
    heur_t *h = BOR_ALLOC(heur_t);
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


