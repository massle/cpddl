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

#include <boruvka/alloc.h>
#include <boruvka/sort.h>
#include <boruvka/extarr.h>
#include <boruvka/pairheap.h>

#include "pddl/symbolic_task.h"
#include "pddl/cost.h"
#include "pddl/time_limit.h"
#include "pddl/disambiguation.h"
#include "pddl/scc.h"
#include "assert.h"

struct pddl_symbolic_trans {
    pddl_bdd_t *bdd; /*!< BDD representing the transition(s) */
    bor_iset_t eff_groups; /*!< Groups appearing in the effect(s) */
    pddl_bdd_t **var_pre; /*!< List of pre variables */
    pddl_bdd_t **var_eff; /*!< List of eff variables */
    int var_size; /*!< Size of .var_pre and .var_eff */
    pddl_bdd_t *exist_pre; /*!< Cube from .var_pre */
    pddl_bdd_t *exist_eff; /*!< Cube from .var_eff */
};
typedef struct pddl_symbolic_trans pddl_symbolic_trans_t;

struct pddl_symbolic_trans_set {
    pddl_symbolic_trans_t *trans;
    int trans_size;

    bor_iset_t op; /*!< List of covered operators */
    pddl_cost_t cost; /*!< Cost of the covered operatros */
};
typedef struct pddl_symbolic_trans_set pddl_symbolic_trans_set_t;

struct pddl_symbolic_trans_sets {
    pddl_symbolic_trans_set_t *trans;
    int trans_size;
};
typedef struct pddl_symbolic_trans_sets pddl_symbolic_trans_sets_t;

struct pddl_symbolic_state {
    int id; /*!< ID of this state */
    int parent_id; /*!< Parent state ID */
    bor_iset_t parent_ids; /*!< IDs of parent state if this is a merge-state */
    int trans_id; /*!< ID of the transitions that achieved this state */
    pddl_cost_t cost; /*!< Cost of the state: g value + zero cost g value */
    // TODO: Add heuristic estimate
    pddl_bdd_t *bdd; /*!< BDD representing the state */
    int is_closed; /*!< True if the state is closed */
    bor_pairheap_node_t heap;
    bor_pairheap_node_t heap_cost;
};
typedef struct pddl_symbolic_state pddl_symbolic_state_t;

struct pddl_symbolic_states {
    bor_extarr_t *pool; /*!< Data pool */
    int num_states; /*!< Number of states stored in .pool */
    bor_pairheap_t *open; /*!< Open list */
    bor_pairheap_t *open_cost; /*!< Costs of states in the open list */
    bor_extarr_t *closed; /*!< Closed states stored with increasing cost */
    int num_closed; /*!< Number of closed states */
    pddl_bdd_t *all_closed; /*!< BDD representing all closed states */
    pddl_cost_t bound; /*!< Bound for the cost of the plan */
};
typedef struct pddl_symbolic_states pddl_symbolic_states_t;

typedef pddl_bdd_t *(*trans_set_image_fn)(pddl_symbolic_task_t *ss,
                                          pddl_symbolic_trans_set_t *trset,
                                          pddl_bdd_t *state);
struct pddl_symbolic_search {
    int fw; /*!< True if this is forward search */
    trans_set_image_fn image; /*!< Function constructing image */
    trans_set_image_fn pre_image; /*!< Function constructing pre-image */
    pddl_symbolic_constr_apply_fn constr_apply; /*!< Function for applying constraints */
    pddl_symbolic_states_t state; /*!< State space */
    pddl_bdd_t *goal; /*!< BDD describing the goal states */
    bor_iarr_t plan; /*!< Extracted plan */
    int plan_goal_id; /*!< This search's state where plan was reached */
    int plan_other_goal_id; /*!< Other search's state where plan was reached*/
    float next_step_estimate; /*!< Estimate of the duration of next step */
};
typedef struct pddl_symbolic_search pddl_symbolic_search_t;

struct pddl_symbolic_strips_op {
    int id;
    bor_iset_t pre;
    bor_iset_t neg_pre;
    bor_iset_t add_eff;
    bor_iset_t del_eff;
    bor_iset_t uncovered_eff;
    pddl_cost_t cost;
    char *name;
    int is_dead;
};
typedef struct pddl_symbolic_strips_op pddl_symbolic_strips_op_t;

struct pddl_symbolic_strips {
    int fact_size;
    pddl_symbolic_strips_op_t *op;
    int op_size;
    pddl_mgroups_t mgroup;
    bor_iset_t *fact_mutex;
    bor_iset_t *fact_mutex_fw;
    bor_iset_t *fact_mutex_bw;
    pddl_disambiguate_t *disambiguate;
};
typedef struct pddl_symbolic_strips pddl_symbolic_strips_t;

struct pddl_symbolic_task {
    pddl_symbolic_task_config_t cfg; /*!< Configuration */
    pddl_bdd_manager_t *ddm; /*!< Cudd manager */
    pddl_symbolic_strips_t strips; /*!< Prepared strips problem */
    pddl_symbolic_vars_t vars; /*!< TODO */
    int fact_size; /*!< Number of facts in the problem */
    int *ordered_facts; /*!< Ordered facts */
    int *fact_to_order; /*!< Mapping from fact to its order index */
    pddl_symbolic_trans_sets_t trans; /*!< BDD transitions */
    pddl_symbolic_constr_t constr; /*!< Constraints */
    pddl_bdd_t *init; /*!< Initial state */
    pddl_bdd_t *goal; /*!< Goal states */
    int goal_constr_failed; /*!< True if applying constraints on the goal
                                 failed */
};



static void transFree(pddl_symbolic_task_t *ss,
                      pddl_symbolic_trans_t *tr)
{
    pddlBDDDel(ss->ddm, tr->bdd);
    for (int i = 0; i < tr->var_size; ++i){
        pddlBDDDel(ss->ddm, tr->var_pre[i]);
        pddlBDDDel(ss->ddm, tr->var_eff[i]);
    }
    if (tr->var_pre != NULL)
        BOR_FREE(tr->var_pre);
    if (tr->var_eff != NULL)
        BOR_FREE(tr->var_eff);
    pddlBDDDel(ss->ddm, tr->exist_pre);
    pddlBDDDel(ss->ddm, tr->exist_eff);
    borISetFree(&tr->eff_groups);
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
    pddlSymbolicVarsGroupsBDDVars(&ss->vars, &tr->eff_groups,
                                  &tr->var_pre, &tr->var_eff, &tr->var_size);
    tr->exist_pre = pddlBDDCube(ss->ddm, tr->var_pre, tr->var_size);
    tr->exist_eff = pddlBDDCube(ss->ddm, tr->var_eff, tr->var_size);
}

static void transInit(pddl_symbolic_task_t *ss,
                      const pddl_symbolic_strips_op_t *op,
                      pddl_symbolic_trans_t *tr,
                      bor_err_t *err)
{
    ASSERT(!op->is_dead);

    // Build the BDD from bottom up by first filling the array bdds and
    // then going over it from last to the first item
    pddl_bdd_t **bdds = BOR_CALLOC_ARR(pddl_bdd_t *, 2 * ss->vars.group_size);
    int *pre_set = BOR_CALLOC_ARR(int, ss->vars.group_size);
    int *eff_set = BOR_CALLOC_ARR(int, ss->vars.group_size);

    int fact_id;
    BOR_ISET_FOR_EACH(&op->pre, fact_id){
        int group_id = pddlSymbolicVarsFactGroup(&ss->vars, fact_id);
        pre_set[group_id] = 1;
        ASSERT(bdds[2 * group_id] == NULL);
        bdds[2 * group_id] = pddlSymbolicVarsFactPreBDD(&ss->vars, fact_id);
    }

    BOR_ISET_FOR_EACH(&op->neg_pre, fact_id){
        ASSERT(!borISetIn(fact_id, &op->pre));
        int group_id = pddlSymbolicVarsFactGroup(&ss->vars, fact_id);
        pddl_bdd_t *bdd = pddlSymbolicVarsFactPreBDDNeg(&ss->vars, fact_id);
        if (bdds[2 * group_id] == NULL){
            bdds[2 * group_id] = bdd;
        }else{
            pddlBDDAndUpdate(ss->ddm, &bdds[2 * group_id], bdd);
            pddlBDDDel(ss->ddm, bdd);
        }
    }

    BOR_ISET_FOR_EACH(&op->add_eff, fact_id){
        int group_id = pddlSymbolicVarsFactGroup(&ss->vars, fact_id);
        eff_set[group_id] = 1;
        ASSERT(bdds[2 * group_id + 1] == NULL);
        bdds[2 * group_id + 1]
                = pddlSymbolicVarsFactEffBDD(&ss->vars, fact_id);
    }

    BOR_ISET_FOR_EACH(&op->del_eff, fact_id){
        ASSERT(!borISetIn(fact_id, &op->add_eff));
        int group_id = pddlSymbolicVarsFactGroup(&ss->vars, fact_id);
        pddl_bdd_t *bdd = pddlSymbolicVarsFactEffBDDNeg(&ss->vars, fact_id);
        if (bdds[2 * group_id + 1] == NULL){
            bdds[2 * group_id + 1] = bdd;
        }else{
            pddlBDDAndUpdate(ss->ddm, &bdds[2 * group_id + 1], bdd);
            pddlBDDDel(ss->ddm, bdd);
        }

    }

    tr->bdd = pddlBDDOne(ss->ddm);
    for (int i = 2 * ss->vars.group_size - 1; i >= 0; --i){
        if (bdds[i] != NULL){
            pddlBDDAndUpdate(ss->ddm, &tr->bdd, bdds[i]);
            pddlBDDDel(ss->ddm, bdds[i]);
        }
    }
    BOR_FREE(bdds);


    if (ss->cfg.use_op_constr){
        int fact_id;


        BOR_ISET_FOR_EACH(&op->add_eff, fact_id){
            int group_id = ss->vars.fact[fact_id].group_id;
            if (!pre_set[group_id]){
                // TODO: pre-compute
                int fid;
                BOR_ISET_FOR_EACH(&ss->vars.group[group_id].fact, fid){
                    int fact_id2;
                    BOR_ISET_FOR_EACH(ss->strips.fact_mutex_bw + fid, fact_id2){
                        pddl_bdd_t *mutex;
                        mutex = pddlSymbolicVarsCreateMutexPre(&ss->vars,
                                                               fid, fact_id2);
                        pddlBDDAndUpdate(ss->ddm, &tr->bdd, mutex);
                        pddlBDDDel(ss->ddm, mutex);
                    }
                }
            }


            pddl_bdd_t *mg;
            // TODO: pre-compute, use all mutex groups
            mg = pddlSymbolicVarsCreateExactlyOneMGroupPre(
                        &ss->vars, &ss->vars.group[group_id].fact);
            pddlBDDAndUpdate(ss->ddm, &tr->bdd, mg);
            pddlBDDDel(ss->ddm, mg);
        }
    }

    for (int i = 0; i < ss->vars.group_size; ++i){
        if (eff_set[i])
            borISetAdd(&tr->eff_groups, i);
    }
    transInitEffVars(ss, tr);
}

static int transMerge(pddl_symbolic_task_t *ss,
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

    pddl_bdd_t *bdd1 = pddlBDDClone(ss->ddm, tr1->bdd);
    pddl_bdd_t *bdd2 = pddlBDDClone(ss->ddm, tr2->bdd);

    borISetUnion2(&dst->eff_groups, &tr1->eff_groups, &tr2->eff_groups);
    int e1 = 0, esize1 = borISetSize(&tr1->eff_groups);
    int e2 = 0, esize2 = borISetSize(&tr2->eff_groups);
    int group_id;
    BOR_ISET_FOR_EACH(&dst->eff_groups, group_id){
        if (e1 < esize1 && borISetGet(&tr1->eff_groups, e1) == group_id){
            ++e1;
        }else{
            pddl_bdd_t *biimp;
            biimp = pddlSymbolicVarsCreateBiimp(&ss->vars, group_id);
            pddlBDDAndUpdate(ss->ddm, &bdd1, biimp);
            pddlBDDDel(ss->ddm, biimp);
        }

        if (e2 < esize2 && borISetGet(&tr2->eff_groups, e2) == group_id){
            ++e2;
        }else{
            pddl_bdd_t *biimp;
            biimp = pddlSymbolicVarsCreateBiimp(&ss->vars, group_id);
            pddlBDDAndUpdate(ss->ddm, &bdd2, biimp);
            pddlBDDDel(ss->ddm, biimp);
        }
    }

    if (max_nodes > 0){
        dst->bdd = pddlBDDOrLimit(ss->ddm, bdd1, bdd2, max_nodes, NULL);
    }else{
        dst->bdd = pddlBDDOr(ss->ddm, bdd1, bdd2);
    }
    pddlBDDDel(ss->ddm, bdd1);
    pddlBDDDel(ss->ddm, bdd2);
    if (dst->bdd == NULL){
        borISetFree(&dst->eff_groups);
        return -1;
    }

    transInitEffVars(ss, dst);

    return 0;
}

static void transSetsAddRange(pddl_symbolic_task_t *ss,
                              pddl_symbolic_trans_set_t *trset,
                              const int *op_ids,
                              int op_ids_size,
                              bor_err_t *err)
{
    bzero(trset, sizeof(*trset));
    for (int i = 0; i < op_ids_size; ++i)
        borISetAdd(&trset->op, op_ids[i]);
    ASSERT(borISetSize(&trset->op) > 0);
    trset->cost = ss->strips.op[op_ids[0]].cost;

    int T_size = borISetSize(&trset->op);
    pddl_symbolic_trans_t *T = BOR_CALLOC_ARR(pddl_symbolic_trans_t, T_size);
    int Tres_size = 0;
    pddl_symbolic_trans_t *Tres = BOR_CALLOC_ARR(pddl_symbolic_trans_t, T_size);
    for (int i = 0; i < T_size; ++i)
        transInit(ss, ss->strips.op + op_ids[i], T + i, err);

    BOR_INFO(err, "Initialized individual trans BDDs: cost: %d, ops: %d",
             trset->cost, borISetSize(&trset->op));

    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    pddlTimeLimitSet(&time_limit, ss->cfg.trans_merge_max_time);
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
                                         ss->cfg.trans_merge_max_nodes);
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
                          const pddl_strips_t *strips,
                          const pddl_mutex_pairs_t *mutex,
                          const pddl_mgroups_t *mgroup,
                          pddl_symbolic_trans_sets_t *trset,
                          bor_err_t *err)
{
    bzero(trset, sizeof(*trset));

    BOR_ISET(costs);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        if (!ss->strips.op[op_id].is_dead)
            borISetAdd(&costs, strips->op.op[op_id]->cost);
    }

    trset->trans_size = borISetSize(&costs);
    trset->trans = BOR_CALLOC_ARR(pddl_symbolic_trans_set_t, trset->trans_size);
    borISetFree(&costs);

    int op_ids_size = strips->op.op_size;
    int *op_ids = BOR_ALLOC_ARR(int, op_ids_size);
    int ins = 0;
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        if (!ss->strips.op[op_id].is_dead)
            op_ids[ins++] = op_id;
    }
    op_ids_size = ins;
    borSort(op_ids, op_ids_size, sizeof(int), opIdCostCmp, (void *)strips);

    int start = 0, end = 1, tr_id = 0;
    for (end = 1; end < op_ids_size; ++end){
        int cost_start = strips->op.op[op_ids[start]]->cost;
        int cost_end = strips->op.op[op_ids[end]]->cost;
        if (cost_start != cost_end){
            ASSERT(end > start);
            ASSERT(tr_id < trset->trans_size);
            transSetsAddRange(ss, trset->trans + tr_id,
                              op_ids + start, end - start, err);
            ++tr_id;
            start = end;
        }
    }
    if (end > start){
        transSetsAddRange(ss, trset->trans + tr_id,
                          op_ids + start, end - start, err);
        ++tr_id;
    }
    ASSERT(trset->trans_size == tr_id);

    BOR_FREE(op_ids);
}


static pddl_bdd_t *transImage(pddl_symbolic_task_t *ss,
                          pddl_symbolic_trans_t *tr,
                          pddl_bdd_t *state)
{
    pddl_bdd_t *bdd1, *bdd;
    bdd1 = pddlBDDAndAbstract(ss->ddm, state, tr->bdd, tr->exist_pre);
    bdd = pddlBDDSwapVars(ss->ddm, bdd1, tr->var_pre, tr->var_eff,
                          tr->var_size);
    pddlBDDDel(ss->ddm, bdd1);
    return bdd;
}

static pddl_bdd_t *transPreImage(pddl_symbolic_task_t *ss,
                          pddl_symbolic_trans_t *tr,
                          pddl_bdd_t *state)
{
    pddl_bdd_t *bdd1, *bdd;
    bdd1 = pddlBDDSwapVars(ss->ddm, state, tr->var_eff, tr->var_pre,
                           tr->var_size);
    bdd = pddlBDDAndAbstract(ss->ddm, bdd1, tr->bdd, tr->exist_eff);
    pddlBDDDel(ss->ddm, bdd1);
    return bdd;
}

static pddl_bdd_t *transSetApply(pddl_symbolic_task_t *ss,
                             pddl_symbolic_trans_set_t *trset,
                             pddl_bdd_t *state,
                             pddl_bdd_t *(*f)(pddl_symbolic_task_t *ss,
                                          pddl_symbolic_trans_t *tr,
                                          pddl_bdd_t *state))
{
    if (trset->trans_size == 0)
        return NULL;

    pddl_bdd_t *bdd = f(ss, trset->trans + 0, state);
    for (int i = 1; i < trset->trans_size; ++i){
        pddl_bdd_t *bdd2 = f(ss, trset->trans + i, state);
        pddlBDDOrUpdate(ss->ddm, &bdd, bdd2);
        pddlBDDDel(ss->ddm, bdd2);
    }
    return bdd;
}

static pddl_bdd_t *transSetImage(pddl_symbolic_task_t *ss,
                             pddl_symbolic_trans_set_t *trset,
                             pddl_bdd_t *state)
{
    return transSetApply(ss, trset, state, transImage);
}

static pddl_bdd_t *transSetPreImage(pddl_symbolic_task_t *ss,
                                pddl_symbolic_trans_set_t *trset,
                                pddl_bdd_t *state)
{
    return transSetApply(ss, trset, state, transPreImage);
}

static void stateFree(pddl_symbolic_task_t *ss, pddl_symbolic_state_t *state)
{
    if (state->bdd != NULL)
        pddlBDDDel(ss->ddm, state->bdd);
    borISetFree(&state->parent_ids);
}


static int openLT(const bor_pairheap_node_t *n1,
                  const bor_pairheap_node_t *n2,
                  void *data)
{
    const pddl_symbolic_state_t *o1, *o2;
    o1 = bor_container_of(n1, pddl_symbolic_state_t, heap);
    o2 = bor_container_of(n2, pddl_symbolic_state_t, heap);
    return pddlCostCmp(&o1->cost, &o2->cost) <= 0;
}

static int openCostLT(const bor_pairheap_node_t *n1,
                      const bor_pairheap_node_t *n2,
                      void *data)
{
    const pddl_symbolic_state_t *o1, *o2;
    o1 = bor_container_of(n1, pddl_symbolic_state_t, heap_cost);
    o2 = bor_container_of(n2, pddl_symbolic_state_t, heap_cost);
    return pddlCostCmp(&o1->cost, &o2->cost) <= 0;
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

    states->open = borPairHeapNew(openLT, states);
    states->open_cost = borPairHeapNew(openCostLT, states);

    el_size = sizeof(int);
    int closed_el = -1;
    states->closed = borExtArrNew(el_size, NULL, &closed_el);
    states->num_closed = 0;

    states->all_closed = pddlBDDZero(ss->ddm);

    pddlCostSetMax(&states->bound);
}

static void statesFree(pddl_symbolic_task_t *ss, pddl_symbolic_states_t *states)
{
    borPairHeapDel(states->open_cost);
    borPairHeapDel(states->open);

    pddlBDDDel(ss->ddm, states->all_closed);

    for (int si = 0; si < states->num_states; ++si)
        stateFree(ss, borExtArrGet(states->pool, si));
    borExtArrDel(states->pool);

    borExtArrDel(states->closed);
}

static pddl_symbolic_state_t *statesGet(pddl_symbolic_states_t *states, int id)
{
    return borExtArrGet(states->pool, id);
}

static pddl_symbolic_state_t *statesGetClosed(pddl_symbolic_states_t *states,
                                              int idx)
{
    const int *state_id = borExtArrGet(states->closed, idx);
    return statesGet(states, *state_id);
}

static void statesCloseState(pddl_symbolic_task_t *ss,
                             pddl_symbolic_states_t *states,
                             pddl_symbolic_state_t *state)
{
    ASSERT(!state->is_closed);
    state->is_closed = 1;
    ASSERT(state->bdd != NULL);
    pddlBDDOrUpdate(ss->ddm, &states->all_closed, state->bdd);
    int *dst = borExtArrGet(states->closed, states->num_closed);
    *dst = state->id;
    ++states->num_closed;
}

static void statesOpenState(pddl_symbolic_task_t *ss,
                            pddl_symbolic_states_t *states,
                            pddl_symbolic_state_t *state)
{
    ASSERT(!state->is_closed);
    borPairHeapAdd(states->open, &state->heap);
    borPairHeapAdd(states->open_cost, &state->heap_cost);
}

static pddl_symbolic_state_t *statesNextOpen(pddl_symbolic_states_t *states)
{
    if (borPairHeapEmpty(states->open))
        return NULL;

    bor_pairheap_node_t *hstate = borPairHeapExtractMin(states->open);
    pddl_symbolic_state_t *state;
    state = bor_container_of(hstate, pddl_symbolic_state_t, heap);
    borPairHeapRemove(states->open_cost, &state->heap_cost);
    return state;
}

static pddl_symbolic_state_t *statesOpenPeek(pddl_symbolic_states_t *states)
{
    if (borPairHeapEmpty(states->open))
        return NULL;

    bor_pairheap_node_t *hstate = borPairHeapMin(states->open);
    pddl_symbolic_state_t *state;
    state = bor_container_of(hstate, pddl_symbolic_state_t, heap);
    return state;
}

static const pddl_cost_t *
    statesMinOpenCost(const pddl_symbolic_states_t *states)
{
    if (borPairHeapEmpty(states->open_cost))
        return NULL;

    bor_pairheap_node_t *hstate = borPairHeapMin(states->open_cost);
    pddl_symbolic_state_t *state;
    state = bor_container_of(hstate, pddl_symbolic_state_t, heap_cost);
    return &state->cost;
}

static pddl_symbolic_state_t *statesAdd(pddl_symbolic_task_t *ss,
                                        pddl_symbolic_states_t *states)
{
    pddl_symbolic_state_t *state;
    state = borExtArrGet(states->pool, states->num_states);
    state->id = states->num_states;
    state->parent_id = -1;
    state->trans_id = -1;
    pddlCostSetZero(&state->cost);
    state->bdd = NULL;
    state->is_closed = 0;

    states->num_states++;
    return state;
}

static pddl_symbolic_state_t *statesAddBDD(pddl_symbolic_task_t *ss,
                                           pddl_symbolic_states_t *states,
                                           pddl_bdd_t *bdd)
{
    pddl_symbolic_state_t *state = statesAdd(ss, states);
    if (bdd != NULL)
        state->bdd = pddlBDDClone(ss->ddm, bdd);
    return state;
}

static void statesAddInit(pddl_symbolic_task_t *ss,
                          pddl_symbolic_states_t *states,
                          pddl_bdd_t *bdd)
{
    pddl_symbolic_state_t *state;
    state = statesAddBDD(ss, states, bdd);
    pddlCostSetZero(&state->cost);
    statesOpenState(ss, states, state);
}



static void searchInit(pddl_symbolic_task_t *ss,
                       pddl_symbolic_search_t *search,
                       int fw,
                       trans_set_image_fn image,
                       trans_set_image_fn pre_image,
                       pddl_symbolic_constr_apply_fn constr_apply,
                       pddl_bdd_t *init,
                       pddl_bdd_t *goal)
{
    bzero(search, sizeof(*search));
    search->fw = fw;
    search->image = image;
    search->pre_image = pre_image;
    if (ss->cfg.use_constr)
        search->constr_apply = constr_apply;
    statesInit(ss, &search->state);
    search->goal = goal;
    if (search->goal != NULL)
        search->goal = pddlBDDClone(ss->ddm, search->goal);

    statesAddInit(ss, &search->state, init);

    search->plan_goal_id = -1;
    search->plan_other_goal_id = -1;
}

static void searchFree(pddl_symbolic_task_t *ss,
                       pddl_symbolic_search_t *search)
{
    statesFree(ss, &search->state);
    if (search->goal != NULL)
        pddlBDDDel(ss->ddm, search->goal);
    borIArrFree(&search->plan);
}

static pddl_bdd_t *bddStateSelectOne(pddl_symbolic_task_t *ss,
                                 pddl_bdd_t *bdd,
                                 bor_iset_t *state)
{
    borISetEmpty(state);
    char *cube = BOR_ALLOC_ARR(char, ss->vars.bdd_var_size);
    pddlBDDPickOneCube(ss->ddm, bdd, cube);
    for (int gi = 0; gi < ss->vars.group_size; ++gi){
        int fact_id = pddlSymbolicVarsFactFromBDDCube(&ss->vars, gi, cube);
        ASSERT(fact_id >= 0);
        borISetAdd(state, fact_id);
    }
    BOR_FREE(cube);
    return pddlSymbolicVarsCreateState(&ss->vars, state);
}

struct plan {
    int plan_len;
    bor_iset_t *state;
    bor_iset_t **tr_op;
};
typedef struct plan plan_t;

static const pddl_symbolic_state_t *
        planNextState(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      const pddl_symbolic_state_t *state,
                      pddl_bdd_t *bdd)
{
    if (borISetSize(&state->parent_ids) == 0)
        return state;

    int state_id;
    BOR_ISET_FOR_EACH(&state->parent_ids, state_id){
        const pddl_symbolic_state_t *state;
        state = statesGet(&search->state, state_id);
        ASSERT(state->trans_id >= 0);
        // The state BDD must be already constructed
        ASSERT_RUNTIME(state->bdd != NULL);
        pddl_bdd_t *conj = pddlBDDAnd(ss->ddm, bdd, state->bdd);
        if (!pddlBDDIsFalse(ss->ddm, conj)){
            pddlBDDDel(ss->ddm, conj);
            return state;
        }
        pddlBDDDel(ss->ddm, conj);
    }
    ASSERT_RUNTIME(0);
    return state;
}

static void planInit(pddl_symbolic_task_t *ss,
                     pddl_symbolic_search_t *search,
                     plan_t *plan,
                     const pddl_symbolic_state_t *goal_state,
                     pddl_bdd_t *reached_goal)
{
    bzero(plan, sizeof(*plan));

    int alloc = 2;
    plan->state = BOR_CALLOC_ARR(bor_iset_t, alloc + 1);
    plan->tr_op = BOR_CALLOC_ARR(bor_iset_t *, alloc);

    // Backtrack from the goal_state and extract one particular state at
    // each step.
    // Select one specific state -- it doesn't matter which one
    pddl_bdd_t *bdd = bddStateSelectOne(ss, reached_goal, plan->state + 0);
    const pddl_symbolic_state_t *state;
    state = planNextState(ss, search, goal_state, bdd);
    while (state->parent_id >= 0){
        ASSERT(borISetSize(&state->parent_ids) == 0);
        ASSERT(state->trans_id >= 0);
        ASSERT(state->parent_id >= 0);

        int idx = plan->plan_len++;
        if (idx == alloc){
            int old_alloc = alloc;
            alloc *= 2;
            plan->state = BOR_REALLOC_ARR(plan->state, bor_iset_t, alloc + 1);
            bzero(plan->state + old_alloc + 1,
                  sizeof(bor_iset_t) * (alloc - old_alloc));
            plan->tr_op = BOR_REALLOC_ARR(plan->tr_op, bor_iset_t *, alloc);
        }

        plan->tr_op[idx] = &ss->trans.trans[state->trans_id].op;
        const pddl_symbolic_state_t *prev_state;
        prev_state = statesGet(&search->state, state->parent_id);

        // This step of the plan goes from prev_state to state.
        // So, compute the conjuction of the preimage of state and
        // state_prev.
        pddl_symbolic_trans_set_t *trset = ss->trans.trans + state->trans_id;
        pddl_bdd_t *preimg = search->pre_image(ss, trset, bdd);
        ASSERT_RUNTIME(!pddlBDDIsFalse(ss->ddm, preimg));
        pddlBDDAndUpdate(ss->ddm, &preimg, prev_state->bdd);

        // Select one of the states -- again, it doesn't matter which one
        pddlBDDDel(ss->ddm, bdd);
        bdd = bddStateSelectOne(ss, preimg, plan->state + idx + 1);
        pddlBDDDel(ss->ddm, preimg);
        state = prev_state;
        state = planNextState(ss, search, prev_state, bdd);
    }
    pddlBDDDel(ss->ddm, bdd);

    // Reverse the order of states and transitions
    for (int i = 0; i < (plan->plan_len + 1) / 2; ++i){
        bor_iset_t tmp;
        BOR_SWAP(plan->state[i], plan->state[plan->plan_len - i], tmp);
    }
    for (int i = 0; i < plan->plan_len / 2; ++i){
        bor_iset_t *tmp;
        BOR_SWAP(plan->tr_op[i], plan->tr_op[plan->plan_len - i - 1], tmp);
    }
}

static void planFree(plan_t *plan)
{
    for (int i = 0; i < plan->plan_len + 1; ++i)
        borISetFree(plan->state + i);
    BOR_FREE(plan->state);
    BOR_FREE(plan->tr_op);
}

static void planReverse(plan_t *plan)
{
    bor_iset_t state_tmp;
    int len = (plan->plan_len + 1) / 2;
    for (int i = 0; i < len; ++i){
        BOR_SWAP(plan->state[i],
                 plan->state[plan->plan_len - i],
                 state_tmp);
    }

    bor_iset_t *tr_tmp;
    len = plan->plan_len / 2;
    for (int i = 0; i < len; ++i){
        BOR_SWAP(plan->tr_op[i],
                 plan->tr_op[plan->plan_len - i - 1],
                 tr_tmp);
    }
}

static void planExtractFw(plan_t *plan,
                          const pddl_symbolic_strips_t *strips,
                          bor_iarr_t *out)
{
    // Extract plan from the intermediate states
    BOR_ISET(res_state);
    for (int si = 0; si < plan->plan_len; ++si){
        const bor_iset_t *from = plan->state + si;
        const bor_iset_t *to = plan->state + si + 1;

        int op_id;
        int found = 0;
        BOR_ISET_FOR_EACH(plan->tr_op[si], op_id){
            const pddl_symbolic_strips_op_t *op = strips->op + op_id;
            if (borISetIsSubset(&op->pre, from)){
                borISetMinus2(&res_state, from, &op->del_eff);
                borISetUnion(&res_state, &op->add_eff);
                if (borISetEq(&res_state, to)){
                    borIArrAdd(out, op_id);
                    found = 1;
                    break;
                }
            }
        }
        ASSERT_RUNTIME(found);
    }
    borISetFree(&res_state);
}


static pddl_bdd_t *searchStateBDD(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              pddl_symbolic_state_t *state)
{
    if (state->bdd == NULL){
        const pddl_symbolic_state_t *prev_state;
        prev_state = statesGet(&search->state, state->parent_id);
        state->bdd = search->image(ss, ss->trans.trans + state->trans_id,
                                   prev_state->bdd);
        pddl_bdd_t *nall = pddlBDDNot(ss->ddm, search->state.all_closed);
        pddlBDDAndUpdate(ss->ddm, &state->bdd, nall);
        pddlBDDDel(ss->ddm, nall);
        if (search->constr_apply)
            search->constr_apply(&ss->constr, &state->bdd);
    }
    return state->bdd;
}

static int searchNextOpenSize(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search)
{
    pddl_symbolic_state_t *state = statesOpenPeek(&search->state);
    if (state == NULL)
        return 0;
    pddl_bdd_t *bdd = searchStateBDD(ss, search, state);
    return pddlBDDSize(bdd);
}




static int checkGoal(pddl_symbolic_task_t *ss,
                     pddl_symbolic_search_t *search,
                     const pddl_symbolic_state_t *state,
                     bor_err_t *err)
{
    pddl_bdd_t *goal = pddlBDDAnd(ss->ddm, state->bdd, search->goal);
    if (!pddlBDDIsFalse(ss->ddm, goal)){
        plan_t plan;
        planInit(ss, search, &plan, state, goal);
        if (!search->fw)
            planReverse(&plan);
        planExtractFw(&plan, &ss->strips, &search->plan);
        planFree(&plan);
        pddlBDDDel(ss->ddm, goal);
        return 1;
    }
    pddlBDDDel(ss->ddm, goal);
    return 0;
}

static int costStatesIsBetter(const pddl_symbolic_search_t *search,
                              const pddl_symbolic_state_t *s1,
                              const pddl_symbolic_state_t *s2)
{
    return pddlCostCmpSum(&s1->cost, &s2->cost, &search->state.bound) < 0;
}

static void searchSetBestPlan(pddl_symbolic_search_t *search,
                              const pddl_symbolic_state_t *s1,
                              const pddl_symbolic_state_t *s2)
{
    search->state.bound = s1->cost;
    pddlCostSum(&search->state.bound, &s2->cost);
    search->plan_goal_id = s1->id;
    search->plan_other_goal_id = s2->id;
}

static int checkGoal2(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      pddl_symbolic_search_t *other_search,
                      pddl_symbolic_state_t *state,
                      bor_err_t *err)
{
    int res = 0;
    pddl_bdd_t *state_bdd = searchStateBDD(ss, search, state);
    pddl_bdd_t *goal = pddlBDDAnd(ss->ddm, state_bdd,
                                  other_search->state.all_closed);
    if (!pddlBDDIsFalse(ss->ddm, goal)){
        for (int si = 0; si < other_search->state.num_closed; ++si){
            const pddl_symbolic_state_t *closed_state;
            closed_state = statesGetClosed(&other_search->state, si);
            if (!costStatesIsBetter(search, state, closed_state))
                break;

            pddl_bdd_t *goal = pddlBDDAnd(ss->ddm, state_bdd, closed_state->bdd);
            if (!pddlBDDIsFalse(ss->ddm, goal)){
                searchSetBestPlan(search, state, closed_state);
                searchSetBestPlan(other_search, closed_state, state);
                BOR_INFO(err, "%s: Found best plan so far: cost: %d:%d",
                         (search->fw ? "fw" : "bw"),
                         search->state.bound.cost,
                         search->state.bound.zero_cost);
                res = 1;
            }
            pddlBDDDel(ss->ddm, goal);

            if (res)
                break;
        }
    }
    pddlBDDDel(ss->ddm, goal);
    return res;
}

static void searchSetNextStepEstimate(pddl_symbolic_task_t *ss,
                                      pddl_symbolic_search_t *search,
                                      pddl_symbolic_state_t *state,
                                      float cur_time,
                                      bor_err_t *err)
{
    pddl_bdd_t *state_bdd = searchStateBDD(ss, search, state);
    long bdd_size = pddlBDDSize(state_bdd);
    if (bdd_size == 0){
        search->next_step_estimate = 0.f;
    }else if (cur_time < 1.){
        search->next_step_estimate = cur_time;
    }else{
        int next_size = searchNextOpenSize(ss, search);
        float est = ((float)next_size / (float)bdd_size) * cur_time;
        search->next_step_estimate = est;
    }
}

static void searchExpandState(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              pddl_symbolic_search_t *other_search,
                              pddl_symbolic_state_t *state_in,
                              bor_err_t *err)
{
    pddl_symbolic_states_t *states = &search->state;
    pddl_bdd_t *bdd_in = pddlBDDClone(ss->ddm, state_in->bdd);
    ASSERT(bdd_in != NULL);
    pddl_bdd_t *nall = pddlBDDNot(ss->ddm, states->all_closed);
    pddlBDDAndUpdate(ss->ddm, &bdd_in, nall);
    pddlBDDDel(ss->ddm, nall);

    if (pddlBDDIsFalse(ss->ddm, bdd_in)){
        pddlBDDDel(ss->ddm, bdd_in);
        return;
    }

    for (int tri = 0; tri < ss->trans.trans_size; ++tri){
        const pddl_cost_t *tr_cost = &ss->trans.trans[tri].cost;
        if (pddlCostCmpSum(&state_in->cost, tr_cost, &states->bound) >= 0)
            continue;

        pddl_symbolic_state_t *state = statesAdd(ss, states);
        state->parent_id = state_in->id;
        state->trans_id = tri;
        state->cost = state_in->cost;
        pddlCostSum(&state->cost, tr_cost);

        statesOpenState(ss, states, state);
        if (other_search != NULL)
            checkGoal2(ss, search, other_search, state, err);
    }

    pddlBDDDel(ss->ddm, bdd_in);
}

static pddl_symbolic_state_t *searchNextNonEmpty(pddl_symbolic_task_t *ss,
                                                 pddl_symbolic_search_t *search,
                                                 bor_err_t *err)
{
    pddl_symbolic_state_t *state;
    do {
        state = statesNextOpen(&search->state);
        if (state != NULL){
            searchStateBDD(ss, search, state);
            pddl_bdd_t *nall = pddlBDDNot(ss->ddm, search->state.all_closed);
            pddlBDDAndUpdate(ss->ddm, &state->bdd, nall);
            pddlBDDDel(ss->ddm, nall);
        }
    } while (state != NULL && pddlBDDIsFalse(ss->ddm, state->bdd));

    return state;
}

static void searchPrepareNext(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              bor_err_t *err)
{
    pddl_symbolic_state_t *state = searchNextNonEmpty(ss, search, err);
    if (state == NULL)
        return;

    BOR_ISET(parents);
    borISetAdd(&parents, state->id);
    pddl_bdd_t *bdd = searchStateBDD(ss, search, state);
    bdd = pddlBDDClone(ss->ddm, bdd);

    pddl_symbolic_state_t *next = statesOpenPeek(&search->state);
    while (next != NULL && pddlCostCmp(&state->cost, &next->cost) == 0){
        ASSERT(borISetSize(&next->parent_ids) == 0);
        ASSERT(next->parent_id >= 0);

        next = statesNextOpen(&search->state);
        searchStateBDD(ss, search, next);
        pddl_bdd_t *nall = pddlBDDNot(ss->ddm, search->state.all_closed);
        pddlBDDAndUpdate(ss->ddm, &next->bdd, nall);
        pddlBDDDel(ss->ddm, nall);
        if (!pddlBDDIsFalse(ss->ddm, next->bdd)){
            pddlBDDOrUpdate(ss->ddm, &bdd, next->bdd);
            borISetAdd(&parents, next->id);
        }

        next = statesOpenPeek(&search->state);
    }

    if (borISetSize(&parents) > 1){
        pddl_symbolic_state_t *merged;
        merged = statesAddBDD(ss, &search->state, bdd);
        merged->parent_id = -2;
        merged->trans_id = -1;
        merged->cost = state->cost;
        borISetUnion(&merged->parent_ids, &parents);
        statesOpenState(ss, &search->state, merged);

        BOR_INFO(err, "%s: Merged %d states when preparing"
                      " next state (nodes: %d)",
                 (search->fw ? "fw" : "bw"),
                 borISetSize(&parents),
                 pddlBDDSize(bdd));
    }else{
        statesOpenState(ss, &search->state, state);
    }

    borISetFree(&parents);
    pddlBDDDel(ss->ddm, bdd);
}

static int searchStep(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      pddl_symbolic_search_t *other_search,
                      bor_err_t *err)
{
    bor_timer_t timer;
    borTimerStart(&timer);
    pddl_symbolic_state_t *state = statesNextOpen(&search->state);
    if (state == NULL){
        BOR_INFO(err, "%s: Plan does not exist",
                 (search->fw ? "fw" : "bw"));
        borTimerStop(&timer);
        return PDDL_SYMBOLIC_PLAN_NOT_EXIST;
    }

    BOR_INFO(err, "%s: step cost: %d:%d,"
                  " states: %d, closed states: %d,"
                  " cudd mem: %.2fMB, gc: %d",
             (search->fw ? "fw" : "bw"),
             state->cost.cost,
             state->cost.zero_cost,
             search->state.num_states,
             search->state.num_closed,
             pddlBDDMem(ss->ddm),
             pddlBDDGCUsed(ss->ddm));

    pddl_bdd_t *state_bdd = searchStateBDD(ss, search, state);
    if (pddlBDDIsFalse(ss->ddm, state_bdd)){
        BOR_INFO(err, "%s: State is empty", (search->fw ? "fw" : "bw"));
        return PDDL_SYMBOLIC_CONT;
    }

    if (other_search != NULL){
        checkGoal2(ss, search, other_search, state, err);

    }else{ // search->goal != NULL
        if (checkGoal(ss, search, state, err)){
            BOR_INFO(err, "%s: Found plan, cost: %d:%d, length: %d",
                     (search->fw ? "fw" : "bw"),
                     state->cost.cost,
                     state->cost.zero_cost,
                     borIArrSize(&search->plan));

            borTimerStop(&timer);
            searchSetNextStepEstimate(ss, search, state,
                                      borTimerElapsedInSF(&timer), err);
            return PDDL_SYMBOLIC_PLAN_FOUND;
        }
    }

    searchExpandState(ss, search, other_search, state, err);
    statesCloseState(ss, &search->state, state);
    borTimerStop(&timer);
    searchPrepareNext(ss, search, err);
    searchSetNextStepEstimate(ss, search, state,
                              borTimerElapsedInSF(&timer), err);
    return PDDL_SYMBOLIC_CONT;
}



static int selectMGroup(const pddl_mgroups_t *mgroup,
                        const bor_iset_t *mg_ids)
{
    int select = -1;
    int size = -1;
    int mgi;
    BOR_ISET_FOR_EACH(mg_ids, mgi){
        if (borISetSize(&mgroup->mgroup[mgi].mgroup) > size){
            size = borISetSize(&mgroup->mgroup[mgi].mgroup);
            select = mgi;
        }
    }
    return select;
}

static void groupMGroups(const pddl_symbolic_strips_t *strips,
                         const pddl_mgroups_t *mgroup,
                         int *ordering)
{
    int fact_size = strips->fact_size;
    bor_iset_t *fact_to_mgroup = BOR_CALLOC_ARR(bor_iset_t, fact_size);
    for (int mgi = 0; mgi < mgroup->mgroup_size; ++mgi){
        const bor_iset_t *mg = &mgroup->mgroup[mgi].mgroup;
        int fact;
        BOR_ISET_FOR_EACH(mg, fact)
            borISetAdd(fact_to_mgroup + fact, mgi);
    }

    int *in = BOR_ALLOC_ARR(int, fact_size);
    memcpy(in, ordering, sizeof(int) * fact_size);

    int start = 0;
    int ins = 0;
    while (start < fact_size){
        int fact = in[start];
        in[start] = -1;
        ordering[ins++] = fact;
        const bor_iset_t *mgs = fact_to_mgroup + fact;
        if (borISetSize(mgs) > 0){
            int select_mg = selectMGroup(mgroup, mgs);
            const bor_iset_t *mg = &mgroup->mgroup[select_mg].mgroup;
            for (int i = start + 1; i < fact_size; ++i){
                if (in[i] >= 0 && borISetIn(in[i], mg)){
                    ordering[ins++] = in[i];
                    in[i] = -1;
                }
            }
        }

        for (; start < fact_size && in[start] < 0; ++start);
    }

#ifdef PDDL_DEBUG
    BOR_ISET(facts);
    for (int i = 0; i < fact_size; ++i)
        borISetAdd(&facts, ordering[i]);
    ASSERT(borISetSize(&facts) == fact_size);
    ASSERT(borISetGet(&facts, 0) == 0);
    ASSERT(borISetGet(&facts, fact_size - 1) == fact_size - 1);
    borISetFree(&facts);
#endif

    BOR_FREE(in);
    for (int i = 0; i < fact_size; ++i)
        borISetFree(fact_to_mgroup + i);
    BOR_FREE(fact_to_mgroup);
}

static void setCGEdgesPreEff(const pddl_symbolic_strips_op_t *op,
                             pddl_scc_graph_t *graph)
{
    if (op->is_dead)
        return;

    int pfact;
    BOR_ISET_FOR_EACH(&op->pre, pfact){
        int efact;
        BOR_ISET_FOR_EACH(&op->add_eff, efact)
            pddlSCCGraphAddEdge(graph, pfact, efact);
    }
}

static void setCGEdgesEffEff(const pddl_symbolic_strips_op_t *op,
                             pddl_scc_graph_t *graph)
{
    if (op->is_dead)
        return;

    int add_eff_size = borISetSize(&op->add_eff);
    for (int i = 0; i < add_eff_size; ++i){
        int f1 = borISetGet(&op->add_eff, i);
        for (int j = i + 1; j < add_eff_size; ++j){
            int f2 = borISetGet(&op->add_eff, j);
            pddlSCCGraphAddEdge(graph, f1, f2);
            pddlSCCGraphAddEdge(graph, f2, f1);
        }
    }
}

static void graphInit(pddl_scc_graph_t *graph,
                      const pddl_symbolic_strips_t *strips)
{
    pddlSCCGraphInit(graph, strips->fact_size);

    for (int op_id = 0; op_id < strips->op_size; ++op_id){
        const pddl_symbolic_strips_op_t *op = strips->op + op_id;
        setCGEdgesPreEff(op, graph);
        setCGEdgesEffEff(op, graph);
    }
}

static void graphSCC(pddl_scc_graph_t *graph, int *fact_comp)
{
    pddl_scc_t scc;
    pddlSCC(&scc, graph);
    int id = 0;
    for (int i = scc.comp_size - 1; i >= 0; --i){
        int fact;
        BOR_ISET_FOR_EACH(&scc.comp[i], fact)
            fact_comp[fact] = id;
        ++id;
    }
    pddlSCCFree(&scc);
}

static int minDegreeNode(const int *indegree,
                         const int *fact_comp,
                         const pddl_scc_graph_t *graph)
{
    int min_degree = graph->node_size + 1;
    int min_degree_node = -1;
    int comp = INT_MAX;
    for (int ni = 0; ni < graph->node_size; ++ni){
        if (indegree[ni] > 0
                && (fact_comp[ni] < comp || indegree[ni] < min_degree)){
            min_degree = indegree[ni];
            min_degree_node = ni;
            comp = fact_comp[ni];
        }
    }
    return min_degree_node;
}

static void topologicalPseudoOrder(const pddl_scc_graph_t *graph,
                                   const int *fact_comp,
                                   int *order)
{
    int *indegree = BOR_CALLOC_ARR(int, graph->node_size);
    BOR_IARR(zero_indegree);
    for (int ni = 0; ni < graph->node_size; ++ni){
        int f;
        BOR_ISET_FOR_EACH(&graph->node[ni], f)
            indegree[f] += 1;
    }

    for (int ni = 0; ni < graph->node_size; ++ni){
        if (indegree[ni] == 0)
            borIArrAdd(&zero_indegree, ni);
    }

    if (borIArrSize(&zero_indegree) == 0){
        int min_node = minDegreeNode(indegree, fact_comp, graph);
        indegree[min_node] = 0;
        borIArrAdd(&zero_indegree, min_node);
    }


    int ins = 0;
    while (ins != graph->node_size){
        int node = borIArrPopLast(&zero_indegree);
        order[ins++] = node;
        int node2;
        BOR_ISET_FOR_EACH(&graph->node[node], node2){
            if (--indegree[node2] == 0)
                borIArrAdd(&zero_indegree, node2);
        }

        if (borIArrSize(&zero_indegree) == 0 && ins != graph->node_size){
            int min_node = minDegreeNode(indegree, fact_comp, graph);
            indegree[min_node] = 0;
            borIArrAdd(&zero_indegree, min_node);
        }
    }
    borIArrFree(&zero_indegree);
    BOR_FREE(indegree);
}

static void determineFactOrdering(const pddl_symbolic_strips_t *strips,
                                  const pddl_mgroups_t *mgroup,
                                  int *ordering,
                                  bor_err_t *err,
                                  const pddl_strips_t *s)
{
    pddl_scc_graph_t graph;
    graphInit(&graph, strips);

    int *fact_comp = BOR_CALLOC_ARR(int, strips->fact_size);
    graphSCC(&graph, fact_comp);

    topologicalPseudoOrder(&graph, fact_comp, ordering);

    pddl_mgroups_t mgs;
    pddlMGroupsInitEmpty(&mgs);
    pddlMGroupsExtractCoverEssential(mgroup, &mgs);
    groupMGroups(strips, &mgs, ordering);

    pddlMGroupsFree(&mgs);
    BOR_FREE(fact_comp);
    pddlSCCGraphFree(&graph);

    for (int i = 0; i < strips->fact_size; ++i){
        fprintf(stderr, "%d:(%s)\n", ordering[i],
                s->fact.fact[ordering[i]]->name);
    }
}

static void stripsInitOp(pddl_symbolic_strips_t *strips,
                         const pddl_strips_op_t *op_in,
                         pddl_symbolic_strips_op_t *op,
                         const pddl_symbolic_task_config_t *cfg,
                         bor_err_t *err)
{
    op->id = op_in->id;
    borISetUnion(&op->pre, &op_in->pre);
    borISetUnion(&op->add_eff, &op_in->add_eff);
    borISetUnion(&op->del_eff, &op_in->del_eff);
    pddlCostSetOp(&op->cost, op_in->cost);
    if (op_in->name != NULL)
        op->name = BOR_STRDUP(op_in->name);

    if (cfg->use_disambiguation && strips->disambiguate != NULL){
        // Disambiguate preconditions
        if (pddlDisambiguate(strips->disambiguate, &op->pre, NULL,
                             1, 0, NULL, &op->pre) < 0){
            BOR_INFO(err, "Operator %d:(%s) skipped, because it"
                          " is unreachable or dead-end", op->id, op->name);
            op->is_dead = 1;
            return;
        }
        borISetMinus(&op->add_eff, &op->pre);

        int fact_id;
        BOR_ISET_FOR_EACH(&op->pre, fact_id)
            borISetMinus(&op->del_eff, strips->fact_mutex + fact_id);
    }

    if (cfg->use_op_constr){
        int fact;

        BOR_ISET(fw_neg_pre);
        // Find negative preconditions
        BOR_ISET_FOR_EACH(&op->pre, fact){
            borISetUnion(&op->neg_pre, strips->fact_mutex_bw + fact);
            borISetUnion(&fw_neg_pre, strips->fact_mutex_fw + fact);
        }

        // E-delete facts that are mutex with the add effect
        BOR_ISET_FOR_EACH(&op->add_eff, fact)
            borISetUnion(&op->del_eff, strips->fact_mutex_fw + fact);
        borISetMinus(&op->del_eff, &fw_neg_pre);
        borISetMinus(&op->del_eff, &op->neg_pre);
        borISetFree(&fw_neg_pre);
    }

    borISetUnion2(&op->uncovered_eff, &op->add_eff, &op->del_eff);
    borISetMinus(&op->uncovered_eff, &op->pre);
    borISetMinus(&op->uncovered_eff, &op->neg_pre);

    if (!borISetIsDisjoint(&op->neg_pre, &op->pre)
            || !borISetIsDisjoint(&op->del_eff, &op->add_eff)){
        BOR_INFO(err, "Operator %d:(%s) skipped, because it"
                      " is unreachable or dead-end", op->id, op->name);
        op->is_dead = 1;
    }
}

static void stripsFreeOp(pddl_symbolic_strips_op_t *op)
{
    borISetFree(&op->pre);
    borISetFree(&op->neg_pre);
    borISetFree(&op->add_eff);
    borISetFree(&op->del_eff);
    borISetFree(&op->uncovered_eff);
    if (op->name != NULL)
        BOR_FREE(op->name);
}

static void stripsInit(pddl_symbolic_strips_t *strips,
                       const pddl_strips_t *strips_in,
                       const pddl_mgroups_t *mgroups,
                       const pddl_mutex_pairs_t *mutex,
                       const pddl_symbolic_task_config_t *cfg,
                       bor_err_t *err)
{
    int fact_size = strips_in->fact.fact_size;
    bzero(strips, sizeof(*strips));
    strips->fact_size = fact_size;

    pddlMGroupsInitCopy(&strips->mgroup, mgroups);
    strips->fact_mutex = BOR_CALLOC_ARR(bor_iset_t, fact_size);
    strips->fact_mutex_fw = BOR_CALLOC_ARR(bor_iset_t, fact_size);
    strips->fact_mutex_bw = BOR_CALLOC_ARR(bor_iset_t, fact_size);
    PDDL_MUTEX_PAIRS_FOR_EACH(mutex, f1, f2){
        borISetAdd(strips->fact_mutex + f1, f2);
        borISetAdd(strips->fact_mutex + f2, f1);
        if (pddlMutexPairsIsFwMutex(mutex, f1, f2)){
            borISetAdd(strips->fact_mutex_fw + f1, f2);
            borISetAdd(strips->fact_mutex_fw + f2, f1);
        }
        if (pddlMutexPairsIsBwMutex(mutex, f1, f2)){
            borISetAdd(strips->fact_mutex_bw + f1, f2);
            borISetAdd(strips->fact_mutex_bw + f2, f1);
        }
    }

    if (cfg->use_disambiguation){
        strips->disambiguate = BOR_ALLOC(pddl_disambiguate_t);
        if (pddlDisambiguateInit(strips->disambiguate, fact_size,
                                 mutex, mgroups) != 0){
            BOR_INFO2(err, "Disambiguation failed because there are"
                           " no exactly-1 mutex groups");
            BOR_FREE(strips->disambiguate);
            strips->disambiguate = NULL;
        }
        BOR_INFO2(err, "Disambiguation created.");
    }

    strips->op_size = strips_in->op.op_size;
    strips->op = BOR_CALLOC_ARR(pddl_symbolic_strips_op_t, strips->op_size);
    for (int op_id = 0; op_id < strips->op_size; ++op_id){
        stripsInitOp(strips, strips_in->op.op[op_id], strips->op + op_id,
                     cfg, err);
    }

    BOR_INFO2(err, "Operators prepared.");
}

static void stripsFree(pddl_symbolic_strips_t *strips)
{
    for (int i = 0; i < strips->op_size; ++i)
        stripsFreeOp(strips->op + i);
    BOR_FREE(strips->op);

    for (int i = 0; i < strips->fact_size; ++i){
        borISetFree(strips->fact_mutex + i);
        borISetFree(strips->fact_mutex_fw + i);
        borISetFree(strips->fact_mutex_bw + i);
    }
    BOR_FREE(strips->fact_mutex);
    BOR_FREE(strips->fact_mutex_fw);
    BOR_FREE(strips->fact_mutex_bw);

    pddlMGroupsFree(&strips->mgroup);

    if (strips->disambiguate != NULL){
        pddlDisambiguateFree(strips->disambiguate);
        BOR_FREE(strips->disambiguate);
    }
}

pddl_symbolic_task_t *pddlSymbolicTaskNew(const pddl_strips_t *strips,
                                          const pddl_mgroups_t *mgroups,
                                          const pddl_mutex_pairs_t *mutex,
                                          const pddl_symbolic_task_config_t *cfg,
                                          bor_err_t *err)
{
    if (strips->has_cond_eff){
        BOR_ERR_RET2(err, NULL, "Symbolic tasks does not support conditional"
                                " effects yet.");
    }

    BOR_INFO_PREFIX_PUSH(err, "symbolic: ");
    pddl_symbolic_task_t *ss;
    BOR_INFO(err, "Constructing symbolic task."
                  " max mem: %dMB,"
                  " merge max nodes: %lu,"
                  " merge max time: %.2fs",
             cfg->max_mem_in_mb,
             cfg->trans_merge_max_nodes,
             cfg->trans_merge_max_time);

    ss = BOR_ALLOC(pddl_symbolic_task_t);
    bzero(ss, sizeof(*ss));
    ss->cfg = *cfg;
    if (ss->cfg.use_op_constr){
        ss->cfg.use_constr = 0;
        ss->cfg.use_disambiguation = 1;
    }

    stripsInit(&ss->strips, strips, mgroups, mutex, cfg, err);
    ss->fact_size = strips->fact.fact_size;


    ss->ordered_facts = BOR_ALLOC_ARR(int, ss->fact_size);
    determineFactOrdering(&ss->strips, mgroups, ss->ordered_facts, err, strips);

    ss->fact_to_order = BOR_ALLOC_ARR(int, ss->fact_size);
    for (int i = 0; i < ss->fact_size; ++i)
        ss->fact_to_order[ss->ordered_facts[i]] = i;

    pddl_mgroups_t mgs;
    pddlMGroupsInitEmpty(&mgs);
    int *mg_used = BOR_CALLOC_ARR(int, mgroups->mgroup_size);
    for (int i = 0; i < ss->fact_size; ++i){
        int fact_id = ss->ordered_facts[i];
        for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
            if (borISetIn(fact_id, &mgroups->mgroup[mgi].mgroup)){
                if (!mg_used[mgi]){
                    pddl_mgroup_t *g;
                    g = pddlMGroupsAdd(&mgs, &mgroups->mgroup[mgi].mgroup);
                    g->is_exactly_one = mgroups->mgroup[mgi].is_exactly_one;
                    mg_used[mgi] = 1;
                }
                break;
            }
        }
    }
    ASSERT(mgroups->mgroup_size == mgs.mgroup_size);

    // TODO
    pddlSymbolicVarsInit(&ss->vars, strips->fact.fact_size, &mgs);

    BOR_FREE(mg_used);
    pddlMGroupsFree(&mgs);


#ifdef PDDL_DEBUG
    /* TODO
    for (int i = 0; i < ss->fact_size; ++i){
        ASSERT(ss->pre_fact_to_var[ss->ordered_facts[i]] == 2 * i);
        ASSERT(ss->eff_fact_to_var[ss->ordered_facts[i]] == 2 * i + 1);
    }
    */
#endif /* PDDL_DEBUG */

    BOR_INFO(err, "Prepared %d BDD variables covering %d facts",
             ss->vars.bdd_var_size, ss->fact_size);

    ss->ddm = pddlBDDManagerNew(ss->vars.bdd_var_size, cfg->cache_size);
    if (ss->ddm == NULL){
        pddlSymbolicTaskDel(ss);
        BOR_ERR_RET2(err, NULL, "Initialization of CUDD failed.");
    }
    //BOR_INFO(err, "CUDD initialized with slots: %u, cache size: %u, mem: %lu",
    //         num_slots, cache_size, mem);

    pddlSymbolicVarsInitBDD(ss->ddm, &ss->vars);

    transSetsInit(ss, strips, mutex, mgroups, &ss->trans, err);
    BOR_INFO2(err, "Transitions created.");
    pddlSymbolicConstrInit(&ss->constr, &ss->vars, mutex, mgroups,
                           ss->cfg.constr_max_nodes,
                           ss->cfg.constr_max_time,
                           err);
    BOR_INFO2(err, "Constraints created.");
    ss->init = pddlSymbolicVarsCreateState(&ss->vars, &strips->init);
    BOR_INFO2(err, "Initial state created.");
    ss->goal = pddlSymbolicVarsCreatePartialState(&ss->vars, &strips->goal);
    BOR_INFO2(err, "Goal state created.");

    BOR_INFO2(err, "Applying constraints on the goal ...");
    if (pddlSymbolicConstrApplyBwLimit(&ss->constr, &ss->goal,
                                       ss->cfg.goal_constr_max_time) == 0){
        BOR_INFO2(err, "Goal updated with constraints");
    }else{
        BOR_INFO2(err, "Applying constraints on the goal failed.");
        ss->goal_constr_failed = 1;
    }

    // TODO
    //ASSERT(Cudd_DebugCheck(ss->ddm) == 0);
    /*
    BOR_INFO(err, "Symbolic task created."
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
    */
    //Cudd_PrintInfo(ss->ddm, stderr);

    BOR_INFO_PREFIX_POP(err);
    return ss;
}

void pddlSymbolicTaskDel(pddl_symbolic_task_t *ss)
{
    stripsFree(&ss->strips);
    pddlSymbolicConstrFree(&ss->constr);
    transSetsFree(ss, &ss->trans);
    if (ss->ordered_facts != NULL)
        BOR_FREE(ss->ordered_facts);
    if (ss->fact_to_order != NULL)
        BOR_FREE(ss->fact_to_order);
    if (ss->init != NULL)
        pddlBDDDel(ss->ddm, ss->init);
    if (ss->goal != NULL)
        pddlBDDDel(ss->ddm, ss->goal);
    //Cudd_PrintInfo(ss->ddm, stderr);
    pddlSymbolicVarsFree(&ss->vars);
    if (ss->ddm != NULL)
        pddlBDDManagerDel(ss->ddm);
    BOR_FREE(ss);
}

int pddlSymbolicTaskGoalConstrFailed(const pddl_symbolic_task_t *task)
{
    return task->goal_constr_failed;
}

static int searchOneDir(pddl_symbolic_task_t *ss,
                        pddl_symbolic_search_t *search,
                        bor_err_t *err)
{
    int res = PDDL_SYMBOLIC_CONT;
    while (res == PDDL_SYMBOLIC_CONT){
        res = searchStep(ss, search, NULL, err);
    }
    return res;
}


int pddlSymbolicTaskSearchFw(pddl_symbolic_task_t *ss,
                             bor_iarr_t *plan,
                             bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "symbolic search fw: ");
    pddl_symbolic_search_t fw_search;
    searchInit(ss, &fw_search, 1, transSetImage, transSetPreImage,
               pddlSymbolicConstrApplyFw, ss->init, ss->goal);
    int res = searchOneDir(ss, &fw_search, err);
    borIArrAppendArr(plan, &fw_search.plan);
    searchFree(ss, &fw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "plan: (%s) ;; id=%d, cost %d",
                 ss->strips.op[op_id].name,
                 op_id,
                 ss->strips.op[op_id].cost);
    }
#endif /* PDDL_DEBUG */
    BOR_INFO_PREFIX_POP(err);
    return res;
}

int pddlSymbolicTaskSearchBw(pddl_symbolic_task_t *ss,
                             bor_iarr_t *plan,
                             bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "symbolic search bw: ");
    pddl_symbolic_search_t bw_search;
    searchInit(ss, &bw_search, 0, transSetPreImage, transSetImage,
               pddlSymbolicConstrApplyBw, ss->goal, ss->init);
    int res = searchOneDir(ss, &bw_search, err);
    borIArrAppendArr(plan, &bw_search.plan);
    searchFree(ss, &bw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "plan: (%s) ;; id=%d, cost %d",
                 ss->strips.op[op_id].name,
                 op_id,
                 ss->strips.op[op_id].cost);
    }
#endif /* PDDL_DEBUG */
    BOR_INFO_PREFIX_POP(err);
    return res;
}

static void fwbwExtractPlan(pddl_symbolic_task_t *ss,
                            pddl_symbolic_search_t *fw_search,
                            pddl_symbolic_search_t *bw_search,
                            bor_iarr_t *plan,
                            bor_err_t *err)
{
    const pddl_symbolic_state_t *fw_goal_state, *bw_goal_state;
    fw_goal_state = statesGet(&fw_search->state, fw_search->plan_goal_id);
    bw_goal_state = statesGet(&bw_search->state, bw_search->plan_goal_id);

    pddl_bdd_t *fw_goal_bdd = fw_goal_state->bdd;
    pddl_bdd_t *bw_goal_bdd = bw_goal_state->bdd;

    // Compute cut between forward and backward search frontier
    pddl_bdd_t *cut = pddlBDDAnd(ss->ddm, fw_goal_bdd, bw_goal_bdd);
    ASSERT_RUNTIME(!pddlBDDIsFalse(ss->ddm, cut));

    // We need to choose one particular state before extracting plans from
    // fw and bw searches
    BOR_ISET(cut_fact_state);
    pddl_bdd_t *cut_state = bddStateSelectOne(ss, cut, &cut_fact_state);
    borISetFree(&cut_fact_state);
    pddlBDDDel(ss->ddm, cut);

    // Extract forward plan from init to cut_state
    plan_t fw_plan;
    planInit(ss, fw_search, &fw_plan, fw_goal_state, cut_state);
    planExtractFw(&fw_plan, &ss->strips, &fw_search->plan);
    planFree(&fw_plan);

    // Extract backward plan from cut_state to goal
    plan_t bw_plan;
    planInit(ss, bw_search, &bw_plan, bw_goal_state, cut_state);
    planReverse(&bw_plan);
    planExtractFw(&bw_plan, &ss->strips, &bw_search->plan);
    planFree(&bw_plan);

    pddlBDDDel(ss->ddm, cut_state);

    // Join fw and bw plans
    int op_id;
    BOR_IARR_FOR_EACH(&fw_search->plan, op_id)
        borIArrAdd(plan, op_id);
    BOR_IARR_FOR_EACH(&bw_search->plan, op_id)
        borIArrAdd(plan, op_id);
}

int pddlSymbolicTaskSearchFwBw(pddl_symbolic_task_t *ss,
                               bor_iarr_t *plan,
                               bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "symbolic search fw+bw: ");
    BOR_INFO2(err, "start");
    int res = PDDL_SYMBOLIC_FAIL;
    pddl_symbolic_search_t fw_search;
    searchInit(ss, &fw_search, 1, transSetImage, transSetPreImage,
               pddlSymbolicConstrApplyFw, ss->init, ss->goal);
    BOR_INFO2(err, "fw-search created.");

    pddl_symbolic_search_t bw_search;
    searchInit(ss, &bw_search, 0, transSetPreImage, transSetImage,
               pddlSymbolicConstrApplyBw, ss->goal, ss->init);
    BOR_INFO2(err, "bw-search created.");

    pddl_cost_t zero_cost;
    pddlCostSetZero(&zero_cost);

    int fw_cont = searchStep(ss, &fw_search, &bw_search, err);
    int bw_cont = searchStep(ss, &bw_search, &fw_search, err);

    while (!borPairHeapEmpty(fw_search.state.open)
            && !borPairHeapEmpty(bw_search.state.open)){
        if (fw_cont != PDDL_SYMBOLIC_CONT && bw_cont != PDDL_SYMBOLIC_CONT)
            break;

        const pddl_cost_t *min_fw_cost = statesMinOpenCost(&fw_search.state);
        if (min_fw_cost == NULL)
            min_fw_cost = &zero_cost;
        const pddl_cost_t *min_bw_cost = statesMinOpenCost(&bw_search.state);
        if (min_bw_cost == NULL)
            min_bw_cost = &zero_cost;
        const pddl_cost_t *bound = &fw_search.state.bound;
        ASSERT(pddlCostCmp(bound, &bw_search.state.bound) == 0);
        if (pddlCostCmpSum(min_fw_cost, min_bw_cost, bound) >= 0)
            break;

        float fw_est = fw_search.next_step_estimate;
        float bw_est = bw_search.next_step_estimate;
        int fw_step = 0;
        if (fw_cont == PDDL_SYMBOLIC_CONT && fw_est <= bw_est)
            fw_step = 1;

        BOR_INFO(err, "fw est: %.2f, bw est: %.2f, fw open: %d:%d,"
                      " bw open: %d:%d, bound: %d:%d, use fw: %d",
                 fw_est, bw_est,
                 min_fw_cost->cost, min_fw_cost->zero_cost,
                 min_bw_cost->cost, min_bw_cost->zero_cost,
                 fw_search.state.bound.cost, fw_search.state.bound.zero_cost,
                 fw_step);
        if (fw_step){
            fw_cont = searchStep(ss, &fw_search, &bw_search, err);
        }else{
            bw_cont = searchStep(ss, &bw_search, &fw_search, err);
        }
    }
    ASSERT(pddlCostCmp(&fw_search.state.bound, &bw_search.state.bound) == 0);
    ASSERT(fw_search.plan_goal_id == bw_search.plan_other_goal_id);
    ASSERT(fw_search.plan_other_goal_id == bw_search.plan_goal_id);

    if (fw_search.plan_goal_id == -1){
        res = PDDL_SYMBOLIC_PLAN_NOT_EXIST;
    }else{
        res = PDDL_SYMBOLIC_PLAN_FOUND;
        fwbwExtractPlan(ss, &fw_search, &bw_search, plan, err);
        BOR_INFO(err, "Found plan, cost: %d:%d, length: %d",
                 fw_search.state.bound.cost,
                 fw_search.state.bound.zero_cost,
                 borIArrSize(plan));
    }

    searchFree(ss, &fw_search);
    searchFree(ss, &bw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "plan: (%s) ;; id=%d, cost %d",
                 ss->strips.op[op_id].name,
                 op_id,
                 ss->strips.op[op_id].cost);
    }
#endif /* PDDL_DEBUG */

    const char *res_str = "UNKOWN";
    switch (res){
        case PDDL_SYMBOLIC_CONT:
            res_str = "CONT";
            break;
        case PDDL_SYMBOLIC_PLAN_FOUND:
            res_str = "PLAN FOUND";
            break;
        case PDDL_SYMBOLIC_PLAN_NOT_EXIST:
            res_str = "PLAN NOT EXIST";
            break;
        case PDDL_SYMBOLIC_FAIL:
            res_str = "FAIL";
            break;
    }
    BOR_INFO(err, "DONE: %s", res_str);
    BOR_INFO_PREFIX_POP(err);
    return res;
}


int pddlSymbolicTaskCheckApplyFw(pddl_symbolic_task_t *ss,
                                 const bor_iset_t *state,
                                 const bor_iset_t *res_state,
                                 int op_id)
{
    int res = 1;
    pddl_bdd_t *bdd_state;
    bdd_state = pddlSymbolicVarsCreateState(&ss->vars, state);
    pddl_bdd_t *bdd_res_state;
    bdd_res_state = pddlSymbolicVarsCreateState(&ss->vars, res_state);
    for (int tri = 0; res && tri < ss->trans.trans_size; ++tri){
        pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
        if (!borISetIn(op_id, &trs->op))
            continue;

        pddl_bdd_t *next_states = transSetImage(ss, trs, bdd_state);
        pddlSymbolicConstrApplyFw(&ss->constr, &next_states);
        pddl_bdd_t *conj = pddlBDDAnd(ss->ddm, next_states, bdd_res_state);
        if (pddlBDDIsFalse(ss->ddm, conj)){
            res = 0;
        }
        pddlBDDDel(ss->ddm, conj);
        pddlBDDDel(ss->ddm, next_states);
    }
    pddlBDDDel(ss->ddm, bdd_state);
    pddlBDDDel(ss->ddm, bdd_res_state);

    return res;
}

int pddlSymbolicTaskCheckApplyBw(pddl_symbolic_task_t *ss,
                                 const bor_iset_t *state,
                                 const bor_iset_t *res_state,
                                 int op_id)
{
    int res = 1;
    pddl_bdd_t *bdd_state;
    bdd_state = pddlSymbolicVarsCreateState(&ss->vars, state);
    pddl_bdd_t *bdd_res_state;
    bdd_res_state = pddlSymbolicVarsCreateState(&ss->vars, res_state);
    for (int tri = 0; res && tri < ss->trans.trans_size; ++tri){
        pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
        if (!borISetIn(op_id, &trs->op))
            continue;

        pddl_bdd_t *next_states = transSetPreImage(ss, trs, bdd_state);
        pddlSymbolicConstrApplyBw(&ss->constr, &next_states);
        pddl_bdd_t *conj = pddlBDDAnd(ss->ddm, next_states, bdd_res_state);
        if (pddlBDDIsFalse(ss->ddm, conj)){
            res = 0;
        }
        pddlBDDDel(ss->ddm, conj);
        pddlBDDDel(ss->ddm, next_states);
    }
    pddlBDDDel(ss->ddm, bdd_state);
    pddlBDDDel(ss->ddm, bdd_res_state);

    return res;
}

int pddlSymbolicTaskCheckPlan(pddl_symbolic_task_t *ss,
                              const bor_iset_t *states,
                              const bor_iarr_t *op,
                              int plan_size)
{
    int res = 1;
    pddl_bdd_t **fw_node = BOR_ALLOC_ARR(pddl_bdd_t *, plan_size + 1);
    pddl_bdd_t **bw_node = BOR_ALLOC_ARR(pddl_bdd_t *, plan_size + 1);
    fw_node[0] = pddlBDDClone(ss->ddm, ss->init);
    bw_node[plan_size] = pddlBDDClone(ss->ddm, ss->goal);
    pddl_bdd_t *fw_closed = pddlBDDClone(ss->ddm, fw_node[0]);
    pddl_bdd_t *bw_closed = pddlBDDClone(ss->ddm, bw_node[plan_size]);
    for (int fi = 0; fi < plan_size; ++fi){
        int fw_op_id = borIArrGet(op, fi);
        for (int tri = 0; tri < ss->trans.trans_size; ++tri){
            pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
            if (!borISetIn(fw_op_id, &trs->op))
                continue;
            fw_node[fi + 1] = transSetImage(ss, trs, fw_node[fi]);
            if (ss->cfg.use_op_constr){
                pddl_bdd_t *tmp = pddlBDDClone(ss->ddm, fw_node[fi + 1]);
                pddlSymbolicConstrApplyFw(&ss->constr, &tmp);

                pddl_bdd_t *fwnot = pddlBDDNot(ss->ddm, fw_node[fi + 1]);
                pddl_bdd_t *diff;
                diff = pddlBDDAnd(ss->ddm, tmp, fwnot);
                //ASSERT(IS_FALSE(ss->ddm, diff));
                pddlBDDDel(ss->ddm, diff);
                pddlBDDDel(ss->ddm, fwnot);

                pddl_bdd_t *tmpnot = pddlBDDNot(ss->ddm, tmp);
                diff = pddlBDDAnd(ss->ddm, tmpnot, fw_node[fi + 1]);
                //ASSERT(IS_FALSE(ss->ddm, diff));
                pddlBDDDel(ss->ddm, diff);
                pddlBDDDel(ss->ddm, tmpnot);

                //if (tmp != fw_node[fi + 1])
                //    res = 0;
                //ASSERT(tmp == fw_node[fi + 1]);
                pddlBDDDel(ss->ddm, tmp);

            }else if (ss->cfg.use_constr){
                pddlSymbolicConstrApplyFw(&ss->constr, &fw_node[fi + 1]);
            }

            pddl_bdd_t *nclosed = pddlBDDNot(ss->ddm, fw_closed);
            pddlBDDAndUpdate(ss->ddm, &fw_node[fi + 1], nclosed);
            pddlBDDDel(ss->ddm, nclosed);
            pddlBDDOrUpdate(ss->ddm, &fw_closed, fw_node[fi + 1]);
        }

        int bw_op_id = borIArrGet(op, plan_size - fi - 1);
        for (int tri = 0; tri < ss->trans.trans_size; ++tri){
            pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
            if (!borISetIn(bw_op_id, &trs->op))
                continue;
            int fi2 = plan_size - fi;
            bw_node[fi2 - 1] = transSetPreImage(ss, trs, bw_node[fi2]);
            if (ss->cfg.use_op_constr){
                pddl_bdd_t *tmp = pddlBDDClone(ss->ddm, bw_node[fi2 - 1]);
                pddlSymbolicConstrApplyBw(&ss->constr, &tmp);
                //if (tmp != bw_node[fi2 - 1])
                //    res = 0;
                //ASSERT(tmp == bw_node[fi2 - 1]);
                pddlBDDDel(ss->ddm, tmp);

            }else if (ss->cfg.use_constr){
                pddlSymbolicConstrApplyBw(&ss->constr, &bw_node[fi2 - 1]);
            }
            pddl_bdd_t *nclosed = pddlBDDNot(ss->ddm, bw_closed);
            pddlBDDAndUpdate(ss->ddm, &bw_node[fi2 - 1], nclosed);
            pddlBDDDel(ss->ddm, nclosed);
            pddlBDDOrUpdate(ss->ddm, &bw_closed, bw_node[fi2 - 1]);
        }
    }

    for (int fi = 0; fi < plan_size + 1; ++fi){
        fprintf(stderr, "F %d\n", fi);
        pddl_bdd_t *conj = pddlBDDAnd(ss->ddm, fw_node[fi], bw_node[fi]);
        if (pddlBDDIsFalse(ss->ddm, conj)){
            fprintf(stderr, "A %d\n", fi);
            fflush(stderr);
            res = 0;
        }
        pddlBDDDel(ss->ddm, conj);

        conj = pddlBDDAnd(ss->ddm, bw_node[fi], fw_closed);
        if (pddlBDDIsFalse(ss->ddm, conj)){
            fprintf(stderr, "B %d\n", fi);
            fflush(stderr);
            res = 0;
        }
        pddlBDDDel(ss->ddm, conj);

        conj = pddlBDDAnd(ss->ddm, fw_node[fi], bw_closed);
        if (pddlBDDIsFalse(ss->ddm, conj)){
            fprintf(stderr, "C %d\n", fi);
            fflush(stderr);
            res = 0;
        }
        pddlBDDDel(ss->ddm, conj);
    }

    pddlBDDDel(ss->ddm, fw_closed);
    pddlBDDDel(ss->ddm, bw_closed);
    for (int fi = 0; fi < plan_size + 1; ++fi){
        pddlBDDDel(ss->ddm, fw_node[fi]);
        pddlBDDDel(ss->ddm, bw_node[fi]);
    }
    BOR_FREE(fw_node);
    BOR_FREE(bw_node);

    return res;
}
