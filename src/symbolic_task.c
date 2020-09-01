/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/config.h"

#ifdef PDDL_CUDD

#include <stdio.h>
#include <cudd/cudd.h>
#include <boruvka/alloc.h>
#include <boruvka/sort.h>
#include <boruvka/extarr.h>

#include "pddl/symbolic_task.h"
#include "pddl/time_limit.h"
#include "assert.h"

struct pddl_symbolic_trans {
    DdNode *bdd; /*!< BDD representing the transition(s) */
    bor_iset_t eff_facts; /*!< Facts appearing in the effect(s) */
    DdNode **var_pre; /*!< List of pre variables */
    DdNode **var_eff; /*!< List of eff variables */
    int var_size; /*!< Size of .var_pre and .var_eff */
    DdNode *exist_pre; /*!< Cube from .var_pre */
    DdNode *exist_eff; /*!< Cube from .var_eff */
};
typedef struct pddl_symbolic_trans pddl_symbolic_trans_t;

struct pddl_symbolic_trans_set {
    pddl_symbolic_trans_t *trans;
    int trans_size;

    bor_iset_t op; /*!< List of covered operators */
    int cost; /*!< Cost of the covered operatros */
};
typedef struct pddl_symbolic_trans_set pddl_symbolic_trans_set_t;

struct pddl_symbolic_trans_sets {
    pddl_symbolic_trans_set_t *trans;
    int trans_size;
};
typedef struct pddl_symbolic_trans_sets pddl_symbolic_trans_sets_t;

struct pddl_symbolic_state {
    int id;
    int parent_id;
    int cost; /*!< g value */
    int zero_cost; /*!< Number of zero cost operators on the path */
    DdNode *bdd;
    int is_closed;
};
typedef struct pddl_symbolic_state pddl_symbolic_state_t;

struct pddl_symbolic_states {
    bor_extarr_t *pool; /*!< Data pool */
    int num_states;
    DdNode *all_closed;
};
typedef struct pddl_symbolic_states pddl_symbolic_states_t;

struct pddl_symbolic_task {
    DdManager *ddm; /*!< Cudd manager */
    int fact_size;
    int *ordered_facts;
    int *fact_to_order;
    int *pre_fact_to_var;
    int *eff_fact_to_var;
    int num_vars;
    pddl_symbolic_trans_sets_t trans;
    int zero_cost_trans;

    DdNode *init;
    DdNode *goal;
    pddl_symbolic_states_t fw_state;

    pddl_symbolic_states_t states;
};

// X = X and Y
#define CUDD_AND(DDM, X, Y) \
    do { \
        DdNode *___res = Cudd_bddAnd((DDM), (X), (Y)); \
        Cudd_Ref(___res); \
        Cudd_RecursiveDeref((DDM), (X)); \
        (X) = ___res; \
    } while (0)

// X = X or Y
#define CUDD_OR(DDM, X, Y) \
    do { \
        DdNode *___res = Cudd_bddOr((DDM), (X), (Y)); \
        Cudd_Ref(___res); \
        Cudd_RecursiveDeref((DDM), (X)); \
        (X) = ___res; \
    } while (0)


static DdNode *createState(pddl_symbolic_task_t *ss, const bor_iset_t *state)
{
    DdNode *bdd = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(bdd);
    for (int i = 0; i < ss->fact_size; ++i){
        int fact_id = ss->ordered_facts[i];
        int var_id = ss->pre_fact_to_var[fact_id];
        DdNode *var = Cudd_bddIthVar(ss->ddm, var_id);
        if (!borISetIn(fact_id, state))
            var = Cudd_Not(var);
        CUDD_AND(ss->ddm, bdd, var);
    }
    return bdd;
}

static DdNode *createPartialState(pddl_symbolic_task_t *ss,
                                  const bor_iset_t *part_state)
{
    DdNode *bdd = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(bdd);
    for (int i = 0; i < ss->fact_size; ++i){
        int fact_id = ss->ordered_facts[i];
        if (!borISetIn(fact_id, part_state))
            continue;

        int var_id = ss->pre_fact_to_var[fact_id];
        DdNode *var = Cudd_bddIthVar(ss->ddm, var_id);
        CUDD_AND(ss->ddm, bdd, var);
    }
    return bdd;
}

static DdNode *createBiimp(pddl_symbolic_task_t *ss, int var1, int var2)
{
    DdNode *bdd = Cudd_bddXnor(ss->ddm,
                               Cudd_bddIthVar(ss->ddm, var1),
                               Cudd_bddIthVar(ss->ddm, var2));
    Cudd_Ref(bdd);
    return bdd;
}

static DdNode *createBiimpFact(pddl_symbolic_task_t *ss, int fact_id)
{
    return createBiimp(ss, ss->pre_fact_to_var[fact_id],
                           ss->eff_fact_to_var[fact_id]);
}

static void transFree(pddl_symbolic_task_t *ss,
                      pddl_symbolic_trans_t *tr)
{
    //if (tr->bdd == NULL)
    //    return;
    Cudd_RecursiveDeref(ss->ddm, tr->bdd);
    for (int i = 0; i < tr->var_size; ++i){
        Cudd_RecursiveDeref(ss->ddm, tr->var_pre[i]);
        Cudd_RecursiveDeref(ss->ddm, tr->var_eff[i]);
    }
    if (tr->var_pre != NULL)
        BOR_FREE(tr->var_pre);
    if (tr->var_eff != NULL)
        BOR_FREE(tr->var_eff);
    Cudd_RecursiveDeref(ss->ddm, tr->exist_pre);
    Cudd_RecursiveDeref(ss->ddm, tr->exist_eff);
    borISetFree(&tr->eff_facts);
    bzero(tr, sizeof(*tr));
}

static void transSetFree(pddl_symbolic_task_t *ss,
                         pddl_symbolic_trans_set_t *trset)
{
    for (int i = 0; i < trset->trans_size; ++i)
        transFree(ss, trset->trans + i);
    if (trset->trans != NULL)
        BOR_FREE(trset->trans);
    borISetFree(&trset->op);
}

static void transSetsFree(pddl_symbolic_task_t *ss,
                          pddl_symbolic_trans_sets_t *trset)
{
    for (int i = 0; i < trset->trans_size; ++i)
        transSetFree(ss, trset->trans + i);
    if (trset->trans != NULL)
        BOR_FREE(trset->trans);
}

static void transInitEffVars(pddl_symbolic_task_t *ss,
                             pddl_symbolic_trans_t *tr)
{
    tr->var_size = borISetSize(&tr->eff_facts);
    tr->var_pre = BOR_CALLOC_ARR(DdNode *, tr->var_size);
    tr->var_eff = BOR_CALLOC_ARR(DdNode *, tr->var_size);
    int ins = 0;
    int fact_id;
    BOR_ISET_FOR_EACH(&tr->eff_facts, fact_id){
        int var_pre = ss->pre_fact_to_var[fact_id];
        int var_eff = ss->eff_fact_to_var[fact_id];
        DdNode *vpre = Cudd_bddIthVar(ss->ddm, var_pre);
        Cudd_Ref(vpre);
        DdNode *veff = Cudd_bddIthVar(ss->ddm, var_eff);
        Cudd_Ref(veff);
        tr->var_pre[ins] = vpre;
        tr->var_eff[ins] = veff;
        ++ins;
    }

    tr->exist_pre = Cudd_bddComputeCube(ss->ddm, tr->var_pre,
                                        NULL, tr->var_size);
    Cudd_Ref(tr->exist_pre);
    tr->exist_eff = Cudd_bddComputeCube(ss->ddm, tr->var_eff,
                                        NULL, tr->var_size);
    Cudd_Ref(tr->exist_eff);

    /*
    tr->exist_pre = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(tr->exist_pre);
    tr->exist_eff = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(tr->exist_eff);
    for (int i = 0; i < tr->var_size; ++i){
        CUDD_AND(ss->ddm, tr->exist_pre, tr->var_pre[i]);
        CUDD_AND(ss->ddm, tr->exist_eff, tr->var_eff[i]);
    }
    */
}

static void transInit(pddl_symbolic_task_t *ss,
                      const pddl_strips_op_t *op,
                      pddl_symbolic_trans_t *tr)
{
    tr->bdd = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(tr->bdd);

    int fact_id;
    BOR_ISET_FOR_EACH(&op->pre, fact_id){
        int var_id = ss->pre_fact_to_var[fact_id];
        CUDD_AND(ss->ddm, tr->bdd, Cudd_bddIthVar(ss->ddm, var_id));
    }

    BOR_ISET_FOR_EACH(&op->del_eff, fact_id){
        int var_id = ss->eff_fact_to_var[fact_id];
        CUDD_AND(ss->ddm, tr->bdd, Cudd_Not(Cudd_bddIthVar(ss->ddm, var_id)));
    }

    BOR_ISET_FOR_EACH(&op->add_eff, fact_id){
        int var_id = ss->eff_fact_to_var[fact_id];
        CUDD_AND(ss->ddm, tr->bdd, Cudd_bddIthVar(ss->ddm, var_id));
    }

    borISetUnion2(&tr->eff_facts, &op->add_eff, &op->del_eff);
    transInitEffVars(ss, tr);
}

static int transMerge(pddl_symbolic_task_t *ss,
                      pddl_symbolic_trans_t *dst,
                      pddl_symbolic_trans_t *tr1,
                      pddl_symbolic_trans_t *tr2,
                      size_t max_nodes)
{
    bzero(dst, sizeof(*dst));

    DdNode *bdd1 = tr1->bdd;
    Cudd_Ref(bdd1);
    DdNode *bdd2 = tr2->bdd;
    Cudd_Ref(bdd2);

    borISetUnion2(&dst->eff_facts, &tr1->eff_facts, &tr2->eff_facts);
    int e1 = 0, esize1 = borISetSize(&tr1->eff_facts);
    int e2 = 0, esize2 = borISetSize(&tr2->eff_facts);
    int fact_id;
    BOR_ISET_FOR_EACH(&dst->eff_facts, fact_id){
        if (e1 < esize1 && borISetGet(&tr1->eff_facts, e1) == fact_id){
            ++e1;
        }else{
            DdNode *biimp = createBiimpFact(ss, fact_id);
            CUDD_AND(ss->ddm, bdd1, biimp);
            Cudd_RecursiveDeref(ss->ddm, biimp);
        }

        if (e2 < esize2 && borISetGet(&tr2->eff_facts, e2) == fact_id){
            ++e2;
        }else{
            DdNode *biimp = createBiimpFact(ss, fact_id);
            CUDD_AND(ss->ddm, bdd2, biimp);
            Cudd_RecursiveDeref(ss->ddm, biimp);
        }
    }

    if (max_nodes > 0){
        dst->bdd = Cudd_bddOrLimit(ss->ddm, bdd1, bdd2, max_nodes);
        if (dst->bdd != NULL)
            Cudd_Ref(dst->bdd);
    }else{
        dst->bdd = Cudd_bddOr(ss->ddm, bdd1, bdd2);
        Cudd_Ref(dst->bdd);
    }
    Cudd_RecursiveDeref(ss->ddm, bdd1);
    Cudd_RecursiveDeref(ss->ddm, bdd2);
    if (dst->bdd == NULL){
        borISetFree(&dst->eff_facts);
        return -1;
    }

    transInitEffVars(ss, dst);

    return 0;
}

static void transSetsAddRange(pddl_symbolic_task_t *ss,
                              const pddl_symbolic_task_config_t *cfg,
                              const pddl_strips_t *strips,
                              pddl_symbolic_trans_set_t *trset,
                              const int *op_ids,
                              int op_ids_size,
                              bor_err_t *err)
{
    bzero(trset, sizeof(*trset));
    for (int i = 0; i < op_ids_size; ++i)
        borISetAdd(&trset->op, op_ids[i]);
    trset->cost = strips->op.op[op_ids[0]]->cost;

    int T_size = borISetSize(&trset->op);
    pddl_symbolic_trans_t *T = BOR_CALLOC_ARR(pddl_symbolic_trans_t, T_size);
    int Tres_size = 0;
    pddl_symbolic_trans_t *Tres = BOR_CALLOC_ARR(pddl_symbolic_trans_t, T_size);
    for (int i = 0; i < T_size; ++i)
        transInit(ss, strips->op.op[op_ids[i]], T + i);

    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    pddlTimeLimitSet(&time_limit, cfg->trans_merge_max_time);
    while (T_size > 1){
        if (pddlTimeLimitCheck(&time_limit) < 0)
            break;

        int ins = 0;
        for (int i = 0; i < T_size; i = i + 2){
            if (i + 1 >= T_size){
                T[ins] = T[i];

            }else{
                if (T[i].bdd == NULL && T[i + 1].bdd == NULL){
                    bzero(T + ins, sizeof(*T));

                }else if (T[i].bdd == NULL){
                    T[ins] = T[i + 1];

                }else if (T[i + 1].bdd == NULL){
                    T[ins] = T[i];

                }else{
                    pddl_symbolic_trans_t restr;
                    int res = transMerge(ss, &restr, T + i, T + i + 1,
                                         cfg->trans_merge_max_nodes);
                    if (res < 0){
                        Tres[Tres_size++] = T[i];
                        Tres[Tres_size++] = T[i + 1];
                        bzero(T + ins, sizeof(*T));

                    }else{
                        transFree(ss, T + i);
                        transFree(ss, T + i + 1);
                        T[ins] = restr;
                    }
                }
            }
            ++ins;
        }
        T_size = ins;
    }

    for (int i = 0; i < T_size; ++i)
        Tres[Tres_size++] = T[i];

    trset->trans_size = Tres_size;
    trset->trans = BOR_CALLOC_ARR(pddl_symbolic_trans_t, trset->trans_size);
    memcpy(trset->trans, Tres, sizeof(pddl_symbolic_trans_t) * Tres_size);

    BOR_FREE(T);
    BOR_FREE(Tres);

    BOR_INFO(err, "symbolic: created trans BDDs: cost: %d, ops: %d, bdds: %d"
                  " %s",
             trset->cost, borISetSize(&trset->op), trset->trans_size,
             (T_size > 1 ? "(time limit reached)" : ""));
}

static int opIdCostCmp(const void *a, const void *b, void *_strips)
{
    const int id1 = *(const int *)a;
    const int id2 = *(const int *)b;
    const pddl_strips_t *strips = _strips;
    int cmp = strips->op.op[id1]->cost - strips->op.op[id2]->cost;
    if (cmp == 0)
        return id1 - id2;
    return cmp;
}

static void transSetsInit(pddl_symbolic_task_t *ss,
                          const pddl_symbolic_task_config_t *cfg,
                          const pddl_strips_t *strips,
                          pddl_symbolic_trans_sets_t *trset,
                          bor_err_t *err)
{
    bzero(trset, sizeof(*trset));

    BOR_ISET(costs);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id)
        borISetAdd(&costs, strips->op.op[op_id]->cost);

    trset->trans_size = borISetSize(&costs);
    trset->trans = BOR_CALLOC_ARR(pddl_symbolic_trans_set_t, trset->trans_size);
    borISetFree(&costs);

    int *op_ids = BOR_ALLOC_ARR(int, strips->op.op_size);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id)
        op_ids[op_id] = op_id;
    borSort(op_ids, strips->op.op_size, sizeof(int),
            opIdCostCmp, (void *)strips);

    int start = 0, end = 1, tr_id = 0;
    for (end = 1; end < strips->op.op_size; ++end){
        int cost_start = strips->op.op[op_ids[start]]->cost;
        int cost_end = strips->op.op[op_ids[end]]->cost;
        if (cost_start != cost_end){
            ASSERT(end > start);
            ASSERT(tr_id < trset->trans_size);
            transSetsAddRange(ss, cfg, strips, trset->trans + tr_id,
                              op_ids + start, end - start, err);
            ++tr_id;
            start = end;
        }
    }
    if (end > start){
        transSetsAddRange(ss, cfg, strips, trset->trans + tr_id,
                          op_ids + start, end - start, err);
        ++tr_id;
    }
    ASSERT(trset->trans_size == tr_id);

    BOR_FREE(op_ids);
}


static DdNode *transImage(pddl_symbolic_task_t *ss,
                          pddl_symbolic_trans_t *tr,
                          DdNode *state)
{
    DdNode *bdd;
    bdd = Cudd_bddAndAbstract(ss->ddm, state, tr->bdd, tr->exist_pre);
    bdd = Cudd_bddSwapVariables(ss->ddm, bdd,
                                tr->var_pre, tr->var_eff, tr->var_size);
    Cudd_Ref(bdd);
    return bdd;
}

static DdNode *transPreImage(pddl_symbolic_task_t *ss,
                          pddl_symbolic_trans_t *tr,
                          DdNode *state)
{
    DdNode *bdd;
    bdd = Cudd_bddSwapVariables(ss->ddm, state,
                                tr->var_eff, tr->var_pre, tr->var_size);
    bdd = Cudd_bddAndAbstract(ss->ddm, bdd, tr->bdd, tr->exist_eff);
    Cudd_Ref(bdd);
    return bdd;
}

static DdNode *transSetApply(pddl_symbolic_task_t *ss,
                             pddl_symbolic_trans_set_t *trset,
                             DdNode *state,
                             DdNode *(*f)(pddl_symbolic_task_t *ss,
                                          pddl_symbolic_trans_t *tr,
                                          DdNode *state))
{
    if (trset->trans_size == 0)
        return NULL;

    DdNode *bdd = f(ss, trset->trans + 0, state);
    for (int i = 1; i < trset->trans_size; ++i){
        DdNode *bdd2 = f(ss, trset->trans + i, state);
        CUDD_OR(ss->ddm, bdd, bdd2);
        Cudd_RecursiveDeref(ss->ddm, bdd2);
    }
    return bdd;
}

static DdNode *transSetImage(pddl_symbolic_task_t *ss,
                             pddl_symbolic_trans_set_t *trset,
                             DdNode *state)
{
    return transSetApply(ss, trset, state, transImage);
}

static DdNode *transSetPreImage(pddl_symbolic_task_t *ss,
                                pddl_symbolic_trans_set_t *trset,
                                DdNode *state)
{
    return transSetApply(ss, trset, state, transPreImage);
}

static void stateFree(pddl_symbolic_task_t *ss, pddl_symbolic_state_t *state)
{
    Cudd_RecursiveDeref(ss->ddm, state->bdd);
}

static void statesInit(pddl_symbolic_task_t *ss, pddl_symbolic_states_t *states)
{
    bzero(states, sizeof(*states));
    size_t el_size = sizeof(pddl_symbolic_state_t);
    pddl_symbolic_state_t el_init;
    bzero(&el_init, sizeof(el_init));
    el_init.id = -1;

    states->pool = borExtArrNew(el_size, NULL, &el_init);
    states->num_states = 0;

    states->all_closed = Cudd_ReadLogicZero(ss->ddm);
    Cudd_Ref(states->all_closed);
}

static void statesFree(pddl_symbolic_task_t *ss, pddl_symbolic_states_t *states)
{
    for (int si = 0; si < states->num_states; ++si)
        stateFree(ss, borExtArrGet(states->pool, si));
    borExtArrDel(states->pool);
}

static pddl_symbolic_state_t *statesGet(pddl_symbolic_states_t *states, int id)
{
    return borExtArrGet(states->pool, id);
}

static void statesCloseState(pddl_symbolic_task_t *ss,
                             pddl_symbolic_states_t *states,
                             pddl_symbolic_state_t *state)
{
    ASSERT(!state->is_closed);
    state->is_closed = 1;
    CUDD_OR(ss->ddm, states->all_closed, state->bdd);
}

static void statesOpenState(pddl_symbolic_task_t *ss,
                            pddl_symbolic_states_t *states,
                            pddl_symbolic_state_t *state)
{
    // TODO
}

static pddl_symbolic_state_t *statesAddBDD(pddl_symbolic_task_t *ss,
                                           pddl_symbolic_states_t *states,
                                           DdNode *bdd)
{
    pddl_symbolic_state_t *state;
    state = borExtArrGet(states->pool, states->num_states);
    state->id = states->num_states;
    state->parent_id = -1;
    state->cost = 0;
    state->zero_cost = 0;
    state->bdd = bdd;
    Cudd_Ref(state->bdd);
    state->is_closed = 0;

    states->num_states++;
    return state;
}

static void statesAddInit(pddl_symbolic_task_t *ss,
                          pddl_symbolic_states_t *states,
                          DdNode *bdd)
{
    pddl_symbolic_state_t *state;
    state = statesAddBDD(ss, states, bdd);
    state->cost = 0;
    state->zero_cost = 0;
    statesOpenState(ss, states, state);
}

static void statesApplyOps(pddl_symbolic_task_t *ss,
                           pddl_symbolic_states_t *states,
                           pddl_symbolic_state_t *state_in,
                           DdNode *(*apply)(pddl_symbolic_task_t *ss,
                                            pddl_symbolic_trans_set_t *trset,
                                            DdNode *state))
{
    DdNode *bdd_in = state_in->bdd;
    Cudd_Ref(bdd_in);
    CUDD_AND(ss->ddm, bdd_in, Cudd_Not(states->all_closed));

    if (bdd_in == Cudd_ReadLogicZero(ss->ddm)){
        Cudd_RecursiveDeref(ss->ddm, bdd_in);
        return;
    }

    for (int tri = 0; tri < ss->trans.trans_size; ++tri){
        int tr_cost = ss->trans.trans[tri].cost;
        DdNode *bdd = apply(ss, ss->trans.trans + tri, bdd_in);
        if (bdd != Cudd_ReadLogicZero(ss->ddm)){
            pddl_symbolic_state_t *state = statesAddBDD(ss, states, bdd);
            state->parent_id = state_in->id;
            state->cost += state_in->cost + tr_cost;
            state->zero_cost = state_in->zero_cost;
            if (tr_cost == 0)
                state->zero_cost += 1;

            statesOpenState(ss, states, state);
        }

        Cudd_RecursiveDeref(ss->ddm, bdd);
    }

    Cudd_RecursiveDeref(ss->ddm, bdd_in);
}

pddl_symbolic_task_t *pddlSymbolicTaskNew(const pddl_strips_t *strips,
                                          const pddl_mgroups_t *mgroups,
                                          const pddl_mutex_pairs_t *mutex,
                                          const pddl_symbolic_task_config_t *cfg,
                                          bor_err_t *err)
{
    pddl_symbolic_task_t *ss;
    BOR_INFO(err, "symbolic: Constructing symbolic task."
                  " max mem: %dMB,"
                  " merge max nodes: %lu,"
                  " merge max time: %.2fs",
             cfg->max_mem_in_mb,
             cfg->trans_merge_max_nodes,
             cfg->trans_merge_max_time);

    ss = BOR_ALLOC(pddl_symbolic_task_t);
    bzero(ss, sizeof(*ss));

    ss->fact_size = strips->fact.fact_size;
    ss->ordered_facts = BOR_ALLOC_ARR(int, ss->fact_size);
    ss->fact_to_order = BOR_ALLOC_ARR(int, ss->fact_size);
    for (int fact_id = 0; fact_id < ss->fact_size; ++fact_id){
        ss->ordered_facts[fact_id] = fact_id;
        ss->fact_to_order[fact_id] = fact_id;
    }

    ss->pre_fact_to_var = BOR_CALLOC_ARR(int, ss->fact_size);
    ss->eff_fact_to_var = BOR_CALLOC_ARR(int, ss->fact_size);
    for (int fact_id = 0; fact_id < ss->fact_size; ++fact_id){
        int order = ss->fact_to_order[fact_id];
        ss->pre_fact_to_var[fact_id] = 2 * order;
        ss->eff_fact_to_var[fact_id] = 2 * order + 1;
    }
    ss->num_vars = 2 * ss->fact_size;

    unsigned int num_slots = CUDD_UNIQUE_SLOTS;
    unsigned int cache_size = CUDD_CACHE_SLOTS;
    size_t mem = 0;
    if (cfg->max_mem_in_mb > 0)
        mem = cfg->max_mem_in_mb * 1024UL * 1024UL;
    ss->ddm = Cudd_Init(ss->num_vars, 0, num_slots, cache_size, mem);
    if (ss->ddm == NULL){
        pddlSymbolicTaskDel(ss);
        BOR_ERR_RET2(err, NULL, "Initialization of CUDD failed.");
    }

    transSetsInit(ss, cfg, strips, &ss->trans, err);
    ss->zero_cost_trans = -1;
    if (ss->trans.trans[0].cost == 0)
        ss->zero_cost_trans = 0;

    ss->init = createState(ss, &strips->init);
    ss->goal = createPartialState(ss, &strips->goal);
    statesInit(ss, &ss->fw_state);
    statesAddInit(ss, &ss->fw_state, ss->init);


    ASSERT(Cudd_DebugCheck(ss->ddm) == 0);

    pddl_symbolic_state_t *state = statesGet(&ss->fw_state, 0);
    statesApplyOps(ss, &ss->fw_state, state, transSetImage);
    statesCloseState(ss, &ss->fw_state, state);

    for (int si = 0; si < ss->fw_state.num_states; ++si){
        pddl_symbolic_state_t *state = statesGet(&ss->fw_state, si);
        fprintf(stdout, "State %d, parent: %d, cost: %d, zero_cost: %d\n",
                state->id, state->parent_id, state->cost,
                state->zero_cost);
        Cudd_PrintDebug(ss->ddm, state->bdd, 20, 4);
    }

    printf("trans_size: %d\n", ss->trans.trans_size);
    DdNode *init = createState(ss, &strips->init);
    Cudd_Ref(init);
    DdNode *next = transImage(ss, ss->trans.trans[0].trans + 0, init);
    Cudd_Ref(next);
    DdNode *prev = transPreImage(ss, ss->trans.trans[0].trans + 0, next);
    Cudd_Ref(prev);
    Cudd_PrintDebug(ss->ddm, init, 20, 4);
    Cudd_PrintDebug(ss->ddm, next, 20, 4);
    Cudd_PrintDebug(ss->ddm, prev, 20, 4);

    DdNode *next2 = transSetImage(ss, ss->trans.trans + 0, init);
    Cudd_Ref(next2);
    Cudd_PrintDebug(ss->ddm, next2, 20, 4);

    DdNode *prev2 = transSetPreImage(ss, ss->trans.trans + 0, next2);
    Cudd_Ref(prev2);
    CUDD_AND(ss->ddm, prev2, init);
    Cudd_PrintDebug(ss->ddm, prev2, 20, 4);

    BOR_INFO(err, "symbolic: Symbolic task created."
                  " mem in use: %.2fMB, node count: %ld,"
                  " bdd variables: %d,"
                  " peak node count: %d,"
                  " peak live node count: %d,"
                  " garbage collections: %d",
             Cudd_ReadMemoryInUse(ss->ddm) / (1024. * 1024.),
             Cudd_ReadNodeCount(ss->ddm),
             Cudd_ReadSize(ss->ddm),
             Cudd_ReadPeakNodeCount(ss->ddm),
             Cudd_ReadPeakLiveNodeCount(ss->ddm),
             Cudd_ReadGarbageCollections(ss->ddm));
    //Cudd_PrintInfo(ss->ddm, stderr);
    return ss;
}

void pddlSymbolicTaskDel(pddl_symbolic_task_t *ss)
{
    statesFree(ss, &ss->fw_state);
    transSetsFree(ss, &ss->trans);
    if (ss->ordered_facts != NULL)
        BOR_FREE(ss->ordered_facts);
    if (ss->fact_to_order != NULL)
        BOR_FREE(ss->fact_to_order);
    if (ss->pre_fact_to_var != NULL)
        BOR_FREE(ss->pre_fact_to_var);
    if (ss->eff_fact_to_var != NULL)
        BOR_FREE(ss->eff_fact_to_var);
    if (ss->init != NULL)
        Cudd_RecursiveDeref(ss->ddm, ss->init);
    if (ss->goal != NULL)
        Cudd_RecursiveDeref(ss->ddm, ss->goal);
    if (ss->ddm != NULL)
        Cudd_Quit(ss->ddm);
    BOR_FREE(ss);
}

#else /* PDDL_CUDD */

#endif /* PDDL_CUDD */
