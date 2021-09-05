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

#include <boruvka/alloc.h>
#include <boruvka/hfunc.h>
#include <boruvka/lp.h>
#include "pddl/pot.h"
#include "pddl/disambiguation.h"
#include "assert.h"

#define LPVAR_UPPER 1E7
#define LPVAR_LOWER -1E20
#define ROUND_EPS 0.001

static int roundOff(double z)
{
    return ceil(z - ROUND_EPS);
}

static int potFltToInt(double pot)
{
    if (pot < 0.)
        return 0;
    if (pot > INT_MAX / 2 || pot > LPVAR_UPPER)
        return PDDL_COST_DEAD_END;
    return roundOff(pot);
}


void pddlPotSolutionInit(pddl_pot_solution_t *sol)
{
    bzero(sol, sizeof(*sol));
}

void pddlPotSolutionFree(pddl_pot_solution_t *sol)
{
    if (sol->pot != NULL)
        BOR_FREE(sol->pot);
    if (sol->op_pot != NULL)
        BOR_FREE(sol->op_pot);
}

double pddlPotSolutionEvalFDRStateFlt(const pddl_pot_solution_t *sol,
                                      const pddl_fdr_vars_t *vars,
                                      const int *state)
{
    // Use kahan summation
    double sum = 0.;
    double comp = 0.;
    for (int var = 0; var < vars->var_size; ++var){
        double v = sol->pot[vars->var[var].val[state[var]].global_id];
        double y = v - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum = t;
    }
    return sum;
}

int pddlPotSolutionEvalFDRState(const pddl_pot_solution_t *sol,
                                const pddl_fdr_vars_t *vars,
                                const int *state)
{
    return potFltToInt(pddlPotSolutionEvalFDRStateFlt(sol, vars, state));
}

double pddlPotSolutionEvalStripsStateFlt(const pddl_pot_solution_t *sol,
                                         const bor_iset_t *state)
{
    // Use kahan summation
    double sum = 0.;
    double comp = 0.;
    int fact_id;
    BOR_ISET_FOR_EACH(state, fact_id){
        double v = sol->pot[fact_id];
        double y = v - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum = t;
    }
    return sum;
}

int pddlPotSolutionEvalStripsState(const pddl_pot_solution_t *sol,
                                   const bor_iset_t *state)
{
    return potFltToInt(pddlPotSolutionEvalStripsStateFlt(sol, state));
}

void pddlPotSolutionsInit(pddl_pot_solutions_t *sols)
{
    bzero(sols, sizeof(*sols));
}

void pddlPotSolutionsFree(pddl_pot_solutions_t *sols)
{
    for (int i = 0; i < sols->sol_size; ++i)
        pddlPotSolutionFree(sols->sol + i);
}

void pddlPotSolutionsAdd(pddl_pot_solutions_t *sols,
                         const pddl_pot_solution_t *sol)
{
    if (sols->sol_size >= sols->sol_alloc){
        if (sols->sol_alloc == 0)
            sols->sol_alloc = 1;
        sols->sol_alloc *= 2;
        sols->sol = BOR_REALLOC_ARR(sols->sol, pddl_pot_solution_t,
                                    sols->sol_alloc);
    }

    pddl_pot_solution_t *s = sols->sol + sols->sol_size++;
    pddlPotSolutionInit(s);
    s->pot_size = sol->pot_size;
    if (s->pot_size > 0){
        s->pot = BOR_ALLOC_ARR(double, s->pot_size);
        memcpy(s->pot, sol->pot, sizeof(double) * s->pot_size);
    }
    s->op_pot_size = sol->op_pot_size;
    if (s->op_pot_size > 0){
        s->op_pot = BOR_ALLOC_ARR(double, s->op_pot_size);
        memcpy(s->op_pot, sol->op_pot, sizeof(double) * s->op_pot_size);
    }
}

int pddlPotSolutionsEvalMaxFDRState(const pddl_pot_solutions_t *sols,
                                    const pddl_fdr_vars_t *vars,
                                    const int *fdr_state)
{
    int h = 0;
    for (int i = 0; i < sols->sol_size; ++i){
        int h1 = pddlPotSolutionEvalFDRState(sols->sol + i, vars, fdr_state);
        h = BOR_MAX(h, h1);
    }
    return h;
}

struct maxpot_var {
    int var_id;
    int count;
};
typedef struct maxpot_var maxpot_var_t;

struct maxpot {
    maxpot_var_t *var;
    int var_size;
    int maxpot_id;
    int id;

    bor_htable_key_t hkey;
    bor_list_t htable;
};
typedef struct maxpot maxpot_t;

static bor_htable_key_t maxpotComputeHash(const maxpot_t *m)
{
    return borCityHash_64(m->var, sizeof(maxpot_var_t) * m->var_size);
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
    if (m1->var_size != m2->var_size)
        return 0;
    return memcmp(m1->var, m2->var, sizeof(maxpot_var_t) * m1->var_size) == 0;
}

static pddl_pot_constr_t *addConstr(pddl_pot_constrs_t *cs, int op_id)
{
    if (cs->size >= cs->alloc){
        if (cs->alloc == 0)
            cs->alloc = 4;
        cs->alloc *= 2;
        cs->c = BOR_REALLOC_ARR(cs->c, pddl_pot_constr_t, cs->alloc);
    }

    pddl_pot_constr_t *c = cs->c + cs->size++;
    bzero(c, sizeof(*c));
    c->op_id = op_id;
    return c;
}


static void putBackLastConstr(pddl_pot_constrs_t *cs)
{
    borISetFree(&cs->c[cs->size - 1].plus);
    borISetFree(&cs->c[cs->size - 1].minus);
    --cs->size;
}

static int getMaxpot(pddl_pot_t *pot,
                     const bor_iset_t *set,
                     const int *count)
{
    maxpot_t *m = borSegmArrGet(pot->maxpot, pot->maxpot_size);

    m->var_size = borISetSize(set);
    m->var = BOR_CALLOC_ARR(maxpot_var_t, m->var_size);
    for (int i = 0; i < m->var_size; ++i){
        m->var[i].var_id = borISetGet(set, i);
        if (count != NULL)
            m->var[i].count = count[m->var[i].var_id];
    }
    m->hkey = maxpotComputeHash(m);
    borListInit(&m->htable);

    bor_list_t *found;
    found = borHTableInsertUnique(pot->maxpot_htable, &m->htable);
    if (found == NULL){
        m->id = pot->maxpot_size++;
        m->maxpot_id = pot->var_size++;
        return m->maxpot_id;

    }else{
        if (m->var != NULL)
            BOR_FREE(m->var);

        m = BOR_LIST_ENTRY(found, maxpot_t, htable);
        return m->maxpot_id;
    }
}

static int getFDRMaxpot(pddl_pot_t *pot,
                        int var_id,
                        const pddl_fdr_vars_t *vars)
{
    BOR_ISET(lp_vars);
    for (int val = 0; val < vars->var[var_id].val_size; ++val)
        borISetAdd(&lp_vars, vars->var[var_id].val[val].global_id);
    int lp_var_id = getMaxpot(pot, &lp_vars, NULL);
    borISetFree(&lp_vars);
    return lp_var_id;
}

static void addFDROp(pddl_pot_t *pot,
                     const pddl_fdr_vars_t *vars,
                     const pddl_fdr_op_t *op)
{
    pddl_pot_constr_t *c = addConstr(&pot->constr_op, op->id);

    for (int effi = 0; effi < op->eff.fact_size; ++effi){
        const pddl_fdr_fact_t *eff = op->eff.fact + effi;
        int pre = pddlFDRPartStateGet(&op->pre, eff->var);
        if (pre >= 0){
            borISetAdd(&c->plus, vars->var[eff->var].val[pre].global_id);
        }else{
            borISetAdd(&c->plus, getFDRMaxpot(pot, eff->var, vars));
        }
        borISetAdd(&c->minus, vars->var[eff->var].val[eff->val].global_id);
    }
    c->rhs = op->cost;
}

static void addFDRGoal(pddl_pot_t *pot,
                       const pddl_fdr_vars_t *vars,
                       const pddl_fdr_part_state_t *goal)
{
    pddl_pot_constr_t *c = addConstr(&pot->constr_goal, -1);
    for (int var_id = 0; var_id < vars->var_size; ++var_id){
        int eff = pddlFDRPartStateGet(goal, var_id);
        if (eff >= 0){
            borISetAdd(&c->plus, vars->var[var_id].val[eff].global_id);
        }else{
            borISetAdd(&c->plus, getFDRMaxpot(pot, var_id, vars));
        }
    }
    c->rhs = 0;
}

static void addFDRInit(pddl_pot_t *pot, const pddl_fdr_vars_t *vars, const int *init)
{
    for (int var = 0; var < vars->var_size; ++var)
        borISetAdd(&pot->init, vars->var[var].val[init[var]].global_id);
}

static void hsetToVarSet(pddl_pot_t *pot,
                         const pddl_set_iset_t *hset,
                         bor_iset_t *var_set)
{
    int *count = BOR_CALLOC_ARR(int, pot->var_size);
    const bor_iset_t *set;
    PDDL_SET_ISET_FOR_EACH(hset, set){
        int fact_id;
        BOR_ISET_FOR_EACH(set, fact_id)
            count[fact_id] += 1;
    }

    PDDL_SET_ISET_FOR_EACH(hset, set){
        if (borISetSize(set) == 1){
            int fact_id = borISetGet(set, 0);
            ASSERT(count[fact_id] == 1);
            borISetAdd(var_set, fact_id);
        }else{
            int maxpot_id = getMaxpot(pot, set, count);
            borISetAdd(var_set, maxpot_id);
        }
    }

    if (count != NULL)
        BOR_FREE(count);
}

static void addMGStripsOp(pddl_pot_t *pot,
                          pddl_disambiguate_t *dis,
                          const pddl_strips_op_t *op,
                          int single_fact_dis)
{
    pddl_set_iset_t hset;
    pddlSetISetInit(&hset);

    if (pddlDisambiguate(dis, &op->pre, &op->add_eff, 0,
                         single_fact_dis, &hset, NULL) < 0){
        // Skip unreachable operators
        pddlSetISetFree(&hset);
        return;
    }

    pddl_pot_constr_t *c = addConstr(&pot->constr_op, op->id);
    hsetToVarSet(pot, &hset, &c->plus);
    borISetUnion(&c->minus, &op->add_eff);
    c->rhs = op->cost;

    BOR_ISET(inter);
    borISetIntersect2(&inter, &c->plus, &c->minus);
    borISetMinus(&c->minus, &inter);
    borISetMinus(&c->plus, &inter);
    borISetFree(&inter);

    if (borISetSize(&c->plus) == 0 && borISetSize(&c->minus) == 0)
        putBackLastConstr(&pot->constr_op);

    pddlSetISetFree(&hset);
}

static int addMGStripsGoal(pddl_pot_t *pot,
                           pddl_disambiguate_t *dis,
                           const bor_iset_t *goal,
                           int single_fact_dis)
{
    pddl_set_iset_t hset;
    pddlSetISetInit(&hset);

    if (pddlDisambiguate(dis, goal, NULL, 0, single_fact_dis, &hset, NULL) < 0){
        pddlSetISetFree(&hset);
        return -1;
    }

    pddl_pot_constr_t *c = addConstr(&pot->constr_goal, -1);
    hsetToVarSet(pot, &hset, &c->plus);
    c->rhs = 0;

    pddlSetISetFree(&hset);
    return 0;
}

static void init(pddl_pot_t *pot, int maxpot_segm_size)
{
    bzero(pot, sizeof(*pot));
    pot->constr_lb.set = 0;

    int segm_size = BOR_MAX(maxpot_segm_size, 8) * sizeof(maxpot_t);
    pot->maxpot_size = 0;
    pot->maxpot = borSegmArrNew(sizeof(maxpot_t), segm_size);
    pot->maxpot_htable = borHTableNew(htableHash, htableEq, NULL);
}

void pddlPotInitFDR(pddl_pot_t *pot, const pddl_fdr_t *fdr)
{
    init(pot, fdr->var.var_size);

    pot->var_size = fdr->var.global_id_size;
    pot->fact_var_size = pot->var_size;
    for (int op_id = 0; op_id < fdr->op.op_size; ++op_id)
        addFDROp(pot, &fdr->var, fdr->op.op[op_id]);
    pot->op_size = fdr->op.op_size;

    addFDRGoal(pot, &fdr->var, &fdr->goal);
    addFDRInit(pot, &fdr->var, fdr->init);

    pot->obj = BOR_CALLOC_ARR(double, pot->var_size);
}

static int initMGStrips(pddl_pot_t *pot,
                        const pddl_mg_strips_t *mg_strips,
                        const pddl_mutex_pairs_t *mutex,
                        int single_fact_disamb)
{
    init(pot, mg_strips->mg.mgroup_size);

    pot->var_size = mg_strips->strips.fact.fact_size;
    pot->fact_var_size = pot->var_size;

    pddl_disambiguate_t dis;
    pddlDisambiguateInit(&dis, mg_strips->strips.fact.fact_size,
                         mutex, &mg_strips->mg);

    for (int op_id = 0; op_id < mg_strips->strips.op.op_size; ++op_id){
        addMGStripsOp(pot, &dis, mg_strips->strips.op.op[op_id],
                      single_fact_disamb);
    }
    pot->op_size = mg_strips->strips.op.op_size;
    if (addMGStripsGoal(pot, &dis, &mg_strips->strips.goal,
                        single_fact_disamb) != 0){
        pddlDisambiguateFree(&dis);
        pddlPotFree(pot);
        return -1;
    }
    borISetUnion(&pot->init, &mg_strips->strips.init);

    pot->obj = BOR_CALLOC_ARR(double, pot->var_size);

    pddlDisambiguateFree(&dis);
    return 0;
}

int pddlPotInitMGStrips(pddl_pot_t *pot,
                        const pddl_mg_strips_t *mg_strips,
                        const pddl_mutex_pairs_t *mutex)
{
    return initMGStrips(pot, mg_strips, mutex, 0);
}

int pddlPotInitMGStripsSingleFactDisamb(pddl_pot_t *pot,
                                        const pddl_mg_strips_t *mg_strips,
                                        const pddl_mutex_pairs_t *mutex)
{
    return initMGStrips(pot, mg_strips, mutex, 1);
}

void pddlPotFree(pddl_pot_t *pot)
{
    if (pot->maxpot_htable != NULL)
        borHTableDel(pot->maxpot_htable);
    for (int mi = 0; mi < pot->maxpot_size; ++mi){
        maxpot_t *m = borSegmArrGet(pot->maxpot, mi);
        if (m->var != NULL)
            BOR_FREE(m->var);
    }
    if (pot->maxpot != NULL)
        borSegmArrDel(pot->maxpot);

    for (int i = 0; i < pot->constr_op.size; ++i){
        borISetFree(&pot->constr_op.c[i].plus);
        borISetFree(&pot->constr_op.c[i].minus);
    }
    if (pot->constr_op.c != NULL)
        BOR_FREE(pot->constr_op.c);

    for (int i = 0; i < pot->constr_goal.size; ++i){
        borISetFree(&pot->constr_goal.c[i].plus);
        borISetFree(&pot->constr_goal.c[i].minus);
    }
    if (pot->constr_goal.c != NULL)
        BOR_FREE(pot->constr_goal.c);

    borISetFree(&pot->constr_lb.vars);

    if (pot->obj != NULL)
        BOR_FREE(pot->obj);

    borISetFree(&pot->init);
}

void pddlPotSetObj(pddl_pot_t *pot, const double *coef)
{
    memcpy(pot->obj, coef, sizeof(double) * pot->var_size);
}

void pddlPotSetObjFDRState(pddl_pot_t *pot,
                           const pddl_fdr_vars_t *vars,
                           const int *state)
{
    bzero(pot->obj, sizeof(*pot->obj) * pot->var_size);
    for (int var_id = 0; var_id < vars->var_size; ++var_id)
        pot->obj[vars->var[var_id].val[state[var_id]].global_id] = 1.;
}

void pddlPotSetObjFDRAllSyntacticStates(pddl_pot_t *pot,
                                        const pddl_fdr_vars_t *vars)
{
    bzero(pot->obj, sizeof(*pot->obj) * pot->var_size);
    for (int var_id = 0; var_id < vars->var_size; ++var_id){
        double c = 1. / vars->var[var_id].val_size;
        for (int val = 0; val < vars->var[var_id].val_size; ++val){
            pot->obj[vars->var[var_id].val[val].global_id] = c;
        }
    }
}

void pddlPotSetObjStripsState(pddl_pot_t *pot, const bor_iset_t *state)
{
    bzero(pot->obj, sizeof(*pot->obj) * pot->var_size);
    int fact_id;
    BOR_ISET_FOR_EACH(state, fact_id)
        pot->obj[fact_id] = 1.;
}

void pddlPotSetLowerBoundConstr(pddl_pot_t *pot,
                                const bor_iset_t *vars,
                                double rhs)
{
    pot->constr_lb.set = 1;
    borISetEmpty(&pot->constr_lb.vars);
    borISetUnion(&pot->constr_lb.vars, vars);
    pot->constr_lb.rhs = rhs;
}

void pddlPotResetLowerBoundConstr(pddl_pot_t *pot)
{
    pot->constr_lb.set = 0;
}

double pddlPotSetLowerBoundConstrRHS(const pddl_pot_t *pot)
{
    if (!pot->constr_lb.set)
        return 0.;
    return pot->constr_lb.rhs;
}

void pddlPotDecreaseLowerBoundConstrRHS(pddl_pot_t *pot, double decrease)
{
    if (!pot->constr_lb.set)
        return;
    pot->constr_lb.rhs -= decrease;
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

static void setConstrs(bor_lp_t *lp,
                       const pddl_pot_t *pot,
                       const pddl_pot_constrs_t *cs,
                       int *row)
{
    for (int ci = 0; ci < cs->size; ++ci)
        setConstr(lp, (*row)++, pot, cs->c + ci);
}

static void setMaxpotConstr(bor_lp_t *lp,
                            const pddl_pot_t *pot,
                            const maxpot_t *maxpot,
                            int *prow)
{
    for (int i = 0; i < maxpot->var_size; ++i){
        double coef = 1.;
        if (maxpot->var[i].count > 1)
            coef = 1. / maxpot->var[i].count;
        int row = (*prow)++;
        borLPSetCoef(lp, row, maxpot->var[i].var_id, coef);
        borLPSetCoef(lp, row, maxpot->maxpot_id, -1.);
        borLPSetRHS(lp, row, 0., 'L');
    }
}

static void setMaxpotConstrs(bor_lp_t *lp, const pddl_pot_t *pot, int *row)
{
    for (int mi = 0; mi < pot->maxpot_size; ++mi){
        const maxpot_t *m = borSegmArrGet(pot->maxpot, mi);
        setMaxpotConstr(lp, pot, m, row);
    }
}

static void setLBConstr(bor_lp_t *lp, const pddl_pot_t *pot, int *row)
{
    if (!pot->constr_lb.set)
        return;

    if (*row == borLPNumRows(lp)){
        char sense = 'G';
        borLPAddRows(lp, 1, &pot->constr_lb.rhs, &sense);
    }else{
        borLPSetRHS(lp, *row, pot->constr_lb.rhs, 'G');
    }

    int var;
    BOR_ISET_FOR_EACH(&pot->constr_lb.vars, var)
        borLPSetCoef(lp, *row, var, 1.);
    (*row)++;
}

static void enforceIntInit(bor_lp_t *lp, const pddl_pot_t *pot)
{
    int var = borLPNumCols(lp);
    borLPAddCols(lp, 1);
    borLPSetVarRange(lp, var, -1E20, 1E20);
    borLPSetVarInt(lp, var);
    borLPSetObj(lp, var, 0);

    double rhs = 0;
    char sense = 'E';
    int row = borLPNumRows(lp);
    borLPAddRows(lp, 1, &rhs, &sense);
    int fact;
    BOR_ISET_FOR_EACH(&pot->init, fact)
        borLPSetCoef(lp, row, fact, 1);
    borLPSetCoef(lp, row, var, 1);
}

static int addOpPotConstrs(bor_lp_t *lp, const pddl_pot_t *pot)
{
    int var_size = borLPNumCols(lp);
    borLPAddCols(lp, pot->constr_op.size);
    for (int i = 0; i < pot->constr_op.size; ++i){
        int var = var_size + i;
        //borLPSetVarRange(lp, var, LPVAR_LOWER, 10. * LPVAR_UPPER);
        borLPSetVarRange(lp, var, -1E20, 1E20);
        if (!pot->op_pot_real)
            borLPSetVarInt(lp, var);
        borLPSetObj(lp, var, 0);
    }

    int row = borLPNumRows(lp);
    for (int i = 0; i < pot->constr_op.size; ++i){
        double rhs = 0;
        char sense = 'E';
        borLPAddRows(lp, 1, &rhs, &sense);
        setConstr(lp, row, pot, pot->constr_op.c + i);
        borLPSetRHS(lp, row, rhs, sense);
        borLPSetCoef(lp, row, var_size + i, 1);
        ++row;
    }

    return var_size;
}

static void storeOpPot(bor_lp_t *lp,
                       const double *obj,
                       int var_offset,
                       const pddl_pot_t *pot,
                       pddl_pot_solution_t *sol)
{
    sol->op_pot_size = pot->constr_op.size;
    sol->op_pot = BOR_CALLOC_ARR(double, pot->op_size);
    for (int ci = 0; ci < pot->constr_op.size; ++ci){
        const pddl_pot_constr_t *c = pot->constr_op.c + ci;
        if (c->op_id >= 0){
            double oval = obj[var_offset + ci];
            if (pot->op_pot_real){
                sol->op_pot[c->op_id] = oval;
            }else{
                sol->op_pot[c->op_id] = (int)round(oval);
            }
        }
    }
}

int pddlPotSolve(const pddl_pot_t *pot, pddl_pot_solution_t *sol)
{
    int ret = 0;
    bor_lp_t *lp;

    unsigned lp_flags;
    lp_flags  = BOR_LP_MAX;
    lp_flags |= BOR_LP_NUM_THREADS(1);

    int rows = pot->constr_op.size;
    rows += pot->constr_goal.size;
    for (int mi = 0; mi < pot->maxpot_size; ++mi){
        const maxpot_t *m = borSegmArrGet(pot->maxpot, mi);
        rows += m->var_size;
    }
    lp = borLPNew(rows, pot->var_size, lp_flags);

    for (int i = 0; i < pot->var_size; ++i){
        if (pot->use_ilp)
            borLPSetVarInt(lp, i);
        borLPSetVarRange(lp, i, LPVAR_LOWER, LPVAR_UPPER);
        borLPSetObj(lp, i, pot->obj[i]);
    }

    int row = 0;
    setConstrs(lp, pot, &pot->constr_op, &row);
    setConstrs(lp, pot, &pot->constr_goal, &row);
    setMaxpotConstrs(lp, pot, &row);

    int op_pot_var_offset = 0;
    if (pot->op_pot)
        op_pot_var_offset = addOpPotConstrs(lp, pot);
    if (pot->enforce_int_init)
        enforceIntInit(lp, pot);

    row = borLPNumRows(lp);
    setLBConstr(lp, pot, &row);

    int var_size = borLPNumCols(lp);
    double objval, *obj;
    obj = BOR_CALLOC_ARR(double, var_size);
    if (borLPSolve(lp, &objval, obj) == 0){
        sol->objval = objval;
        sol->pot_size = pot->var_size;
        sol->pot = BOR_ALLOC_ARR(double, sol->pot_size);
        memcpy(sol->pot, obj, sizeof(double) * sol->pot_size);

        if (pot->op_pot)
            storeOpPot(lp, obj, op_pot_var_offset, pot, sol);

    }else{
        bzero(sol, sizeof(*sol));
        ret = -1;
    }

    BOR_FREE(obj);
    borLPDel(lp);

    return ret;
}

double pddlPotFDRStateFlt(const pddl_fdr_t *fdr,
                          const int *state,
                          const pddl_pot_solution_t *sol)
{
    // Use kahan summation
    double sum = 0.;
    double comp = 0.;
    for (int var = 0; var < fdr->var.var_size; ++var){
        double v = sol->pot[fdr->var.var[var].val[state[var]].global_id];
        double y = v - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum = t;
    }
    return sum;
}

int pddlPotFDRState(const pddl_fdr_t *fdr,
                    const int *state,
                    const pddl_pot_solution_t *sol)
{
    return potFltToInt(pddlPotFDRStateFlt(fdr, state, sol));
}

double pddlPotStripsStateFlt(const bor_iset_t *state,
                             const pddl_pot_solution_t *sol)
{
    // Use kahan summation
    double sum = 0.;
    double comp = 0.;
    int fact_id;
    BOR_ISET_FOR_EACH(state, fact_id){
        double v = sol->pot[fact_id];
        double y = v - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum = t;
    }
    return sum;
}

int pddlPotStripsState(const bor_iset_t *state,
                       const pddl_pot_solution_t *sol)
{
    return potFltToInt(pddlPotStripsStateFlt(state, sol));
}

void pddlPotMGStripsPrintLP(const pddl_pot_t *pot,
                            const pddl_mg_strips_t *mg_strips,
                            FILE *fout)
{
    for (int fid = 0; fid < mg_strips->strips.fact.fact_size; ++fid){
        fprintf(fout, "\\Fact[%d] = (%s)\n", fid,
                mg_strips->strips.fact.fact[fid]->name);
    }

    int fact_id;
    fprintf(fout, "\\Init:");
    BOR_ISET_FOR_EACH(&mg_strips->strips.init, fact_id)
        fprintf(fout, " x%d", fact_id);
    fprintf(fout, "\n");

    fprintf(fout, "\\Goal:");
    BOR_ISET_FOR_EACH(&mg_strips->strips.goal, fact_id)
        fprintf(fout, " x%d", fact_id);
    fprintf(fout, "\n");

    for (int mi = 0; mi < mg_strips->mg.mgroup_size; ++mi){
        int fact_id;
        fprintf(fout, "\\MG%d:", mi);
        BOR_ISET_FOR_EACH(&mg_strips->mg.mgroup[mi].mgroup, fact_id)
            fprintf(fout, " x%d", fact_id);
        fprintf(fout, "\n");
    }
    fprintf(fout, "Maximize\n");
    fprintf(fout, "  obj:");
    int first = 1;
    for (int i = 0; i < pot->var_size; ++i){
        if (pot->obj[i] != 0.){
            if (!first)
                fprintf(fout, " +");
            fprintf(fout, " %f x%d", pot->obj[i], i);
            first = 0;
        }
    }
    fprintf(fout, "\n");

    fprintf(fout, "Subject To\n");
    fprintf(fout, "\\Ops:\n");
    for(int ci = 0; ci < pot->constr_op.size; ++ci){
        const pddl_pot_constr_t *c = pot->constr_op.c + ci;
        int var;

        int first = 1;
        BOR_ISET_FOR_EACH(&c->plus, var){
            if (!first)
                fprintf(fout, " +");
            fprintf(fout, " x%d", var);
            first = 0;
        }
        BOR_ISET_FOR_EACH(&c->minus, var)
            fprintf(fout, " - x%d", var);
        fprintf(fout, " <= %d\n", c->rhs);
    }

    fprintf(fout, "\\Goals: (");
    BOR_ISET_FOR_EACH(&mg_strips->strips.goal, fact_id)
        fprintf(fout, " x%d", fact_id);
    fprintf(fout, ")\n");
    for(int ci = 0; ci < pot->constr_goal.size; ++ci){
        const pddl_pot_constr_t *c = pot->constr_goal.c + ci;
        int var;

        int first = 1;
        BOR_ISET_FOR_EACH(&c->plus, var){
            if (!first)
                fprintf(fout, " +");
            fprintf(fout, " x%d", var);
            first = 0;
        }
        BOR_ISET_FOR_EACH(&c->minus, var)
            fprintf(fout, " - x%d", var);
        fprintf(fout, " <= %d\n", c->rhs);
    }

    fprintf(fout, "\\Maxpots:\n");

    for (int mxi = 0; mxi < pot->maxpot_size; ++mxi){
        const maxpot_t *m = borSegmArrGet(pot->maxpot, mxi);
        for (int i = 0; i < m->var_size; ++i){
            double coef = 1.;
            if (m->var[i].count > 1)
                coef = 1. / m->var[i].count;
            fprintf(fout, "%f x%d - x%d <= 0\n",
                    coef, m->var[i].var_id, m->maxpot_id);
        }
    }

    int mvar = pot->var_size;
    for (int mi = 0; mi < mg_strips->mg.mgroup_size; ++mi){
        int fact_id;
        fprintf(fout, "\\M%d:\n", mi);
        BOR_ISET_FOR_EACH(&mg_strips->mg.mgroup[mi].mgroup, fact_id)
            fprintf(fout, "x%d - x%d <= 0\n", fact_id, mvar);
        ++mvar;
    }

    fprintf(fout, "Bounds\n");
    for (int i = 0; i < pot->var_size; ++i)
        fprintf(fout, "-inf <= x%d <= %f\n", i, LPVAR_UPPER);
    fprintf(fout, "End\n");
}
