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

#include <boruvka/alloc.h>
#include <boruvka/hfunc.h>
#include <boruvka/lp.h>
#include "pddl/pot.h"

#define LPVAR_UPPER 1E7
#define LPVAR_LOWER -1E7

struct maxpot {
    bor_iset_t vars;
    int var_id;
    int id;

    bor_htable_key_t hkey;
    bor_list_t htable;
};
typedef struct maxpot maxpot_t;

static bor_htable_key_t maxpotComputeHash(const bor_iset_t *set)
{
    return borCityHash_64(set->s, sizeof(int) * set->size);
}

static bor_htable_key_t htableHash(const bor_list_t *key, void *_)
{
    const maxpot_t *m = BOR_LIST_ENTRY(key, maxpot_t, htable);
    return m->hkey;
}

static int htableEq(const bor_list_t *key1, const bor_list_t *key2, void *_)
{
    const maxpot_t *m1 = BOR_LIST_ENTRY(key1, maxpot_t, htable);
    const maxpot_t *m2 = BOR_LIST_ENTRY(key2, maxpot_t, htable);
    return borISetEq(&m1->vars, &m2->vars);
}

static int fdrVar(const pddl_pot_t *pot, int var, int val)
{
    return pot->fdr_var_offset[var] + val;
}

static void addMaxpotConstr(pddl_pot_t *pot, int maxpot_var_id, int var_id)
{
    if (pot->constr_maxpot_size >= pot->constr_maxpot_alloc){
        if (pot->constr_maxpot_alloc == 0)
            pot->constr_maxpot_alloc = 4;
        pot->constr_maxpot_alloc *= 2;
        pot->constr_maxpot = BOR_REALLOC_ARR(pot->constr_maxpot,
                                             pddl_pot_constr_t,
                                             pot->constr_maxpot_alloc);
    }

    pddl_pot_constr_t *c = pot->constr_maxpot + pot->constr_maxpot_size++;
    bzero(c, sizeof(*c));
    borISetAdd(&c->plus, var_id);
    borISetAdd(&c->minus, maxpot_var_id);
    c->rhs = 0;
}

static void addMaxpotConstrs(pddl_pot_t *pot,
                             int maxpot_var_id,
                             const bor_iset_t *maxpot_vars)
{
    int var_id;
    BOR_ISET_FOR_EACH(maxpot_vars, var_id)
        addMaxpotConstr(pot, maxpot_var_id, var_id);
}

static int getMaxpot(pddl_pot_t *pot, const bor_iset_t *set)
{
    maxpot_t *m = borSegmArrGet(pot->maxpot, pot->maxpot_size);
    m->vars = *set;
    m->hkey = maxpotComputeHash(set);
    borListInit(&m->htable);

    bor_list_t *found;
    found = borHTableInsertUnique(pot->maxpot_htable, &m->htable);
    if (found == NULL){
        borISetInit(&m->vars);
        borISetUnion(&m->vars, set);
        m->id = pot->maxpot_size++;
        m->var_id = pot->var_size++;
        addMaxpotConstrs(pot, m->var_id, &m->vars);
        return m->var_id;

    }else{
        m = BOR_LIST_ENTRY(found, maxpot_t, htable);
        return m->var_id;
    }
}

static int getFDRMaxpot(pddl_pot_t *pot,
                        int var_id,
                        const pddl_fdr_vars_t *vars)
{
    BOR_ISET(lp_vars);
    for (int val = 0; val < vars->var[var_id].val_size; ++val)
        borISetAdd(&lp_vars, fdrVar(pot, var_id, val));
    int lp_var_id = getMaxpot(pot, &lp_vars);
    borISetFree(&lp_vars);
    return lp_var_id;
}

static void addFDROp(pddl_pot_t *pot,
                     const pddl_fdr_vars_t *vars,
                     const pddl_fdr_op_t *op)
{
    if (pot->constr_op_size >= pot->constr_op_alloc){
        if (pot->constr_op_alloc == 0)
            pot->constr_op_alloc = 4;
        pot->constr_op_alloc *= 2;
        pot->constr_op = BOR_REALLOC_ARR(pot->constr_op, pddl_pot_constr_t,
                                         pot->constr_op_alloc);
    }

    pddl_pot_constr_t *c = pot->constr_op + pot->constr_op_size++;
    bzero(c, sizeof(*c));

    for (int effi = 0; effi < op->eff.fact_size; ++effi){
        const pddl_fdr_fact_t *eff = op->eff.fact + effi;
        int pre = pddlFDRPartStateGet(&op->pre, eff->var);
        if (pre >= 0){
            borISetAdd(&c->plus, fdrVar(pot, eff->var, pre));
        }else{
            borISetAdd(&c->plus, getFDRMaxpot(pot, eff->var, vars));
        }
        borISetAdd(&c->minus, fdrVar(pot, eff->var, eff->val));
    }
    c->rhs = op->cost;
}

static void setFDRGoal(pddl_pot_t *pot,
                       const pddl_fdr_vars_t *vars,
                       const pddl_fdr_part_state_t *goal)
{
    for (int var_id = 0; var_id < vars->var_size; ++var_id){
        int eff = pddlFDRPartStateGet(goal, var_id);
        if (eff >= 0){
            borISetAdd(&pot->constr_goal.plus, fdrVar(pot, var_id, eff));
        }else{
            borISetAdd(&pot->constr_goal.plus, getFDRMaxpot(pot, var_id, vars));
        }
    }
    pot->constr_goal.rhs = 0;
}

void pddlPotInitFDR(pddl_pot_t *pot, const pddl_fdr_t *fdr)
{
    bzero(pot, sizeof(*pot));

    pot->fdr_var_offset = BOR_CALLOC_ARR(int, fdr->var.var_size);
    for (int vi = 1; vi < fdr->var.var_size; ++vi){
        pot->fdr_var_offset[vi] = pot->fdr_var_offset[vi - 1];
        pot->fdr_var_offset[vi] += fdr->var.var[vi - 1].val_size;
    }
    pot->var_size = pot->fdr_var_offset[fdr->var.var_size - 1];
    pot->var_size += fdr->var.var[fdr->var.var_size - 1].val_size;

    int segm_size = BOR_MAX(fdr->var.var_size, 8) * sizeof(maxpot_t);
    pot->maxpot_size = 0;
    pot->maxpot = borSegmArrNew(sizeof(maxpot_t), segm_size);
    pot->maxpot_htable = borHTableNew(htableHash, htableEq, NULL);

    pot->constr_op_alloc = fdr->op.op_size;
    pot->constr_op = BOR_ALLOC_ARR(pddl_pot_constr_t, pot->constr_op_alloc);
    for (int op_id = 0; op_id < fdr->op.op_size; ++op_id)
        addFDROp(pot, &fdr->var, fdr->op.op[op_id]);

    setFDRGoal(pot, &fdr->var, &fdr->goal);

    pot->obj = BOR_CALLOC_ARR(double, pot->var_size);
}

void pddlPotFree(pddl_pot_t *pot)
{
    if (pot->maxpot_htable != NULL)
        borHTableDel(pot->maxpot_htable);
    for (int mi = 0; mi < pot->maxpot_size; ++mi){
        maxpot_t *m = borSegmArrGet(pot->maxpot, mi);
        borISetFree(&m->vars);
    }
    if (pot->maxpot != NULL)
        borSegmArrDel(pot->maxpot);

    if (pot->obj != NULL)
        BOR_FREE(pot->obj);

    if (pot->fdr_var_offset != NULL)
        BOR_FREE(pot->fdr_var_offset);
}

void pddlPotSetObjFDRState(pddl_pot_t *pot,
                           const pddl_fdr_vars_t *vars,
                           const int *state)
{
    bzero(pot->obj, sizeof(*pot->obj) * pot->var_size);
    for (int var_id = 0; var_id < vars->var_size; ++var_id)
        pot->obj[fdrVar(pot, var_id, state[var_id])] = 1.;
}

void pddlPotSetObjFDRAllSyntacticStates(pddl_pot_t *pot,
                                        const pddl_fdr_vars_t *vars)
{
    bzero(pot->obj, sizeof(*pot->obj) * pot->var_size);
    for (int var_id = 0; var_id < vars->var_size; ++var_id){
        double c = 1. / vars->var[var_id].val_size;
        for (int val = 0; val < vars->var[var_id].val_size; ++val){
            pot->obj[fdrVar(pot, var_id, val)] = c;
        }
    }
}

static void setConstr(bor_lp_t *lp,
                      int row,
                      const pddl_pot_t *pot,
                      const pddl_pot_constr_t *c)
{
    int var;

    BOR_ISET_FOR_EACH(&c->plus, var)
        borLPSetCoef(lp, row, var, 1);
    BOR_ISET_FOR_EACH(&c->minus, var)
        borLPSetCoef(lp, row, var, -1);
    borLPSetRHS(lp, row, c->rhs, 'L');
}

int pddlPotSolve(const pddl_pot_t *pot, double *w, int var_size, int use_ilp)
{
    int ret = 0;
    bor_lp_t *lp;

    unsigned lp_flags;
    lp_flags  = BOR_LP_MAX;
    lp_flags |= BOR_LP_NUM_THREADS(1);

    int rows = pot->constr_op_size + 1 + pot->constr_maxpot_size;
    lp = borLPNew(rows, pot->var_size, lp_flags);

    for (int i = 0; i < pot->var_size; ++i){
        if (use_ilp)
            borLPSetVarInt(lp, i);
        borLPSetVarRange(lp, i, LPVAR_LOWER, LPVAR_UPPER);
        borLPSetObj(lp, i, pot->obj[i]);
    }

    int row = 0;
    for (int ci = 0; ci < pot->constr_op_size; ++ci)
        setConstr(lp, row++, pot, pot->constr_op + ci);
    setConstr(lp, row++, pot, &pot->constr_goal);
    for (int ci = 0; ci < pot->constr_maxpot_size; ++ci)
        setConstr(lp, row++, pot, pot->constr_maxpot + ci);

    double objval, *obj;
    obj = BOR_CALLOC_ARR(double, pot->var_size);
    if (borLPSolve(lp, &objval, obj) == 0){
        memcpy(w, obj, sizeof(double) * var_size);
    }else{
        bzero(w, sizeof(double) * var_size);
        ret = -1;
    }

    BOR_FREE(obj);
    borLPDel(lp);

    return ret;
}
