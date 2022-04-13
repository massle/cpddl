/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
 * AIC, Department of Computer Science,
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
#include "pddl/hmax.h"

#define FID(heur, f) ((f) - (heur)->fact)
#define FVALUE(fact) (fact)->heap.key
#define FVALUE_SET(fact, val) do { (fact)->heap.key = val; } while(0)
#define FVALUE_INIT(fact) FVALUE_SET((fact), INT_MAX)
#define FVALUE_IS_SET(fact) (FVALUE(fact) != INT_MAX)

#define FPUSH(pq, value, fact) \
    do { \
    if (FVALUE_IS_SET(fact)){ \
        pddlPQUpdate((pq), (value), &(fact)->heap); \
    }else{ \
        pddlPQPush((pq), (value), &(fact)->heap); \
    } \
    } while (0)

void pddlHMaxInit(pddl_hmax_t *h, const pddl_fdr_t *fdr)
{
    bzero(h, sizeof(*h));

    // Allocate facts and add one for empty-precondition fact and one for
    // goal fact
    h->fact_size = fdr->var.global_id_size + 2;
    h->fact = CALLOC_ARR(pddl_hmax_fact_t, h->fact_size);
    h->fact_goal = h->fact_size - 2;
    h->fact_nopre = h->fact_size - 1;

    // Allocate operators and add one artificial for goal
    h->op_size = fdr->op.op_size + 1;
    h->op = CALLOC_ARR(pddl_hmax_op_t, h->op_size);
    h->op_goal = h->op_size - 1;

    PDDL_ISET(pre);
    for (int op_id = 0; op_id < fdr->op.op_size; ++op_id){
        const pddl_fdr_op_t *src = fdr->op.op[op_id];
        pddl_hmax_op_t *op = h->op + op_id;

        pddlFDRPartStateToGlobalIDs(&src->eff, &fdr->var, &op->eff);
        op->cost = src->cost;

        pddlISetEmpty(&pre);
        pddlFDRPartStateToGlobalIDs(&src->pre, &fdr->var, &pre);
        int fact;
        PDDL_ISET_FOR_EACH(&pre, fact)
            pddlISetAdd(&h->fact[fact].pre_op, op_id);
        op->pre_size = pddlISetSize(&pre);

        // Record operator with no preconditions
        if (op->pre_size == 0){
            pddlISetAdd(&h->fact[h->fact_nopre].pre_op, op_id);
            op->pre_size = 1;
        }
    }

    // Set up goal operator
    pddl_hmax_op_t *op = h->op + h->op_goal;
    pddlISetAdd(&op->eff, h->fact_goal);
    op->cost = 0;

    pddlISetEmpty(&pre);
    pddlFDRPartStateToGlobalIDs(&fdr->goal, &fdr->var, &pre);
    int fact;
    PDDL_ISET_FOR_EACH(&pre, fact)
        pddlISetAdd(&h->fact[fact].pre_op, h->op_goal);
    op->pre_size = pddlISetSize(&pre);

    pddlISetFree(&pre);
}

void pddlHMaxFree(pddl_hmax_t *hmax)
{
    for (int i = 0; i < hmax->fact_size; ++i)
        pddlISetFree(&hmax->fact[i].pre_op);
    if (hmax->fact != NULL)
        FREE(hmax->fact);

    for (int i = 0; i < hmax->op_size; ++i)
        pddlISetFree(&hmax->op[i].eff);
    if (hmax->op != NULL)
        FREE(hmax->op);
}

static void initFacts(pddl_hmax_t *h)
{
    int i;

    for (i = 0; i < h->fact_size; ++i){
        FVALUE_INIT(h->fact + i);
    }
}

static void initOps(pddl_hmax_t *h)
{
    int i;

    for (i = 0; i < h->op_size; ++i){
        h->op[i].unsat = h->op[i].pre_size;
    }
}

static void addInitState(pddl_hmax_t *h,
                         const int *fdr_state,
                         const pddl_fdr_vars_t *vars,
                         pddl_pq_t *pq)
{
    for (int var = 0; var < vars->var_size; ++var){
        int fact_id = vars->var[var].val[fdr_state[var]].global_id;
        FPUSH(pq, 0, h->fact + fact_id);
    }
    FPUSH(pq, 0, h->fact + h->fact_nopre);
}

static void enqueueOpEffects(pddl_hmax_t *h,
                             pddl_hmax_op_t *op,
                             int fact_value,
                             pddl_pq_t *pq)
{
    int value = op->cost + fact_value;
    int fid;

    PDDL_ISET_FOR_EACH(&op->eff, fid){
        pddl_hmax_fact_t *fact = h->fact + fid;
        if (FVALUE(fact) > value)
            FPUSH(pq, value, fact);
    }
}

int pddlHMax(pddl_hmax_t *h,
             const int *fdr_state,
             const pddl_fdr_vars_t *vars)
{
    pddl_pq_t pq;

    pddlPQInit(&pq);
    initFacts(h);
    initOps(h);
    addInitState(h, fdr_state, vars, &pq);
    while (!pddlPQEmpty(&pq)){
        int value;
        pddl_pq_el_t *el = pddlPQPop(&pq, &value);
        pddl_hmax_fact_t *fact = pddl_container_of(el, pddl_hmax_fact_t, heap);

        int fact_id = FID(h, fact);
        if (fact_id == h->fact_goal)
            break;

        int op_id;
        PDDL_ISET_FOR_EACH(&fact->pre_op, op_id){
            pddl_hmax_op_t *op = h->op + op_id;
            if (--op->unsat == 0)
                enqueueOpEffects(h, op, value, &pq);
        }
    }
    pddlPQFree(&pq);

    int heur = PDDL_COST_DEAD_END;
    if (FVALUE_IS_SET(h->fact + h->fact_goal))
        heur = FVALUE(h->fact + h->fact_goal);
    return heur;
}
