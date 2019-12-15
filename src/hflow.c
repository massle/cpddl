/***
 * cpddl
 * -------
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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
#include "pddl/hflow.h"


#define BOUND_INF 1E30
#define ROUND_EPS 1E-6

/** Upper bound table:
 * [is_goal_var][is_mutex_with_goal][is_init][cause_incomplete_op] */
static double upper_bound_table[2][2][2][2] = {
// .is_goal_var                      0
// .is_mutex_with_goal               0
// .is_init:                 0                  1
// .cause_incomplete_op   0     1            0     1
                    { { { 1., BOUND_INF }, { 0., BOUND_INF } },

// .is_goal_var                      0
// .is_mutex_with_goal               1
// .is_init:                 0                  1
// .cause_incomplete_op   0     1            0     1
                      { { 0., BOUND_INF }, { -1., BOUND_INF } } },

// .is_goal_var                      1
// .is_mutex_with_goal               0
// .is_init:                 0                  1
// .cause_incomplete_op   0     1            0     1
                    { { { 1., BOUND_INF }, { 0., BOUND_INF } },

// .is_goal_var                      1
// .is_mutex_with_goal               1
// .is_init:                 0                  1
// .cause_incomplete_op   0     1            0     1
                      { { 0., BOUND_INF }, { -1., BOUND_INF } } },
};

/** Lower bound table:
 * [is_goal_var][is_mutex_with_goal][is_init] */
static double lower_bound_table[2][2][2] = {
// .is_goal_var                  0
// .is_mutex_with_goal    0      |      1
// .is_init:            0    1   |   0    1
                    { { 0., -1. }, { 0., -1. } },

// .is_goal_var                  1
// .is_mutex_with_goal    0      |      1
// .is_init:            0    1   |   0    1
                    { { 1., 0. }, { 0., -1. } },
};

/** Set up bounds given the state */
static void factsSetState(pddl_hflow_fact_t *facts,
                          const pddl_fdr_vars_t *vars,
                          const int *state);
/** Initialize array of facts */
static void factsInit(pddl_hflow_fact_t *facts,
                      const pddl_fdr_vars_t *var,
                      const pddl_fdr_part_state_t *goal,
                      const pddl_fdr_ops_t *op);
/** Initialize LP solver */
static bor_lp_t *lpInit(const pddl_hflow_fact_t *facts, int facts_size,
                        const pddl_fdr_ops_t *op,
                        int use_ilp,
                        int num_threads);
/** Solve the problem */
static int lpSolve(bor_lp_t *lp,
                   const pddl_hflow_fact_t *facts, int facts_size,
                   int use_ilp,
                   const pddl_set_iset_t *ldms);

void pddlHFlowInit(pddl_hflow_t *h,
                   const pddl_fdr_t *fdr,
                   int use_ilp)
{
    bzero(h, sizeof(*h));
    h->fdr = fdr;
    h->vars = &fdr->var;
    h->use_ilp = use_ilp;

    h->facts = BOR_CALLOC_ARR(pddl_hflow_fact_t, h->vars->global_id_size);
    factsInit(h->facts, &fdr->var, &fdr->goal, &fdr->op);

    h->lp = lpInit(h->facts, h->vars->global_id_size, &fdr->op, use_ilp, 1);
}

void pddlHFlowFree(pddl_hflow_t *h)
{
    for (int i = 0; i < h->vars->global_id_size; ++i){
        if (h->facts[i].constr_idx != NULL)
            BOR_FREE(h->facts[i].constr_idx);
        if (h->facts[i].constr_coef != NULL)
            BOR_FREE(h->facts[i].constr_coef);
    }
    if (h->facts != NULL)
        BOR_FREE(h->facts);
    borLPDel(h->lp);
}

int pddlHFlow(pddl_hflow_t *h,
              const int *fdr_state,
              const pddl_set_iset_t *ldms)
{
    int hval;
    factsSetState(h->facts, h->vars, fdr_state);
    hval = lpSolve(h->lp, h->facts, h->vars->global_id_size, h->use_ilp, ldms);
    return hval;
}

static void factsSetState(pddl_hflow_fact_t *facts,
                          const pddl_fdr_vars_t *vars,
                          const int *state)
{
    // First set .is_init flag
    for (int i = 0; i < vars->global_id_size; ++i)
        facts[i].is_init = 0;
    for (int i = 0; i < vars->var_size; ++i){
        int fid = vars->var[i].val[state[i]].global_id;
        if (fid >= 0)
            facts[fid].is_init = 1;
    }

    // Now set upper and lower bounds
    for (int i = 0; i < vars->global_id_size; ++i){
        facts[i].lower_bound = lower_bound_table[facts[i].is_goal_var]
                                                [facts[i].is_mutex_with_goal]
                                                [facts[i].is_init];
        facts[i].upper_bound = upper_bound_table[facts[i].is_goal_var]
                                                [facts[i].is_mutex_with_goal]
                                                [facts[i].is_init]
                                                [facts[i].cause_incomplete_op];
    }
}

static void factsInitGoal(pddl_hflow_fact_t *facts,
                          const pddl_fdr_part_state_t *goal,
                          const pddl_fdr_vars_t *vars)
{
    for (int i = 0; i < goal->fact_size; ++i){
        int var = goal->fact[i].var;
        int val = goal->fact[i].val;
        int fid = vars->var[var].val[val].global_id;
        if (fid >= 0)
            facts[fid].is_goal = 1;

        // Set .is_goal_var and .is_mutex_with_goal flag
        for (int val2 = 0; val2 < vars->var[var].val_size; ++val2){
            int fid = vars->var[var].val[val2].global_id;
            if (fid >= 0){
                facts[fid].is_goal_var = 1;
                if (val2 != val)
                    facts[fid].is_mutex_with_goal = 1;
            }
        }
    }

    // TODO: Check mutexes
}

static void factAddConstr(pddl_hflow_fact_t *fact, int op_id, double coef)
{
    ++fact->constr_len;
    fact->constr_idx = BOR_REALLOC_ARR(fact->constr_idx, int,
                                       fact->constr_len);
    fact->constr_coef = BOR_REALLOC_ARR(fact->constr_coef, double,
                                        fact->constr_len);
    fact->constr_idx[fact->constr_len - 1] = op_id;
    fact->constr_coef[fact->constr_len - 1] = coef;
}

static void factAddProduce(pddl_hflow_fact_t *fact, int op_id)
{
    factAddConstr(fact, op_id, 1.);
}

static void factAddConsume(pddl_hflow_fact_t *fact, int op_id)
{
    factAddConstr(fact, op_id, -1.);
}

static void factsInitOp(pddl_hflow_fact_t *facts,
                        const pddl_fdr_vars_t *vars,
                        const pddl_fdr_op_t *op, int op_id,
                        int *cause_incomplete_op)
{
    int prei, effi;

    const pddl_fdr_part_state_t *pre = &op->pre;
    const pddl_fdr_part_state_t *eff = &op->eff;
    for (prei = 0, effi = 0; prei < pre->fact_size && effi < eff->fact_size;){
        int pre_var = pre->fact[prei].var;
        int eff_var = eff->fact[effi].var;
        if (pre_var == eff_var){
            // pre_val -> eff_val

            // The operator produces the eff_val
            int eff_val = eff->fact[effi].val;
            int fid = vars->var[eff_var].val[eff_val].global_id;
            factAddProduce(facts + fid, op_id);

            // and consumes the pre_val
            int pre_val = pre->fact[prei].val;
            fid = vars->var[pre_var].val[pre_val].global_id;
            factAddConsume(facts + fid, op_id);

            ++prei;
            ++effi;

        }else if (pre_var < eff_var){
            // This is just prevail -- which can be ignored
            ++prei;
            continue;

        }else{ // eff_var < pre_var
            // (null) -> eff_val

            // The eff_val is produced
            int eff_val = eff->fact[effi].val;
            int fid = vars->var[eff_var].val[eff_val].global_id;
            factAddProduce(facts + fid, op_id);

            // Also set the fact as causing incompletness because this
            // operator only produces and does not consume.
            cause_incomplete_op[facts[fid].var] = 1;

            ++effi;
        }
    }

    // Process the rest of produced values the same way as in
    // (eff_var < pre_var) branch above.
    for (; effi < eff->fact_size; ++effi){
        int eff_var = eff->fact[effi].var;
        int eff_val = eff->fact[effi].val;
        int fid = vars->var[eff_var].val[eff_val].global_id;
        factAddProduce(facts + fid, op_id);
        cause_incomplete_op[facts[fid].var] = 1;
    }
}

static void factsInitOps(pddl_hflow_fact_t *facts,
                         const pddl_fdr_vars_t *vars,
                         const pddl_fdr_ops_t *ops)
{
    int *cause_incomplete_op;

    cause_incomplete_op = BOR_CALLOC_ARR(int, vars->var_size);
    for (int opi = 0; opi < ops->op_size; ++opi)
        factsInitOp(facts, vars, ops->op[opi], opi, cause_incomplete_op);

    for (int i = 0; i < vars->global_id_size; ++i){
        if (cause_incomplete_op[facts[i].var])
            facts[i].cause_incomplete_op = 1;
    }
    BOR_FREE(cause_incomplete_op);
}

static void factsInit(pddl_hflow_fact_t *facts,
                      const pddl_fdr_vars_t *vars,
                      const pddl_fdr_part_state_t *goal,
                      const pddl_fdr_ops_t *op)
{
    for (int var_id = 0; var_id < vars->var_size; ++var_id){
        const pddl_fdr_var_t *var = vars->var + var_id;
        for (int val_id = 0; val_id < var->val_size; ++val_id){
            const pddl_fdr_val_t *val = var->val + val_id;
            facts[val->global_id].var = var_id;
        }
    }

    factsInitGoal(facts, goal, vars);
    factsInitOps(facts, vars, op);
}


static bor_lp_t *lpInit(const pddl_hflow_fact_t *facts, int facts_size,
                        const pddl_fdr_ops_t *op,
                        int use_ilp,
                        int num_threads)
{
    bor_lp_t *lp;
    unsigned lp_flags;

    lp_flags  = BOR_LP_MIN;
    lp_flags |= BOR_LP_NUM_THREADS(num_threads);
    lp = borLPNew(2 * facts_size, op->op_size, lp_flags);

    // Set up columns
    for (int i = 0; i < op->op_size; ++i){
        if (use_ilp)
            borLPSetVarInt(lp, i);
        borLPSetVarRange(lp, i, 0., INT_MAX);
        borLPSetObj(lp, i, op->op[i]->cost);
    }

    // Set up rows
    for (int r = 0; r < facts_size; ++r){
        for (int i = 0; i < facts[r].constr_len; ++i){
            int c = facts[r].constr_idx[i];
            double coef = facts[r].constr_coef[i];
            borLPSetCoef(lp, 2 * r, c, coef);
            borLPSetCoef(lp, 2 * r + 1, c, coef);
        }
    }

    return lp;
}

static void lpAddLandmarks(bor_lp_t *lp, const pddl_set_iset_t *ldms)
{
    if (ldms == NULL || pddlSetISetSize(ldms) == 0)
        return;

    int ldm_size = pddlSetISetSize(ldms);
    int row_id = borLPNumRows(lp);
    double *rhs = BOR_ALLOC_ARR(double, ldm_size);
    char *sense = BOR_ALLOC_ARR(char, ldm_size);
    for (int i = 0; i < ldm_size; ++i){
        rhs[i] = 1.;
        sense[i] = 'G';
    }
    borLPAddRows(lp, ldm_size, rhs, sense);

    for (int i = 0; i < ldm_size; ++i){
        int op_id;
        const bor_iset_t *ldm = pddlSetISetGet(ldms, i);
        BOR_ISET_FOR_EACH(ldm, op_id)
            borLPSetCoef(lp, row_id, op_id, 1.);
        ++row_id;
    }

    BOR_FREE(rhs);
    BOR_FREE(sense);
}

static void lpDelLandmarks(bor_lp_t *lp, const pddl_set_iset_t *ldms)
{
    int from, to;

    if (ldms == NULL || pddlSetISetSize(ldms) == 0)
        return;

    to = borLPNumRows(lp) - 1;
    from = borLPNumRows(lp) - pddlSetISetSize(ldms);
    borLPDelRows(lp, from, to);
}

static int roundOff(double z)
{
    int v = z;
    if (fabs(z - (double)v) > ROUND_EPS)
        return ceil(z);
    return v;
}

static int lpSolve(bor_lp_t *lp,
                   const pddl_hflow_fact_t *facts, int facts_size,
                   int use_ilp,
                   const pddl_set_iset_t *ldms)
{
    int h = PDDL_COST_DEAD_END;

    // Add row for each fact
    for (int i = 0; i < facts_size; ++i){
        double upper, lower;
        lower = facts[i].lower_bound;
        upper = facts[i].upper_bound;

        if (lower == upper){
            borLPSetRHS(lp, 2 * i, lower, 'E');
            borLPSetRHS(lp, 2 * i + 1, upper, 'E');

        }else{
            borLPSetRHS(lp, 2 * i, lower, 'G');
            borLPSetRHS(lp, 2 * i + 1, upper, 'L');
        }
    }

    // Add landmarks if provided
    lpAddLandmarks(lp, ldms);

    double z;
    if (borLPSolve(lp, &z, NULL) == 0){
        h = roundOff(z);
    }else{
        h = PDDL_COST_DEAD_END;
    }

    lpDelLandmarks(lp, ldms);
    return h;
}
