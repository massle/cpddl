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

#include <boruvka/alloc.h>
#include <boruvka/sort.h>
#include "pddl/symbolic_trans.h"
#include "assert.h"

struct op {
    int op_id;
    char *name;
    bor_iset_t pre;
    bor_iset_t neg_pre;
    bor_iset_t eff;
    bor_iset_t neg_eff;
    pddl_cost_t cost;
    pddl_cost_t heur_change;
    int is_dead;
};
typedef struct op op_t;

static void opInit(pddl_symbolic_constr_t *constr,
                   const pddl_strips_op_t *op_in,
                   int use_op_constr,
                   int op_id,
                   op_t *op,
                   pddl_cost_t *op_heur_change,
                   int sum_op_heur_change_to_cost,
                   bor_err_t *err)
{
    bzero(op, sizeof(*op));
    op->op_id = op_id;
    borISetUnion(&op->pre, &op_in->pre);
    borISetUnion(&op->eff, &op_in->add_eff);
    borISetUnion(&op->neg_eff, &op_in->del_eff);
    pddlCostSetOp(&op->cost, op_in->cost);
    if (op_in->name != NULL)
        op->name = BOR_STRDUP(op_in->name);

    // Disambiguate preconditions
    if (pddlDisambiguate(&constr->disambiguate, &op->pre, NULL,
                         1, 0, NULL, &op->pre) < 0){
        BOR_INFO(err, "Operator %d:(%s) skipped, because it"
                " is unreachable or dead-end", op->op_id, op->name);
        op->is_dead = 1;
        return;
    }
    borISetMinus(&op->eff, &op->pre);

    int fact_id;
    BOR_ISET_FOR_EACH(&op->pre, fact_id)
        borISetMinus(&op->neg_eff, constr->fact_mutex + fact_id);

    if (use_op_constr){
        int fact;

        BOR_ISET(fw_neg_pre);
        // Find negative preconditions
        BOR_ISET_FOR_EACH(&op->pre, fact){
            borISetUnion(&op->neg_pre, constr->fact_mutex_bw + fact);
            borISetUnion(&fw_neg_pre, constr->fact_mutex_fw + fact);
        }

        // E-delete facts that are mutex with the add effect
        BOR_ISET_FOR_EACH(&op->eff, fact)
            borISetUnion(&op->neg_eff, constr->fact_mutex_fw + fact);
        borISetMinus(&op->neg_eff, &fw_neg_pre);
        borISetMinus(&op->neg_eff, &op->neg_pre);
        borISetFree(&fw_neg_pre);
    }

    if (!borISetIsDisjoint(&op->neg_pre, &op->pre)
            || !borISetIsDisjoint(&op->neg_eff, &op->eff)){
        BOR_INFO(err, "Operator %d:(%s) skipped, because it"
                      " is unreachable or dead-end", op->op_id, op->name);
        op->is_dead = 1;
    }

    if (op_heur_change != NULL){
        if (sum_op_heur_change_to_cost){
            pddlCostSum(&op->cost, op_heur_change + op_id);
            if (op->cost.cost == 0 && op->cost.zero_cost == 0)
                op->cost.zero_cost = 1;
        }else{
            op->heur_change = op_heur_change[op_id];
            DBG(err, "%d:(%s) --> %d:%d / %d", op->op_id, op->name,
                op->heur_change.cost, op->heur_change.zero_cost, op->cost);
        }
    }
}

static void opFree(op_t *op)
{
    if (op->name != NULL)
        BOR_FREE(op->name);
    borISetFree(&op->pre);
    borISetFree(&op->neg_pre);
    borISetFree(&op->eff);
    borISetFree(&op->neg_eff);
}

static void opsInit(pddl_symbolic_constr_t *constr,
                    const pddl_strips_t *strips,
                    int use_op_constr,
                    op_t *ops,
                    pddl_cost_t *op_heur_change,
                    int sum_op_heur_change_to_cost,
                    bor_err_t *err)
{
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        opInit(constr, strips->op.op[op_id], use_op_constr,
               op_id, ops + op_id, op_heur_change, sum_op_heur_change_to_cost, err);
    }
}

static void transFree(pddl_bdd_manager_t *mgr, pddl_symbolic_trans_t *tr)
{
    pddlBDDDel(mgr, tr->bdd);
    for (int i = 0; i < tr->var_size; ++i){
        pddlBDDDel(mgr, tr->var_pre[i]);
        pddlBDDDel(mgr, tr->var_eff[i]);
    }
    if (tr->var_pre != NULL)
        BOR_FREE(tr->var_pre);
    if (tr->var_eff != NULL)
        BOR_FREE(tr->var_eff);
    pddlBDDDel(mgr, tr->exist_pre);
    pddlBDDDel(mgr, tr->exist_eff);
    borISetFree(&tr->eff_groups);
    bzero(tr, sizeof(*tr));
}

static void transSetFree(pddl_bdd_manager_t *mgr,
                         pddl_symbolic_trans_set_t *trset)
{
    for (int i = 0; i < trset->trans_size; ++i)
        transFree(mgr, trset->trans + i);
    if (trset->trans != NULL)
        BOR_FREE(trset->trans);
    borISetFree(&trset->op);
}

static void transInitEffVars(pddl_symbolic_vars_t *vars,
                             pddl_symbolic_trans_t *tr)
{
    pddlSymbolicVarsGroupsBDDVars(vars, &tr->eff_groups,
                                  &tr->var_pre, &tr->var_eff, &tr->var_size);
    tr->exist_pre = pddlBDDCube(vars->mgr, tr->var_pre, tr->var_size);
    tr->exist_eff = pddlBDDCube(vars->mgr, tr->var_eff, tr->var_size);
}

static void transInit(pddl_symbolic_vars_t *vars,
                      pddl_symbolic_constr_t *constr,
                      const op_t *op,
                      pddl_symbolic_trans_t *tr,
                      int use_op_constr,
                      bor_err_t *err)
{
    ASSERT(!op->is_dead);

    // Build the BDD from bottom up by first filling the array bdds and
    // then going over it from last to the first item
    pddl_bdd_t **bdds = BOR_CALLOC_ARR(pddl_bdd_t *, 2 * vars->group_size);
    int *pre_set = BOR_CALLOC_ARR(int, vars->group_size);
    int *eff_set = BOR_CALLOC_ARR(int, vars->group_size);

    int fact_id;
    BOR_ISET_FOR_EACH(&op->pre, fact_id){
        int group_id = pddlSymbolicVarsFactGroup(vars, fact_id);
        pre_set[group_id] = 1;
        ASSERT(bdds[2 * group_id] == NULL);
        bdds[2 * group_id] = pddlSymbolicVarsFactPreBDD(vars, fact_id);
    }

    BOR_ISET_FOR_EACH(&op->neg_pre, fact_id){
        ASSERT(!borISetIn(fact_id, &op->pre));
        int group_id = pddlSymbolicVarsFactGroup(vars, fact_id);
        pddl_bdd_t *bdd = pddlSymbolicVarsFactPreBDDNeg(vars, fact_id);
        if (bdds[2 * group_id] == NULL){
            bdds[2 * group_id] = bdd;
        }else{
            pddlBDDAndUpdate(vars->mgr, &bdds[2 * group_id], bdd);
            pddlBDDDel(vars->mgr, bdd);
        }
    }

    BOR_ISET_FOR_EACH(&op->eff, fact_id){
        int group_id = pddlSymbolicVarsFactGroup(vars, fact_id);
        eff_set[group_id] = 1;
        ASSERT(bdds[2 * group_id + 1] == NULL);
        bdds[2 * group_id + 1] = pddlSymbolicVarsFactEffBDD(vars, fact_id);
    }

    BOR_ISET_FOR_EACH(&op->neg_eff, fact_id){
        ASSERT(!borISetIn(fact_id, &op->eff));
        int group_id = pddlSymbolicVarsFactGroup(vars, fact_id);
        pddl_bdd_t *bdd = pddlSymbolicVarsFactEffBDDNeg(vars, fact_id);
        if (bdds[2 * group_id + 1] == NULL){
            bdds[2 * group_id + 1] = bdd;
        }else{
            pddlBDDAndUpdate(vars->mgr, &bdds[2 * group_id + 1], bdd);
            pddlBDDDel(vars->mgr, bdd);
        }

    }

    tr->bdd = pddlBDDOne(vars->mgr);
    for (int i = 2 * vars->group_size - 1; i >= 0; --i){
        if (bdds[i] != NULL){
            pddlBDDAndUpdate(vars->mgr, &tr->bdd, bdds[i]);
            pddlBDDDel(vars->mgr, bdds[i]);
        }
    }
    BOR_FREE(bdds);


    if (use_op_constr){
        int fact_id;


        BOR_ISET_FOR_EACH(&op->eff, fact_id){
            int group_id = vars->fact[fact_id].group_id;
            if (pre_set[group_id]){
                pddlBDDAndUpdate(vars->mgr, &tr->bdd,
                                 constr->group_mutex[group_id]);
            }

            pddlBDDAndUpdate(vars->mgr, &tr->bdd,
                             constr->group_mgroup[group_id]);
        }
    }

    for (int i = 0; i < vars->group_size; ++i){
        if (eff_set[i])
            borISetAdd(&tr->eff_groups, i);
    }
    transInitEffVars(vars, tr);

    BOR_FREE(pre_set);
    BOR_FREE(eff_set);
}

static int transMerge(pddl_symbolic_vars_t *vars,
                      pddl_symbolic_trans_t *dst,
                      pddl_symbolic_trans_t *tr1,
                      pddl_symbolic_trans_t *tr2,
                      size_t max_nodes)
{
    bzero(dst, sizeof(*dst));

    if (pddlBDDSize(tr1->bdd) >= max_nodes
            || pddlBDDSize(tr2->bdd) >= max_nodes){
        return -1;
    }

    pddl_bdd_t *bdd1 = pddlBDDClone(vars->mgr, tr1->bdd);
    pddl_bdd_t *bdd2 = pddlBDDClone(vars->mgr, tr2->bdd);

    borISetUnion2(&dst->eff_groups, &tr1->eff_groups, &tr2->eff_groups);
    int e1 = 0, esize1 = borISetSize(&tr1->eff_groups);
    int e2 = 0, esize2 = borISetSize(&tr2->eff_groups);
    int group_id;
    BOR_ISET_FOR_EACH(&dst->eff_groups, group_id){
        if (e1 < esize1 && borISetGet(&tr1->eff_groups, e1) == group_id){
            ++e1;
        }else{
            pddl_bdd_t *biimp;
            biimp = pddlSymbolicVarsCreateBiimp(vars, group_id);
            pddlBDDAndUpdate(vars->mgr, &bdd1, biimp);
            pddlBDDDel(vars->mgr, biimp);
        }

        if (e2 < esize2 && borISetGet(&tr2->eff_groups, e2) == group_id){
            ++e2;
        }else{
            pddl_bdd_t *biimp;
            biimp = pddlSymbolicVarsCreateBiimp(vars, group_id);
            pddlBDDAndUpdate(vars->mgr, &bdd2, biimp);
            pddlBDDDel(vars->mgr, biimp);
        }
    }

    if (max_nodes > 0){
        dst->bdd = pddlBDDOrLimit(vars->mgr, bdd1, bdd2, max_nodes, NULL);
    }else{
        dst->bdd = pddlBDDOr(vars->mgr, bdd1, bdd2);
    }
    pddlBDDDel(vars->mgr, bdd1);
    pddlBDDDel(vars->mgr, bdd2);
    if (dst->bdd == NULL){
        borISetFree(&dst->eff_groups);
        return -1;
    }

    transInitEffVars(vars, dst);

    return 0;
}

static void transSetsAddRange(pddl_symbolic_vars_t *vars,
                              pddl_symbolic_constr_t *constr,
                              pddl_symbolic_trans_set_t *trset,
                              op_t *ops,
                              const int *op_ids,
                              int op_ids_size,
                              int use_op_constr,
                              int max_nodes,
                              float max_time,
                              bor_err_t *err)
{
    bzero(trset, sizeof(*trset));
    trset->vars = vars;
    for (int i = 0; i < op_ids_size; ++i)
        borISetAdd(&trset->op, op_ids[i]);
    ASSERT(borISetSize(&trset->op) > 0);
    trset->cost = ops[op_ids[0]].cost;
    trset->heur_change = ops[op_ids[0]].heur_change;

    int T_size = borISetSize(&trset->op);
    pddl_symbolic_trans_t *T = BOR_CALLOC_ARR(pddl_symbolic_trans_t, T_size);
    int Tres_size = 0;
    pddl_symbolic_trans_t *Tres = BOR_CALLOC_ARR(pddl_symbolic_trans_t, T_size);
    for (int i = 0; i < T_size; ++i)
        transInit(vars, constr, ops + op_ids[i], T + i, use_op_constr, err);

    BOR_INFO(err, "Initialized individual trans BDDs: cost: %d:%d,"
                  " heur change: %d:%d ops: %d",
             trset->cost.cost,
             trset->cost.zero_cost,
             trset->heur_change.cost,
             trset->heur_change.zero_cost,
             borISetSize(&trset->op));

    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    pddlTimeLimitSet(&time_limit, max_time);
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
                    int res = transMerge(vars, &restr, T + i, T + i + 1,
                                         max_nodes);
                    if (res < 0){
                        Tres[Tres_size++] = T[i];
                        Tres[Tres_size++] = T[i + 1];
                        bzero(T + ins, sizeof(*T));

                    }else{
                        transFree(vars->mgr, T + i);
                        transFree(vars->mgr, T + i + 1);
                        T[ins] = restr;
                    }
                }
            }
            ++ins;
        }
        T_size = ins;
    }

    for (int i = 0; i < T_size; ++i){
        if (T[i].bdd != NULL)
            Tres[Tres_size++] = T[i];
    }

    trset->trans_size = Tres_size;
    trset->trans = BOR_CALLOC_ARR(pddl_symbolic_trans_t, trset->trans_size);
    memcpy(trset->trans, Tres, sizeof(pddl_symbolic_trans_t) * Tres_size);

    BOR_FREE(T);
    BOR_FREE(Tres);

    long nodes = 0;
    for (int i = 0; i < trset->trans_size; ++i)
        nodes += pddlBDDSize(trset->trans[i].bdd);
    BOR_INFO(err, "created trans BDDs: cost: %d, ops: %d, bdds: %d,"
                  " nodes: %lu, %s",
             trset->cost, borISetSize(&trset->op), trset->trans_size,
             nodes, (T_size > 1 ? "(time limit reached)" : ""));
}

static int opIdCostCmp(const void *a, const void *b, void *_ops)
{
    const int id1 = *(const int *)a;
    const int id2 = *(const int *)b;
    const op_t *ops = _ops;
    int cmp = pddlCostCmp(&ops[id1].cost, &ops[id2].cost);
    if (cmp == 0)
        cmp = pddlCostCmp(&ops[id1].heur_change, &ops[id2].heur_change);
    if (cmp == 0)
        return id1 - id2;
    return cmp;
}

static int add(pddl_symbolic_trans_sets_t *trset)
{
    if (trset->trans_size == trset->trans_alloc){
        trset->trans_alloc *= 2;
        trset->trans = BOR_REALLOC_ARR(trset->trans,
                                       pddl_symbolic_trans_set_t,
                                       trset->trans_alloc);
    }
    bzero(trset->trans + trset->trans_size, sizeof(*trset->trans));
    return trset->trans_size++;
}

void pddlSymbolicTransSetsInit(pddl_symbolic_trans_sets_t *trset,
                               pddl_symbolic_vars_t *vars,
                               pddl_symbolic_constr_t *constr,
                               const pddl_strips_t *strips,
                               int use_op_constr,
                               int max_nodes,
                               float max_time,
                               pddl_cost_t *op_heur_change,
                               int sum_op_heur_change_to_cost,
                               bor_err_t *err)
{
    bzero(trset, sizeof(*trset));
    trset->vars = vars;

    op_t *ops = BOR_CALLOC_ARR(op_t, strips->op.op_size);
    opsInit(constr, strips, use_op_constr, ops,
            op_heur_change, sum_op_heur_change_to_cost, err);

    trset->trans_size = 0;
    trset->trans_alloc = 2;
    trset->trans = BOR_CALLOC_ARR(pddl_symbolic_trans_set_t, trset->trans_alloc);

    int op_ids_size = strips->op.op_size;
    int *op_ids = BOR_ALLOC_ARR(int, op_ids_size);
    int ins = 0;
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        if (!ops[op_id].is_dead)
            op_ids[ins++] = op_id;
    }
    op_ids_size = ins;
    borSort(op_ids, op_ids_size, sizeof(int), opIdCostCmp, (void *)ops);

    int start = 0, end = 1;
    for (end = 1; end < op_ids_size; ++end){
        pddl_cost_t cost_start = ops[op_ids[start]].cost;
        pddl_cost_t heur_change_start = ops[op_ids[start]].heur_change;
        pddl_cost_t cost_end = ops[op_ids[end]].cost;
        pddl_cost_t heur_change_end = ops[op_ids[end]].heur_change;
        if (pddlCostCmp(&cost_start, &cost_end) != 0
                || pddlCostCmp(&heur_change_start, &heur_change_end) != 0){
            ASSERT(end > start);
            int tr_id = add(trset);
            ASSERT(tr_id < trset->trans_size);
            transSetsAddRange(vars, constr, trset->trans + tr_id, ops,
                              op_ids + start, end - start,
                              use_op_constr, max_nodes, max_time, err);
            start = end;
        }
    }
    if (end > start){
        int tr_id = add(trset);
        transSetsAddRange(vars, constr, trset->trans + tr_id, ops,
                          op_ids + start, end - start,
                          use_op_constr, max_nodes, max_time, err);
    }

    BOR_FREE(op_ids);

    for (int op_id = 0; op_id < strips->op.op_size; ++op_id)
        opFree(ops + op_id);
    BOR_FREE(ops);
}

void pddlSymbolicTransSetsFree(pddl_symbolic_trans_sets_t *trset)
{
    for (int i = 0; i < trset->trans_size; ++i)
        transSetFree(trset->vars->mgr, trset->trans + i);
    if (trset->trans != NULL)
        BOR_FREE(trset->trans);
}


static pddl_bdd_t *transImage(pddl_bdd_manager_t *mgr,
                              pddl_symbolic_trans_t *tr,
                              pddl_bdd_t *state)
{
    pddl_bdd_t *bdd1, *bdd;
    bdd1 = pddlBDDAndAbstract(mgr, state, tr->bdd, tr->exist_pre);
    bdd = pddlBDDSwapVars(mgr, bdd1, tr->var_pre, tr->var_eff, tr->var_size);
    pddlBDDDel(mgr, bdd1);
    return bdd;
}

static pddl_bdd_t *transPreImage(pddl_bdd_manager_t *mgr,
                                 pddl_symbolic_trans_t *tr,
                                 pddl_bdd_t *state)
{
    pddl_bdd_t *bdd1, *bdd;
    bdd1 = pddlBDDSwapVars(mgr, state, tr->var_eff, tr->var_pre, tr->var_size);
    bdd = pddlBDDAndAbstract(mgr, bdd1, tr->bdd, tr->exist_eff);
    pddlBDDDel(mgr, bdd1);
    return bdd;
}

static pddl_bdd_t *transSetApply(pddl_bdd_manager_t *mgr,
                                 pddl_symbolic_trans_set_t *trset,
                                 pddl_bdd_t *state,
                                 pddl_bdd_t *(*f)(pddl_bdd_manager_t *,
                                                  pddl_symbolic_trans_t *,
                                                  pddl_bdd_t *))
{
    if (trset->trans_size == 0)
        return NULL;

    pddl_bdd_t *bdd = f(mgr, trset->trans + 0, state);
    for (int i = 1; i < trset->trans_size; ++i){
        pddl_bdd_t *bdd2 = f(mgr, trset->trans + i, state);
        pddlBDDOrUpdate(mgr, &bdd, bdd2);
        pddlBDDDel(mgr, bdd2);
    }
    return bdd;
}

pddl_bdd_t *pddlSymbolicTransSetImage(pddl_symbolic_trans_set_t *trset,
                                      pddl_bdd_t *state)
{
    return transSetApply(trset->vars->mgr, trset, state, transImage);
}

pddl_bdd_t *pddlSymbolicTransSetPreImage(pddl_symbolic_trans_set_t *trset,
                                         pddl_bdd_t *state)
{
    return transSetApply(trset->vars->mgr, trset, state, transPreImage);
}
