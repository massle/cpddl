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
#include <boruvka/pairheap.h>

#include "pddl/symbolic_task.h"
#include "pddl/time_limit.h"
#include "pddl/disambiguation.h"
#include "assert.h"

struct pddl_cost {
    int cost;
    int zero_cost;
};
typedef struct pddl_cost pddl_cost_t;

static void pddlCostAdd(pddl_cost_t *c1, const pddl_cost_t *c2)
{
    c1->cost += c2->cost;
    c1->zero_cost += c2->zero_cost;
}

static void pddlCostAddOp(pddl_cost_t *c1, int op_cost)
{
    if (op_cost == 0){
        c1->zero_cost += 1;
    }else{
        c1->cost += op_cost;
    }
}

static void pddlCostSetZero(pddl_cost_t *c1)
{
    c1->cost = 0;
    c1->zero_cost = 0;
}

static void pddlCostSetInf(pddl_cost_t *c1)
{
    c1->cost = INT_MAX / 4;
    c1->zero_cost = INT_MAX / 4;
}

static int pddlCostCmp(const pddl_cost_t *c1, const pddl_cost_t *c2)
{
    int cmp = c1->cost - c2->cost;
    if (cmp == 0)
        cmp = c1->zero_cost - c2->zero_cost;
    return cmp;
}

static int pddlCostCmpSum(const pddl_cost_t *c1,
                          const pddl_cost_t *c2,
                          const pddl_cost_t *cs)
{
    int cmp = (c1->cost + c2->cost) - cs->cost;
    if (cmp == 0)
        cmp = (c1->zero_cost + c2->zero_cost) - cs->zero_cost;
    return cmp;
}

static int pddlCostCmpSumOp(const pddl_cost_t *c1,
                            int op_cost,
                            const pddl_cost_t *cs)
{
    int cmp = (c1->cost + op_cost) - cs->cost;
    if (cmp == 0)
        cmp = (c1->zero_cost + (op_cost == 0 ? 1 : 0)) - cs->zero_cost;
    return cmp;
}

struct pddl_symbolic_bdds {
    DdNode **bdd;
    int bdd_size;
    int bdd_alloc;
};
typedef struct pddl_symbolic_bdds pddl_symbolic_bdds_t;

struct pddl_symbolic_constr {
    pddl_symbolic_bdds_t fw_mutex;
    pddl_symbolic_bdds_t fw_mgroup;
    pddl_symbolic_bdds_t bw_mutex;
    pddl_symbolic_bdds_t bw_mgroup;
};
typedef struct pddl_symbolic_constr pddl_symbolic_constr_t;

typedef DdNode *(*constr_apply_fn)(pddl_symbolic_task_t *ss,
                                   pddl_symbolic_constr_t *constr,
                                   DdNode *bdd);


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
    int id; /*!< ID of this state */
    int parent_id; /*!< Parent state ID */
    int trans_id; /*!< ID of the transitions that achieved this state */
    pddl_cost_t cost; /*!< Cost of the state: g value + zero cost g value */
    // TODO: Add heuristic estimate
    DdNode *bdd; /*!< BDD representing the state */
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
    DdNode *all_closed; /*!< BDD representing all closed states */
    pddl_cost_t bound; /*!< Bound for the cost of the plan */
};
typedef struct pddl_symbolic_states pddl_symbolic_states_t;

typedef DdNode *(*trans_set_image_fn)(pddl_symbolic_task_t *ss,
                                      pddl_symbolic_trans_set_t *trset,
                                      DdNode *state);
struct pddl_symbolic_search {
    int fw; /*!< True if this is forward search */
    trans_set_image_fn image; /*!< Function constructing image */
    trans_set_image_fn pre_image; /*!< Function constructing pre-image */
    constr_apply_fn constr_apply; /*!< Function for applying constraints */
    pddl_symbolic_states_t state; /*!< State space */
    DdNode *goal; /*!< BDD describing the goal states */
    bor_iarr_t plan; /*!< Extracted plan */
    int plan_goal_id; /*!< This search's state where plan was reached */
    int plan_other_goal_id; /*!< Other search's state where plan was reached*/
    float next_step_estimate; /*!< Estimate of the duration of next step */
};
typedef struct pddl_symbolic_search pddl_symbolic_search_t;

struct pddl_symbolic_task {
    pddl_symbolic_task_config_t cfg; /*!< Configuration */
    DdManager *ddm; /*!< Cudd manager */
    const pddl_strips_t *strips; /*!< TODO */
    pddl_disambiguate_t *disambiguate;
    int fact_size; /*!< Number of facts in the problem */
    int *ordered_facts; /*!< Ordered facts */
    int *fact_to_order; /*!< Mapping from fact to its order index */
    int *pre_fact_to_var; /*!< Mapping from fact to pre BDD variable */
    int *eff_fact_to_var; /*!< Mapping from fact to eff BDD variable */
    int num_vars; /*!< Number of BDD variables */
    pddl_symbolic_trans_sets_t trans; /*!< BDD transitions */
    pddl_symbolic_constr_t constr; /*!< Constraints */
    DdNode *init; /*!< Initial state */
    DdNode *goal; /*!< Goal states */
};

// X = X and Y
#define BDD_AND(DDM, X, Y) \
    do { \
        DdNode *___res = Cudd_bddAnd((DDM), (X), (Y)); \
        Cudd_Ref(___res); \
        Cudd_RecursiveDeref((DDM), (X)); \
        (X) = ___res; \
    } while (0)

// X = X or Y
#define BDD_OR(DDM, X, Y) \
    do { \
        DdNode *___res = Cudd_bddOr((DDM), (X), (Y)); \
        Cudd_Ref(___res); \
        Cudd_RecursiveDeref((DDM), (X)); \
        (X) = ___res; \
    } while (0)

#define IS_FALSE(DDM, BDD) \
    ((BDD) == Cudd_ReadLogicZero(DDM))

#define DEREF(DDM, BDD) \
    Cudd_RecursiveDeref((DDM), (BDD))

static void separateFwBwMutex(const pddl_mutex_pairs_t *mutex,
                              pddl_mutex_pairs_t *fw_mutex,
                              pddl_mutex_pairs_t *bw_mutex)
{
    PDDL_MUTEX_PAIRS_FOR_EACH(mutex, f1, f2){
        if (pddlMutexPairsIsMutex(mutex, f1, f2)){
            if (pddlMutexPairsIsBwMutex(mutex, f1, f2)){
                pddlMutexPairsAdd(bw_mutex, f1, f2);
            }else if (pddlMutexPairsIsFwMutex(mutex, f1, f2)){
                pddlMutexPairsAdd(fw_mutex, f1, f2);
            }else{
                pddlMutexPairsAdd(bw_mutex, f1, f2);
                pddlMutexPairsAdd(fw_mutex, f1, f2);
            }
        }
    }
}

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
        BDD_AND(ss->ddm, bdd, var);
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
        BDD_AND(ss->ddm, bdd, var);
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

static void bddsInit(pddl_symbolic_bdds_t *bdds)
{
    bzero(bdds, sizeof(*bdds));
}

static void bddsFree(pddl_symbolic_task_t *ss,
                     pddl_symbolic_bdds_t *bdds)
{
    for (int i = 0; i < bdds->bdd_size; ++i)
        DEREF(ss->ddm, bdds->bdd[i]);
    if (bdds->bdd != NULL)
        BOR_FREE(bdds->bdd);
}

static void bddsAdd(pddl_symbolic_task_t *ss,
                    pddl_symbolic_bdds_t *bdds,
                    DdNode *bdd)
{
    if (bdds->bdd_size == bdds->bdd_alloc){
        if (bdds->bdd_alloc == 0)
            bdds->bdd_alloc = 8;
        bdds->bdd_alloc *= 2;
        bdds->bdd = BOR_REALLOC_ARR(bdds->bdd, DdNode *, bdds->bdd_alloc);
    }
    bdds->bdd[bdds->bdd_size++] = bdd;
    Cudd_Ref(bdds->bdd[bdds->bdd_size - 1]);
}

static void bddsAddMutex(pddl_symbolic_task_t *ss,
                         pddl_symbolic_bdds_t *bdds,
                         int fact1,
                         int fact2)
{
    DdNode *var1 = Cudd_bddIthVar(ss->ddm, ss->pre_fact_to_var[fact1]);
    Cudd_Ref(var1);
    DdNode *var2 = Cudd_bddIthVar(ss->ddm, ss->pre_fact_to_var[fact2]);
    Cudd_Ref(var2);
    DdNode *bdd = Cudd_bddOr(ss->ddm, Cudd_Not(var1), Cudd_Not(var2));
    bddsAdd(ss, bdds, bdd);
    DEREF(ss->ddm, var1);
    DEREF(ss->ddm, var2);
}

static void bddsAddExactlyOneMGroup(pddl_symbolic_task_t *ss,
                                    pddl_symbolic_bdds_t *bdds,
                                    const bor_iset_t *mgroup)
{
    DdNode *bdd = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(bdd);
    int fact_id;
    BOR_ISET_FOR_EACH(mgroup, fact_id){
        DdNode *var1 = Cudd_bddIthVar(ss->ddm, ss->pre_fact_to_var[fact_id]);
        BDD_OR(ss->ddm, bdd, var1);
    }
    bddsAdd(ss, bdds, bdd);
    DEREF(ss->ddm, bdd);
}

static void bddsMergeAnd(pddl_symbolic_task_t *ss,
                         pddl_symbolic_bdds_t *bdds,
                         int max_nodes,
                         float max_time)
{
    if (bdds->bdd_size == 0)
        return;

    DdNode **bdd = BOR_CALLOC_ARR(DdNode *, bdds->bdd_size);
    int bdd_size = bdds->bdd_size;
    memcpy(bdd, bdds->bdd, sizeof(DdNode *) * bdd_size);
    bdds->bdd_size = 0;

    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    pddlTimeLimitSet(&time_limit, max_time);
    while (bdd_size > 1){
        if (pddlTimeLimitCheck(&time_limit) < 0)
            break;

        int ins = 0;
        for (int i = 0; i < bdd_size; i = i + 2){
            if (i + 1 >= bdd_size){
                bdd[ins++] = bdd[i];
                continue;
            }

            DdNode *bdd1 = bdd[i];
            DdNode *bdd2 = bdd[i + 1];
            if (bdd1 == NULL && bdd2 == NULL){
                bdd[ins] = NULL;

            }else if (bdd1 == NULL){
                bdd[ins] = bdd2;

            }else if (bdd2 == NULL){
                bdd[ins] = bdd1;

            }else{
                DdNode *res = Cudd_bddAndLimit(ss->ddm, bdd1, bdd2, max_nodes);
                if (res != NULL){
                    Cudd_Ref(res);
                    bdd[ins] = res;
                }else{
                    bddsAdd(ss, bdds, bdd1);
                    bddsAdd(ss, bdds, bdd2);
                    bdd[ins] = NULL;
                }
                DEREF(ss->ddm, bdd1);
                DEREF(ss->ddm, bdd2);
            }
            ++ins;
        }
        bdd_size = ins;
    }

    for (int i = 0; i < bdd_size; ++i){
        if (bdd[i] != NULL){
            bddsAdd(ss, bdds, bdd[i]);
            DEREF(ss->ddm, bdd[i]);
        }
    }

    BOR_FREE(bdd);
}

static DdNode *bddsAnd(pddl_symbolic_task_t *ss,
                       pddl_symbolic_bdds_t *bdds,
                       DdNode *bdd)
{
    for (int i = 0; i < bdds->bdd_size; ++i)
        BDD_AND(ss->ddm, bdd, bdds->bdd[i]);
    return bdd;
}


static int constrConstructMutex(pddl_symbolic_task_t *ss,
                                pddl_symbolic_bdds_t *bdds,
                                const pddl_mutex_pairs_t *mutex)
{
    int num_mutexes = 0;
    for (int fact1 = 0; fact1 < ss->fact_size; ++fact1){
        for (int fact2 = fact1 + 1; fact2 < ss->fact_size; ++fact2){
            if (pddlMutexPairsIsMutex(mutex, fact1, fact2)){
                bddsAddMutex(ss, bdds, fact1, fact2);
                ++num_mutexes;
            }
        }
    }

    bddsMergeAnd(ss, bdds, ss->cfg.constr_max_nodes, ss->cfg.constr_max_time);
    return num_mutexes;
}

static int constrConstructFwMGroup(pddl_symbolic_task_t *ss,
                                   pddl_symbolic_bdds_t *bdds,
                                   const pddl_mgroups_t *mgroup)
{
    int num_mgroups = 0;
    for (int mgi = 0; mgi < mgroup->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mgroup->mgroup + mgi;
        if (mg->is_fam_group && mg->is_goal){
            bddsAddExactlyOneMGroup(ss, bdds, &mg->mgroup);
            ++num_mgroups;
        }
    }

    bddsMergeAnd(ss, bdds, ss->cfg.constr_max_nodes, ss->cfg.constr_max_time);
    return num_mgroups;
}

static int constrConstructBwMGroup(pddl_symbolic_task_t *ss,
                                   pddl_symbolic_bdds_t *bdds,
                                   const pddl_mgroups_t *mgroup)
{
    int num_mgroups = 0;
    for (int mgi = 0; mgi < mgroup->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mgroup->mgroup + mgi;
        if (mg->is_exactly_one){
            bddsAddExactlyOneMGroup(ss, bdds, &mg->mgroup);
            ++num_mgroups;
        }
    }

    bddsMergeAnd(ss, bdds, ss->cfg.constr_max_nodes, ss->cfg.constr_max_time);
    return num_mgroups;
}

static void constrInit(pddl_symbolic_task_t *ss,
                       pddl_symbolic_constr_t *constr,
                       const pddl_mutex_pairs_t *mutex,
                       const pddl_mgroups_t *mgroup,
                       bor_err_t *err)
{
    bddsInit(&constr->fw_mutex);
    bddsInit(&constr->fw_mgroup);
    bddsInit(&constr->bw_mutex);
    bddsInit(&constr->bw_mgroup);

    if (!ss->cfg.use_constr)
        return;

    pddl_mutex_pairs_t fw_mutex;
    pddl_mutex_pairs_t bw_mutex;
    pddlMutexPairsInit(&fw_mutex, ss->fact_size);
    pddlMutexPairsInit(&bw_mutex, ss->fact_size);
    separateFwBwMutex(mutex, &fw_mutex, &bw_mutex);

    BOR_INFO2(err, "symbolic: Constructing constraint BDDs ...");

    if (bw_mutex.num_mutex_pairs > 0){
        int num = constrConstructMutex(ss, &constr->fw_mutex, &bw_mutex);
        BOR_INFO(err, "symbolic: Created %d fw-mutex BDDs from %d mutexes",
                 constr->fw_mutex.bdd_size, num);
    }

    if (fw_mutex.num_mutex_pairs > 0){
        int num = constrConstructMutex(ss, &constr->bw_mutex, &fw_mutex);
        BOR_INFO(err, "symbolic: Created %d bw-mutex BDDs from %d mutexes",
                 constr->bw_mutex.bdd_size, num);
    }

    if (mgroup != NULL){
        int num_fw = constrConstructFwMGroup(ss, &constr->fw_mgroup, mgroup);
        BOR_INFO(err, "symbolic: Created %d fw-mgroup BDDs from %d mgroups",
                 constr->fw_mgroup.bdd_size, num_fw);
        int num_bw = constrConstructBwMGroup(ss, &constr->bw_mgroup, mgroup);
        BOR_INFO(err, "symbolic: Created %d bw-mgroup BDDs from %d mgroups",
                 constr->bw_mgroup.bdd_size, num_bw);
    }

    pddlMutexPairsFree(&fw_mutex);
    pddlMutexPairsFree(&bw_mutex);
}

static void constrFree(pddl_symbolic_task_t *ss,
                       pddl_symbolic_constr_t *constr)
{
    bddsFree(ss, &constr->fw_mutex);
    bddsFree(ss, &constr->fw_mgroup);
    bddsFree(ss, &constr->bw_mutex);
    bddsFree(ss, &constr->bw_mgroup);
}

static DdNode *constrApplyFw(pddl_symbolic_task_t *ss,
                             pddl_symbolic_constr_t *constr,
                             DdNode *bdd)
{
    bdd = bddsAnd(ss, &constr->fw_mutex, bdd);
    return bddsAnd(ss, &constr->fw_mgroup, bdd);
}

static DdNode *constrApplyBw(pddl_symbolic_task_t *ss,
                             pddl_symbolic_constr_t *constr,
                             DdNode *bdd)
{
    bdd = bddsAnd(ss, &constr->bw_mutex, bdd);
    return bddsAnd(ss, &constr->bw_mgroup, bdd);
}


static void transFree(pddl_symbolic_task_t *ss,
                      pddl_symbolic_trans_t *tr)
{
    //if (tr->bdd == NULL)
    //    return;
    DEREF(ss->ddm, tr->bdd);
    for (int i = 0; i < tr->var_size; ++i){
        DEREF(ss->ddm, tr->var_pre[i]);
        DEREF(ss->ddm, tr->var_eff[i]);
    }
    if (tr->var_pre != NULL)
        BOR_FREE(tr->var_pre);
    if (tr->var_eff != NULL)
        BOR_FREE(tr->var_eff);
    DEREF(ss->ddm, tr->exist_pre);
    DEREF(ss->ddm, tr->exist_eff);
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
        BDD_AND(ss->ddm, tr->exist_pre, tr->var_pre[i]);
        BDD_AND(ss->ddm, tr->exist_eff, tr->var_eff[i]);
    }
    */
}

static void transInit(pddl_symbolic_task_t *ss,
                      const pddl_strips_op_t *_op,
                      const pddl_mutex_pairs_t *mutex,
                      pddl_symbolic_trans_t *tr,
                      bor_err_t *err)
{
    pddl_strips_op_t op;
    pddlStripsOpInit(&op);
    pddlStripsOpCopy(&op, _op);

    // TODO: Configure
    if (ss->disambiguate != NULL){
        // Disambiguate preconditions
        if (pddlDisambiguate(ss->disambiguate, &op.pre, NULL,
                             1, 0, NULL, &op.pre) < 0){
            BOR_INFO(err, "symbolic: Operator %d:(%s) skipped, because it"
                          " is unreachable", op.id, op.name);
            // Skip unreachable operators
            pddlStripsOpFree(&op);
            return;
        }
        borISetMinus(&op.add_eff, &op.pre);
    }

    // TODO: Speed-up looking for mutexes
    // TODO: Configure
    // Find negative preconditions
    BOR_ISET(neg_pre);
    for (int fact = 0; fact < ss->fact_size; ++fact){
        if (pddlMutexPairsIsMutexFactSet(mutex, fact, &op.pre))
            borISetAdd(&neg_pre, fact);
    }
    borISetMinus(&op.del_eff, &neg_pre);

    // TODO: Configure
    // E-delete facts that are mutex with the add effect
    for (int fact = 0; fact < ss->fact_size; ++fact){
        if (pddlMutexPairsIsMutexFactSet(mutex, fact, &op.add_eff))
            borISetAdd(&op.del_eff, fact);
    }

    tr->bdd = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(tr->bdd);

    int fact_id;
    BOR_ISET_FOR_EACH(&op.pre, fact_id){
        int var_id = ss->pre_fact_to_var[fact_id];
        BDD_AND(ss->ddm, tr->bdd, Cudd_bddIthVar(ss->ddm, var_id));
    }

    BOR_ISET_FOR_EACH(&neg_pre, fact_id){
        int var_id = ss->pre_fact_to_var[fact_id];
        BDD_AND(ss->ddm, tr->bdd, Cudd_Not(Cudd_bddIthVar(ss->ddm, var_id)));
    }

    BOR_ISET_FOR_EACH(&op.del_eff, fact_id){
        int var_id = ss->eff_fact_to_var[fact_id];
        BDD_AND(ss->ddm, tr->bdd, Cudd_Not(Cudd_bddIthVar(ss->ddm, var_id)));
    }

    BOR_ISET_FOR_EACH(&op.add_eff, fact_id){
        int var_id = ss->eff_fact_to_var[fact_id];
        BDD_AND(ss->ddm, tr->bdd, Cudd_bddIthVar(ss->ddm, var_id));
    }

    borISetUnion2(&tr->eff_facts, &op.add_eff, &op.del_eff);
    transInitEffVars(ss, tr);

    borISetFree(&neg_pre);
    pddlStripsOpFree(&op);
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
            BDD_AND(ss->ddm, bdd1, biimp);
            DEREF(ss->ddm, biimp);
        }

        if (e2 < esize2 && borISetGet(&tr2->eff_facts, e2) == fact_id){
            ++e2;
        }else{
            DdNode *biimp = createBiimpFact(ss, fact_id);
            BDD_AND(ss->ddm, bdd2, biimp);
            DEREF(ss->ddm, biimp);
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
    DEREF(ss->ddm, bdd1);
    DEREF(ss->ddm, bdd2);
    if (dst->bdd == NULL){
        borISetFree(&dst->eff_facts);
        return -1;
    }

    transInitEffVars(ss, dst);

    return 0;
}

static void transSetsAddRange(pddl_symbolic_task_t *ss,
                              const pddl_strips_t *strips,
                              const pddl_mutex_pairs_t *mutex,
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
        transInit(ss, strips->op.op[op_ids[i]], mutex, T + i, err); // TODO

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
                          const pddl_strips_t *strips,
                          const pddl_mutex_pairs_t *mutex,
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
            transSetsAddRange(ss, strips, mutex, trset->trans + tr_id,
                              op_ids + start, end - start, err);
            ++tr_id;
            start = end;
        }
    }
    if (end > start){
        transSetsAddRange(ss, strips, mutex, trset->trans + tr_id,
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
    DdNode *bdd1, *bdd;
    bdd1 = Cudd_bddAndAbstract(ss->ddm, state, tr->bdd, tr->exist_pre);
    Cudd_Ref(bdd1);
    bdd = Cudd_bddSwapVariables(ss->ddm, bdd1,
                                tr->var_pre, tr->var_eff, tr->var_size);
    Cudd_Ref(bdd);
    DEREF(ss->ddm, bdd1);
    return bdd;
}

static DdNode *transPreImage(pddl_symbolic_task_t *ss,
                          pddl_symbolic_trans_t *tr,
                          DdNode *state)
{
    DdNode *bdd1, *bdd;
    bdd1 = Cudd_bddSwapVariables(ss->ddm, state,
                                 tr->var_eff, tr->var_pre, tr->var_size);
    Cudd_Ref(bdd1);
    bdd = Cudd_bddAndAbstract(ss->ddm, bdd1, tr->bdd, tr->exist_eff);
    Cudd_Ref(bdd);
    DEREF(ss->ddm, bdd1);
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
        BDD_OR(ss->ddm, bdd, bdd2);
        DEREF(ss->ddm, bdd2);
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
    if (state->bdd != NULL)
        DEREF(ss->ddm, state->bdd);
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
    o1 = bor_container_of(n1, pddl_symbolic_state_t, heap);
    o2 = bor_container_of(n2, pddl_symbolic_state_t, heap);
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

    states->all_closed = Cudd_ReadLogicZero(ss->ddm);
    Cudd_Ref(states->all_closed);

    pddlCostSetInf(&states->bound);
}

static void statesFree(pddl_symbolic_task_t *ss, pddl_symbolic_states_t *states)
{
    borPairHeapDel(states->open_cost);
    borPairHeapDel(states->open);

    DEREF(ss->ddm, states->all_closed);

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
    BDD_OR(ss->ddm, states->all_closed, state->bdd);
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

static pddl_symbolic_state_t *statesOpenPeak(pddl_symbolic_states_t *states)
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
                                           DdNode *bdd)
{
    pddl_symbolic_state_t *state = statesAdd(ss, states);
    if (bdd != NULL){
        state->bdd = bdd;
        Cudd_Ref(state->bdd);
    }
    return state;
}

static void statesAddInit(pddl_symbolic_task_t *ss,
                          pddl_symbolic_states_t *states,
                          DdNode *bdd)
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
                       constr_apply_fn constr_apply,
                       DdNode *init,
                       DdNode *goal)
{
    bzero(search, sizeof(*search));
    search->fw = fw;
    search->image = image;
    search->pre_image = pre_image;
    search->constr_apply = constr_apply;
    statesInit(ss, &search->state);
    search->goal = goal;
    if (search->goal != NULL)
        Cudd_Ref(search->goal);

    statesAddInit(ss, &search->state, init);

    search->plan_goal_id = -1;
    search->plan_other_goal_id = -1;
}

static void searchFree(pddl_symbolic_task_t *ss,
                       pddl_symbolic_search_t *search)
{
    statesFree(ss, &search->state);
    if (search->goal != NULL)
        DEREF(ss->ddm, search->goal);
    borIArrFree(&search->plan);
}

static DdNode *bddStateSelectOne(pddl_symbolic_task_t *ss,
                                 DdNode *bdd,
                                 bor_iset_t *state)
{
    borISetEmpty(state);
    char *cube = BOR_ALLOC_ARR(char, ss->num_vars);
    Cudd_bddPickOneCube(ss->ddm, bdd, cube);
    for (int fi = 0; fi < ss->fact_size; ++fi){
        if (cube[ss->pre_fact_to_var[fi]] == 1)
            borISetAdd(state, fi);
    }
    BOR_FREE(cube);
    return createState(ss, state);
}

struct plan {
    int plan_len;
    bor_iset_t *state;
    bor_iset_t **tr_op;
};
typedef struct plan plan_t;

static void planInit(pddl_symbolic_task_t *ss,
                     pddl_symbolic_search_t *search,
                     plan_t *plan,
                     const pddl_symbolic_state_t *goal_state,
                     DdNode *reached_goal)
{
    bzero(plan, sizeof(*plan));

    // Find out the length of the plan
    const pddl_symbolic_state_t *state = goal_state;
    plan->plan_len = 0;
    while (state->parent_id >= 0){
        ++plan->plan_len;
        state = statesGet(&search->state, state->parent_id);
    }

    plan->state = BOR_CALLOC_ARR(bor_iset_t, plan->plan_len + 1);
    plan->tr_op = BOR_CALLOC_ARR(bor_iset_t *, plan->plan_len);

    // Backtrack from the goal_state and extract one particular state at
    // each step.
    // Select one specific state -- it doesn't matter which one
    DdNode *bdd = bddStateSelectOne(ss, reached_goal,
                                    plan->state + plan->plan_len);
    state = goal_state;
    for (int si = plan->plan_len - 1; state->parent_id >= 0; --si){
        plan->tr_op[si] = &ss->trans.trans[state->trans_id].op;
        const pddl_symbolic_state_t *prev_state;
        prev_state = statesGet(&search->state, state->parent_id);

        // This step of the plan goes from prev_state to state.
        // So, compute the conjuction of the preimage of state and
        // state_prev.
        pddl_symbolic_trans_set_t *trset = ss->trans.trans + state->trans_id;
        DdNode *preimg = search->pre_image(ss, trset, bdd);
        ASSERT_RUNTIME(!IS_FALSE(ss->ddm, preimg));
        BDD_AND(ss->ddm, preimg, prev_state->bdd);

        // Select one of the states -- again, it doesn't matter which one
        DEREF(ss->ddm, bdd);
        bdd = bddStateSelectOne(ss, preimg, plan->state + si);
        DEREF(ss->ddm, preimg);
        state = prev_state;
    }
    DEREF(ss->ddm, bdd);
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
                          const pddl_strips_t *strips,
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
            const pddl_strips_op_t *op = strips->op.op[op_id];
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


static DdNode *searchStateBDD(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              pddl_symbolic_state_t *state)
{
    if (state->bdd == NULL){
        const pddl_symbolic_state_t *prev_state;
        prev_state = statesGet(&search->state, state->parent_id);
        state->bdd = search->image(ss, ss->trans.trans + state->trans_id,
                                   prev_state->bdd);
        BDD_AND(ss->ddm, state->bdd, Cudd_Not(search->state.all_closed));
        state->bdd = search->constr_apply(ss, &ss->constr, state->bdd);
    }
    return state->bdd;
}

static int searchNextOpenSize(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search)
{
    pddl_symbolic_state_t *state = statesOpenPeak(&search->state);
    if (state == NULL)
        return 0;
    DdNode *bdd = searchStateBDD(ss, search, state);
    return Cudd_DagSize(bdd);
}

static void searchExpandState(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              pddl_symbolic_state_t *state_in)
{
    pddl_symbolic_states_t *states = &search->state;
    DdNode *bdd_in = state_in->bdd;
    Cudd_Ref(bdd_in);
    ASSERT(bdd_in != NULL);
    BDD_AND(ss->ddm, bdd_in, Cudd_Not(states->all_closed));

    if (IS_FALSE(ss->ddm, bdd_in)){
        DEREF(ss->ddm, bdd_in);
        return;
    }

    for (int tri = 0; tri < ss->trans.trans_size; ++tri){
        int tr_cost = ss->trans.trans[tri].cost;
        if (pddlCostCmpSumOp(&state_in->cost, tr_cost, &states->bound) >= 0)
            continue;

        pddl_symbolic_state_t *state = statesAdd(ss, states);
        state->parent_id = state_in->id;
        state->trans_id = tri;
        state->cost = state_in->cost;
        pddlCostAddOp(&state->cost, tr_cost);

        statesOpenState(ss, states, state);
    }

    DEREF(ss->ddm, bdd_in);
}



static int checkGoal(pddl_symbolic_task_t *ss,
                     pddl_symbolic_search_t *search,
                     const pddl_symbolic_state_t *state,
                     bor_err_t *err)
{
    DdNode *goal = Cudd_bddAnd(ss->ddm, state->bdd, search->goal);
    Cudd_Ref(goal);
    if (!IS_FALSE(ss->ddm, goal)){
        plan_t plan;
        planInit(ss, search, &plan, state, goal);
        if (!search->fw)
            planReverse(&plan);
        planExtractFw(&plan, ss->strips, &search->plan);
        planFree(&plan);
        DEREF(ss->ddm, goal);
        return 1;
    }
    DEREF(ss->ddm, goal);
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
    pddlCostAdd(&search->state.bound, &s2->cost);
    search->plan_goal_id = s1->id;
    search->plan_other_goal_id = s2->id;
}

static int checkGoal2(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      pddl_symbolic_search_t *other_search,
                      const pddl_symbolic_state_t *state,
                      bor_err_t *err)
{
    int res = 0;
    DdNode *goal = Cudd_bddAnd(ss->ddm, state->bdd,
                               other_search->state.all_closed);
    Cudd_Ref(goal);
    if (!IS_FALSE(ss->ddm, goal)){
        for (int si = 0; si < other_search->state.num_closed; ++si){
            const pddl_symbolic_state_t *closed_state;
            closed_state = statesGetClosed(&other_search->state, si);
            if (!costStatesIsBetter(search, state, closed_state))
                break;

            DdNode *goal = Cudd_bddAnd(ss->ddm, state->bdd, closed_state->bdd);
            Cudd_Ref(goal);
            if (!IS_FALSE(ss->ddm, goal)){
                searchSetBestPlan(search, state, closed_state);
                searchSetBestPlan(other_search, closed_state, state);
                BOR_INFO(err, "symbolic search %s: Found best plan so far:"
                              " cost: %d:%d",
                         (search->fw ? "fw" : "bw"),
                         search->state.bound.cost,
                         search->state.bound.zero_cost);
                res = 1;
            }
            DEREF(ss->ddm, goal);

            if (res)
                break;
        }
    }
    DEREF(ss->ddm, goal);
    return res;
}

static void searchSetNextStepEstimate(pddl_symbolic_task_t *ss,
                                      pddl_symbolic_search_t *search,
                                      pddl_symbolic_state_t *state,
                                      float cur_time)
{
    DdNode *state_bdd = searchStateBDD(ss, search, state);
    long bdd_size = Cudd_DagSize(state_bdd);
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

static int searchStep(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      pddl_symbolic_search_t *other_search,
                      bor_err_t *err)
{
    bor_timer_t timer;
    borTimerStart(&timer);
    pddl_symbolic_state_t *state = statesNextOpen(&search->state);
    if (state == NULL){
        BOR_INFO(err, "symbolic search %s: Plan does not exist",
                 (search->fw ? "fw" : "bw"));
        borTimerStop(&timer);
        return PDDL_SYMBOLIC_PLAN_NOT_EXIST;
    }

    BOR_INFO(err, "symbolic search %s: step cost: %d:%d,"
                  " states: %d, closed states: %d,"
                  " cudd mem: %.2fMB, live nodes: %d, gc: %d",
             (search->fw ? "fw" : "bw"),
             state->cost.cost,
             state->cost.zero_cost,
             search->state.num_states,
             search->state.num_closed,
             Cudd_ReadMemoryInUse(ss->ddm) / (1024. * 1024.),
             Cudd_ReadPeakLiveNodeCount(ss->ddm),
             Cudd_ReadGarbageCollections(ss->ddm));

    DdNode *state_bdd = searchStateBDD(ss, search, state);
    if (IS_FALSE(ss->ddm, state_bdd)){
        BOR_INFO(err, "symbolic search %s: State is empty",
                 (search->fw ? "fw" : "bw"));
        return PDDL_SYMBOLIC_CONT;
    }

    if (other_search != NULL){
        checkGoal2(ss, search, other_search, state, err);

    }else{ // search->goal != NULL
        if (checkGoal(ss, search, state, err)){
            BOR_INFO(err, "symbolic search %s: Found plan, cost: %d:%d,"
                          " length: %d",
                     (search->fw ? "fw" : "bw"),
                     state->cost.cost,
                     state->cost.zero_cost,
                     borIArrSize(&search->plan));

            borTimerStop(&timer);
            searchSetNextStepEstimate(ss, search, state,
                                      borTimerElapsedInSF(&timer));
            return PDDL_SYMBOLIC_PLAN_FOUND;
        }
    }

    searchExpandState(ss, search, state);
    statesCloseState(ss, &search->state, state);
    borTimerStop(&timer);
    searchSetNextStepEstimate(ss, search, state, borTimerElapsedInSF(&timer));
    return PDDL_SYMBOLIC_CONT;
}


struct graph {
    bor_iset_t *out;
    bor_iset_t *in;
    int node_size;
};
typedef struct graph graph_t;

static void setCGEdgesPreEff(const pddl_strips_op_t *op, graph_t *graph)
{
    int pfact;
    BOR_ISET_FOR_EACH(&op->pre, pfact){
        int efact;
        BOR_ISET_FOR_EACH(&op->add_eff, efact){
            borISetAdd(&graph->in[efact], pfact);
            borISetAdd(&graph->out[pfact], efact);
        }
        BOR_ISET_FOR_EACH(&op->del_eff, efact){
            borISetAdd(&graph->in[efact], pfact);
            borISetAdd(&graph->out[pfact], efact);
        }
    }
}

static void setCGEdgesEffEff(const pddl_strips_op_t *op, graph_t *graph)
{
    int add_eff_size = borISetSize(&op->add_eff);
    int del_eff_size = borISetSize(&op->del_eff);
    for (int i = 0; i < add_eff_size; ++i){
        int f1 = borISetGet(&op->add_eff, i);
        for (int j = i + 1; j < add_eff_size; ++j){
            int f2 = borISetGet(&op->add_eff, j);
            borISetAdd(&graph->in[f1], f2);
            borISetAdd(&graph->out[f2], f1);
            borISetAdd(&graph->in[f2], f1);
            borISetAdd(&graph->out[f1], f2);
        }

        for (int j = 0; j < del_eff_size; ++j){
            int f2 = borISetGet(&op->del_eff, j);
            borISetAdd(&graph->in[f1], f2);
            borISetAdd(&graph->out[f2], f1);
            borISetAdd(&graph->in[f2], f1);
            borISetAdd(&graph->out[f1], f2);
        }
    }
    for (int i = 0; i < del_eff_size; ++i){
        int f1 = borISetGet(&op->del_eff, i);
        for (int j = i + 1; j < del_eff_size; ++j){
            int f2 = borISetGet(&op->del_eff, j);
            borISetAdd(&graph->in[f1], f2);
            borISetAdd(&graph->out[f2], f1);
            borISetAdd(&graph->in[f2], f1);
            borISetAdd(&graph->out[f1], f2);
        }
    }
}

static void graphInit(graph_t *graph,
                      const pddl_strips_t *strips,
                      const pddl_mgroups_t *mgroup)
{
    int fact_size = strips->fact.fact_size;
    graph->in = BOR_CALLOC_ARR(bor_iset_t, fact_size);
    graph->out = BOR_CALLOC_ARR(bor_iset_t, fact_size);
    graph->node_size = fact_size;

    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        const pddl_strips_op_t *op = strips->op.op[op_id];
        setCGEdgesPreEff(op, graph);
        setCGEdgesEffEff(op, graph);
    }
}

static void freeISetArr(bor_iset_t *set, int size)
{
    for (int i = 0; i < size; ++i)
        borISetFree(set + i);
    if (set != NULL)
        BOR_FREE(set);
}

static void graphFree(graph_t *graph)
{
    freeISetArr(graph->in, graph->node_size);
    freeISetArr(graph->out, graph->node_size);
}

static int minDegreeNode(const int *indegree, const graph_t *graph)
{
    int min_degree = graph->node_size + 1;
    int min_degree_node = -1;
    for (int ni = 0; ni < graph->node_size; ++ni){
        if (indegree[ni] > 0 && indegree[ni] < min_degree){
            min_degree = indegree[ni];
            min_degree_node = ni;
        }
    }
    return min_degree_node;
}

static void topologicalOrder(const graph_t *graph, int *order)
{
    int *indegree = BOR_CALLOC_ARR(int, graph->node_size);
    BOR_IARR(zero_indegree);
    for (int ni = 0; ni < graph->node_size; ++ni){
        indegree[ni] = borISetSize(&graph->in[ni]);
        if (indegree[ni] == 0)
            borIArrAdd(&zero_indegree, ni);
    }

    if (borIArrSize(&zero_indegree) == 0){
        int min_node = minDegreeNode(indegree, graph);
        indegree[min_node] = 0;
        borIArrAdd(&zero_indegree, min_node);
    }


    int ins = 0;
    while (ins != graph->node_size){
        int node = borIArrPopLast(&zero_indegree);
        order[ins++] = node;
        int node2;
        BOR_ISET_FOR_EACH(&graph->out[node], node2){
            if (--indegree[node2] == 0)
                borIArrAdd(&zero_indegree, node2);
        }

        if (borIArrSize(&zero_indegree) == 0 && ins != graph->node_size){
            int min_node = minDegreeNode(indegree, graph);
            indegree[min_node] = 0;
            borIArrAdd(&zero_indegree, min_node);
        }
    }
    borIArrFree(&zero_indegree);
    BOR_FREE(indegree);
}

static void determineFactOrdering(const pddl_strips_t *strips,
                                  const pddl_mgroups_t *mgroup,
                                  int *ordering)
{
    graph_t graph;
    graphInit(&graph, strips, mgroup);
    topologicalOrder(&graph, ordering);
    graphFree(&graph);
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
    ss->cfg = *cfg;

    ss->strips = strips;
    ss->fact_size = strips->fact.fact_size;
    ss->ordered_facts = BOR_ALLOC_ARR(int, ss->fact_size);
    determineFactOrdering(strips, mgroups, ss->ordered_facts);
    ss->fact_to_order = BOR_ALLOC_ARR(int, ss->fact_size);
    for (int i = 0; i < ss->fact_size; ++i)
        ss->fact_to_order[ss->ordered_facts[i]] = i;

    ss->pre_fact_to_var = BOR_CALLOC_ARR(int, ss->fact_size);
    ss->eff_fact_to_var = BOR_CALLOC_ARR(int, ss->fact_size);
    for (int fact_id = 0; fact_id < ss->fact_size; ++fact_id){
        int order = ss->fact_to_order[fact_id];
        ss->pre_fact_to_var[fact_id] = 2 * order;
        ss->eff_fact_to_var[fact_id] = 2 * order + 1;
    }
    ss->num_vars = 2 * ss->fact_size;

    BOR_INFO(err, "symbolic: Prepared %d BDD variables covering %d facts",
             ss->num_vars, ss->fact_size);

    // TODO
    ss->disambiguate = BOR_ALLOC(pddl_disambiguate_t);
    if (pddlDisambiguateInit(ss->disambiguate, ss->fact_size,
                             mutex, mgroups) != 0){
        BOR_INFO2(err, "symbolic: Disambiguation failed because there are"
                       " no exactly-1 mutex groups");
        BOR_FREE(ss->disambiguate);
        ss->disambiguate = NULL;
    }
    BOR_INFO2(err, "symbolic: Disambiguation created.");

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
    BOR_INFO(err, "symbolic: CUDD initialized with slots: %u, cache size: %u,"
                  " mem: %lu",
             num_slots, cache_size, mem);

    // TODO
    transSetsInit(ss, strips, mutex, &ss->trans, err);
    constrInit(ss, &ss->constr, mutex, mgroups, err);
    ss->init = createState(ss, &strips->init);
    ss->goal = createPartialState(ss, &strips->goal);
    ss->goal = constrApplyBw(ss, &ss->constr, ss->goal);

    ASSERT(Cudd_DebugCheck(ss->ddm) == 0);

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
    if (ss->disambiguate != NULL){
        pddlDisambiguateFree(ss->disambiguate);
        BOR_FREE(ss->disambiguate);
    }
    constrFree(ss, &ss->constr);
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
        DEREF(ss->ddm, ss->init);
    if (ss->goal != NULL)
        DEREF(ss->ddm, ss->goal);
    //Cudd_PrintInfo(ss->ddm, stderr);
    if (ss->ddm != NULL){
        ASSERT(Cudd_CheckZeroRef(ss->ddm) == 0);
        Cudd_Quit(ss->ddm);
    }
    BOR_FREE(ss);
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
    pddl_symbolic_search_t fw_search;
    searchInit(ss, &fw_search, 1, transSetImage, transSetPreImage,
               constrApplyFw, ss->init, ss->goal);
    int res = searchOneDir(ss, &fw_search, err);
    borIArrAppendArr(plan, &fw_search.plan);
    searchFree(ss, &fw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "symbolic: plan: (%s) ;; id=%d, cost %d",
                 ss->strips->op.op[op_id]->name,
                 op_id,
                 ss->strips->op.op[op_id]->cost);
    }
#endif /* PDDL_DEBUG */
    return res;
}

int pddlSymbolicTaskSearchBw(pddl_symbolic_task_t *ss,
                             bor_iarr_t *plan,
                             bor_err_t *err)
{
    pddl_symbolic_search_t bw_search;
    searchInit(ss, &bw_search, 0, transSetPreImage, transSetImage,
               constrApplyBw, ss->goal, ss->init);
    int res = searchOneDir(ss, &bw_search, err);
    borIArrAppendArr(plan, &bw_search.plan);
    searchFree(ss, &bw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "symbolic: plan: (%s) ;; id=%d, cost %d",
                 ss->strips->op.op[op_id]->name,
                 op_id,
                 ss->strips->op.op[op_id]->cost);
    }
#endif /* PDDL_DEBUG */
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

    DdNode *fw_goal_bdd = fw_goal_state->bdd;
    DdNode *bw_goal_bdd = bw_goal_state->bdd;

    // Compute cut between forward and backward search frontier
    DdNode *cut = Cudd_bddAnd(ss->ddm, fw_goal_bdd, bw_goal_bdd);
    Cudd_Ref(cut);
    ASSERT_RUNTIME(!IS_FALSE(ss->ddm, cut));

    // We need to choose one particular state before extracting plans from
    // fw and bw searches
    BOR_ISET(cut_fact_state);
    DdNode *cut_state = bddStateSelectOne(ss, cut, &cut_fact_state);
    borISetFree(&cut_fact_state);
    DEREF(ss->ddm, cut);

    // Extract forward plan from init to cut_state
    plan_t fw_plan;
    planInit(ss, fw_search, &fw_plan, fw_goal_state, cut_state);
    planExtractFw(&fw_plan, ss->strips, &fw_search->plan);
    planFree(&fw_plan);

    // Extract backward plan from cut_state to goal
    plan_t bw_plan;
    planInit(ss, bw_search, &bw_plan, bw_goal_state, cut_state);
    planReverse(&bw_plan);
    planExtractFw(&bw_plan, ss->strips, &bw_search->plan);
    planFree(&bw_plan);

    DEREF(ss->ddm, cut_state);

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
    int res = -1;
    pddl_symbolic_search_t fw_search;
    searchInit(ss, &fw_search, 1, transSetImage, transSetPreImage,
               constrApplyFw, ss->init, ss->goal);

    pddl_symbolic_search_t bw_search;
    searchInit(ss, &bw_search, 0, transSetPreImage, transSetImage,
               constrApplyBw, ss->goal, ss->init);

    searchStep(ss, &fw_search, &bw_search, err);
    searchStep(ss, &bw_search, &fw_search, err);

    while (!borPairHeapEmpty(fw_search.state.open)
            && !borPairHeapEmpty(bw_search.state.open)){
        const pddl_cost_t *min_fw_cost = statesMinOpenCost(&fw_search.state);
        const pddl_cost_t *min_bw_cost = statesMinOpenCost(&bw_search.state);
        const pddl_cost_t *bound = &fw_search.state.bound;
        ASSERT(min_fw_cost != NULL);
        ASSERT(min_bw_cost != NULL);
        ASSERT(pddlCostCmp(bound, &bw_search.state.bound) == 0);
        if (pddlCostCmpSum(min_fw_cost, min_bw_cost, bound) >= 0)
            break;

        BOR_INFO(err, "symbolic search fw+bw: time estimations:"
                      " fw: %.2f, bw: %.2f",
                 fw_search.next_step_estimate,
                 bw_search.next_step_estimate);
        if (fw_search.next_step_estimate <= bw_search.next_step_estimate){
            searchStep(ss, &fw_search, &bw_search, err);
        }else{
            searchStep(ss, &bw_search, &fw_search, err);
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
        BOR_INFO(err, "symbolic search fw+bw: Found plan, cost: %d:%d,"
                      " length: %d",
                 fw_search.state.bound.cost,
                 fw_search.state.bound.zero_cost,
                 borIArrSize(plan));
    }

    searchFree(ss, &fw_search);
    searchFree(ss, &bw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "symbolic search fw+bw: plan: (%s) ;; id=%d, cost %d",
                 ss->strips->op.op[op_id]->name,
                 op_id,
                 ss->strips->op.op[op_id]->cost);
    }
#endif /* PDDL_DEBUG */

    return res;
}

#else /* PDDL_CUDD */

#endif /* PDDL_CUDD */
