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

#include "pddl/symbolic_task.h"
#include "assert.h"

struct pddl_symbolic_trans {
    DdNode *bdd;
    DdNode **var_pre;
    DdNode **var_eff;
    int var_size;
    DdNode *exist_pre;
    DdNode *exist_eff;
};
typedef struct pddl_symbolic_trans pddl_symbolic_trans_t;

struct pddl_symbolic_trans_set {
    pddl_symbolic_trans_t *trans;
    int trans_size;

    bor_iset_t op;
    int cost;
};
typedef struct pddl_symbolic_trans_set pddl_symbolic_trans_set_t;

struct pddl_symbolic_trans_sets {
    pddl_symbolic_trans_set_t *trans;
    int trans_size;
};
typedef struct pddl_symbolic_trans_sets pddl_symbolic_trans_sets_t;

struct pddl_symbolic_task {
    DdManager *ddm;
    int fact_size;
    int *ordered_facts;
    int *fact_to_order;
    int *pre_fact_to_var;
    int *eff_fact_to_var;
    int num_vars;
    DdNode **bdd_var;
    pddl_symbolic_trans_sets_t trans;
};

// X = X & Y
#define CUDD_AND(DDM, X, Y) \
    do { \
        DdNode *___res = Cudd_bddAnd((DDM), (X), (Y)); \
        Cudd_Ref(___res); \
        Cudd_RecursiveDeref((DDM), (X)); \
        (X) = ___res; \
    } while (0)

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

    BOR_ISET(eff);
    borISetUnion2(&eff, &op->add_eff, &op->del_eff);
    tr->var_size = borISetSize(&eff);
    tr->var_pre = BOR_CALLOC_ARR(DdNode *, tr->var_size);
    tr->var_eff = BOR_CALLOC_ARR(DdNode *, tr->var_size);
    int ins = 0;
    BOR_ISET_FOR_EACH(&eff, fact_id){
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
    borISetFree(&eff);

    tr->exist_pre = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(tr->exist_pre);
    tr->exist_eff = Cudd_ReadOne(ss->ddm);
    Cudd_Ref(tr->exist_eff);
    for (int i = 0; i < tr->var_size; ++i){
        CUDD_AND(ss->ddm, tr->exist_pre, tr->var_pre[i]);
        CUDD_AND(ss->ddm, tr->exist_eff, tr->var_eff[i]);
    }
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

    trset->trans_size = borISetSize(&trset->op);
    trset->trans = BOR_CALLOC_ARR(pddl_symbolic_trans_t, trset->trans_size);

    for (int i = 0; i < op_ids_size; ++i)
        transInit(ss, strips->op.op[op_ids[i]], trset->trans + i);

    // TODO: Merge
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

pddl_symbolic_task_t *pddlSymbolicTaskNew(const pddl_strips_t *strips,
                                          const pddl_mgroups_t *mgroups,
                                          const pddl_mutex_pairs_t *mutex,
                                          const pddl_symbolic_task_config_t *cfg,
                                          bor_err_t *err)
{
    pddl_symbolic_task_t *ss;

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
    size_t mem = 0; //1024UL * 1024UL * 1024UL;
    if (cfg->max_mem > 0)
        mem = cfg->max_mem;
    ss->ddm = Cudd_Init(ss->num_vars, 0, num_slots, cache_size, mem);
    if (ss->ddm == NULL){
        pddlSymbolicTaskDel(ss);
        BOR_ERR_RET2(err, NULL, "Initialization of CUDD failed.");
    }

    ss->bdd_var = BOR_CALLOC_ARR(DdNode *, ss->num_vars);
    for (int i = 0; i < ss->num_vars; ++i){
        ss->bdd_var[i] = Cudd_bddIthVar(ss->ddm, i);
        Cudd_Ref(ss->bdd_var[i]);
    }

    transSetsInit(ss, cfg, strips, &ss->trans, err);


    printf("trans_size: %d\n", ss->trans.trans_size);
    DdNode *init = createState(ss, &strips->init);
    Cudd_Ref(init);
    DdNode *next = transImage(ss, ss->trans.trans[0].trans + 1, init);
    Cudd_Ref(next);
    DdNode *prev = transPreImage(ss, ss->trans.trans[0].trans + 1, next);
    Cudd_Ref(prev);
    Cudd_PrintDebug(ss->ddm, init, 20, 4);
    Cudd_PrintDebug(ss->ddm, next, 20, 4);
    Cudd_PrintDebug(ss->ddm, prev, 20, 4);

    DdNode *next2 = transSetImage(ss, ss->trans.trans + 0, init);
    Cudd_Ref(next2);
    Cudd_PrintDebug(ss->ddm, next2, 20, 4);

    DdNode *prev2 = transSetPreImage(ss, ss->trans.trans + 0, next2);
    Cudd_Ref(prev2);
    //CUDD_AND(ss->ddm, prev2, init);
    Cudd_PrintDebug(ss->ddm, prev2, 20, 4);

    return ss;
}

void pddlSymbolicTaskDel(pddl_symbolic_task_t *states)
{
    if (states->ddm != NULL)
        Cudd_Quit(states->ddm);
    if (states->pre_fact_to_var != NULL)
        BOR_FREE(states->pre_fact_to_var);
    if (states->eff_fact_to_var != NULL)
        BOR_FREE(states->eff_fact_to_var);
    BOR_FREE(states);
}

#else /* PDDL_CUDD */

#endif /* PDDL_CUDD */
