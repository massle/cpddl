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

#include "pddl/fdr.h"
#include "pddl/mg_strips.h"
#include "pddl/cg.h"
#include "pddl/critical_path.h"
#include "pddl/symbolic_task.h"
#include "pddl/cost.h"
#include "pddl/time_limit.h"
#include "pddl/disambiguation.h"
#include "pddl/scc.h"
#include "pddl/famgroup.h"
#include "pddl/hpot.h"
#include "assert.h"

#define ROUND_EPS 0.001

struct pddl_symbolic_state {
    int id; /*!< ID of this state */
    int parent_id; /*!< Parent state ID */
    bor_iset_t parent_ids; /*!< IDs of parent state if this is a merge-state */
    int trans_id; /*!< ID of the transitions that achieved this state */
    pddl_cost_t cost; /*!< Cost of the state: g value + zero cost g value */
    double heur;
    pddl_cost_t f_value;
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

struct pddl_symbolic_search {
    int fw; /*!< True if this is forward search */
    pddl_symbolic_trans_set_image_fn image; /*!< constructing image */
    pddl_symbolic_trans_set_image_fn pre_image; /*!< constructing pre-image */
    pddl_symbolic_constr_apply_fn constr_apply; /*!< applying constraints */
    pddl_symbolic_states_t state; /*!< State space */
    pddl_bdd_t *goal; /*!< BDD describing the goal states */
    bor_iarr_t plan; /*!< Extracted plan */
    int plan_goal_id; /*!< This search's state where plan was reached */
    int plan_other_goal_id; /*!< Other search's state where plan was reached*/
    float next_step_estimate; /*!< Estimate of the duration of next step */
};
typedef struct pddl_symbolic_search pddl_symbolic_search_t;

struct pddl_symbolic_task {
    pddl_symbolic_task_config_t cfg; /*!< Configuration */
    pddl_fdr_t fdr;
    pddl_mg_strips_t mg_strips;
    pddl_bdd_manager_t *mgr; /*!< Cudd manager */
    pddl_symbolic_vars_t vars; /*!< TODO */
    int fact_size; /*!< Number of facts in the problem */
    int *ordered_facts; /*!< Ordered facts */
    int *fact_to_order; /*!< Mapping from fact to its order index */
    pddl_symbolic_trans_sets_t trans; /*!< BDD transitions */
    pddl_symbolic_constr_t constr; /*!< Constraints */
    pddl_bdd_t *init; /*!< Initial state */
    pddl_bdd_t *goal; /*!< Goal states */
    double heur_init;
    int goal_constr_failed; /*!< True if applying constraints on the goal
                                 failed */
};

static int roundOff(double z)
{
    return ceil(z - ROUND_EPS);
}


static void stateFree(pddl_symbolic_task_t *ss, pddl_symbolic_state_t *state)
{
    if (state->bdd != NULL)
        pddlBDDDel(ss->mgr, state->bdd);
    borISetFree(&state->parent_ids);
}


static int openLT(const bor_pairheap_node_t *n1,
                  const bor_pairheap_node_t *n2,
                  void *data)
{
    const pddl_symbolic_state_t *o1, *o2;
    o1 = bor_container_of(n1, pddl_symbolic_state_t, heap);
    o2 = bor_container_of(n2, pddl_symbolic_state_t, heap);
    int cmp = pddlCostCmp(&o1->f_value, &o2->f_value);
    if (cmp == 0){
        if (o1->heur < o2->heur){
            cmp = -1;
        }else if (o1->heur > o2->heur){
            cmp = 1;
        }
    }
    return cmp <= 0;
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

    states->all_closed = pddlBDDZero(ss->mgr);

    pddlCostSetMax(&states->bound);
}

static void statesFree(pddl_symbolic_task_t *ss, pddl_symbolic_states_t *states)
{
    borPairHeapDel(states->open_cost);
    borPairHeapDel(states->open);

    pddlBDDDel(ss->mgr, states->all_closed);

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
    pddlBDDOrUpdate(ss->mgr, &states->all_closed, state->bdd);
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
    pddlCostSetZero(&state->f_value);
    state->heur = 0.;
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
        state->bdd = pddlBDDClone(ss->mgr, bdd);
    return state;
}

static void statesAddInit(pddl_symbolic_task_t *ss,
                          pddl_symbolic_states_t *states,
                          pddl_bdd_t *bdd)
{
    pddl_symbolic_state_t *state;
    state = statesAddBDD(ss, states, bdd);
    pddlCostSetZero(&state->cost);
    state->heur = ss->heur_init;
    state->f_value = state->cost;
    state->f_value.cost += roundOff(state->heur);
    statesOpenState(ss, states, state);
}



static void searchInit(pddl_symbolic_task_t *ss,
                       pddl_symbolic_search_t *search,
                       int fw,
                       pddl_symbolic_trans_set_image_fn image,
                       pddl_symbolic_trans_set_image_fn pre_image,
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
        search->goal = pddlBDDClone(ss->mgr, search->goal);

    statesAddInit(ss, &search->state, init);

    search->plan_goal_id = -1;
    search->plan_other_goal_id = -1;
}

static void searchFree(pddl_symbolic_task_t *ss,
                       pddl_symbolic_search_t *search)
{
    statesFree(ss, &search->state);
    if (search->goal != NULL)
        pddlBDDDel(ss->mgr, search->goal);
    borIArrFree(&search->plan);
}

static pddl_bdd_t *bddStateSelectOne(pddl_symbolic_task_t *ss,
                                 pddl_bdd_t *bdd,
                                 bor_iset_t *state)
{
    borISetEmpty(state);
    char *cube = BOR_ALLOC_ARR(char, ss->vars.bdd_var_size);
    pddlBDDPickOneCube(ss->mgr, bdd, cube);
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
        pddl_bdd_t *conj = pddlBDDAnd(ss->mgr, bdd, state->bdd);
        if (!pddlBDDIsFalse(ss->mgr, conj)){
            pddlBDDDel(ss->mgr, conj);
            return state;
        }
        pddlBDDDel(ss->mgr, conj);
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
        pddl_bdd_t *preimg = search->pre_image(trset, bdd);
        ASSERT_RUNTIME(!pddlBDDIsFalse(ss->mgr, preimg));
        pddlBDDAndUpdate(ss->mgr, &preimg, prev_state->bdd);

        // Select one of the states -- again, it doesn't matter which one
        pddlBDDDel(ss->mgr, bdd);
        bdd = bddStateSelectOne(ss, preimg, plan->state + idx + 1);
        pddlBDDDel(ss->mgr, preimg);
        state = prev_state;
        state = planNextState(ss, search, prev_state, bdd);
    }
    pddlBDDDel(ss->mgr, bdd);

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


static pddl_bdd_t *searchStateBDD(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              pddl_symbolic_state_t *state)
{
    if (state->bdd == NULL){
        const pddl_symbolic_state_t *prev_state;
        prev_state = statesGet(&search->state, state->parent_id);
        state->bdd = search->image(ss->trans.trans + state->trans_id,
                                   prev_state->bdd);
        pddl_bdd_t *nall = pddlBDDNot(ss->mgr, search->state.all_closed);
        pddlBDDAndUpdate(ss->mgr, &state->bdd, nall);
        pddlBDDDel(ss->mgr, nall);
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
    pddl_bdd_t *goal = pddlBDDAnd(ss->mgr, state->bdd, search->goal);
    if (!pddlBDDIsFalse(ss->mgr, goal)){
        plan_t plan;
        planInit(ss, search, &plan, state, goal);
        if (!search->fw)
            planReverse(&plan);
        planExtractFw(&plan, &ss->mg_strips.strips, &search->plan);
        planFree(&plan);
        pddlBDDDel(ss->mgr, goal);
        return 1;
    }
    pddlBDDDel(ss->mgr, goal);
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
    pddl_bdd_t *goal = pddlBDDAnd(ss->mgr, state_bdd,
                                  other_search->state.all_closed);
    if (!pddlBDDIsFalse(ss->mgr, goal)){
        for (int si = 0; si < other_search->state.num_closed; ++si){
            const pddl_symbolic_state_t *closed_state;
            closed_state = statesGetClosed(&other_search->state, si);
            if (!costStatesIsBetter(search, state, closed_state))
                break;

            pddl_bdd_t *goal = pddlBDDAnd(ss->mgr, state_bdd, closed_state->bdd);
            if (!pddlBDDIsFalse(ss->mgr, goal)){
                searchSetBestPlan(search, state, closed_state);
                searchSetBestPlan(other_search, closed_state, state);
                BOR_INFO(err, "%s: Found best plan so far: cost: %d:%d",
                         (search->fw ? "fw" : "bw"),
                         search->state.bound.cost,
                         search->state.bound.zero_cost);
                res = 1;
            }
            pddlBDDDel(ss->mgr, goal);

            if (res)
                break;
        }
    }
    pddlBDDDel(ss->mgr, goal);
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
    pddl_bdd_t *bdd_in = pddlBDDClone(ss->mgr, state_in->bdd);
    ASSERT(bdd_in != NULL);
    pddl_bdd_t *nall = pddlBDDNot(ss->mgr, states->all_closed);
    pddlBDDAndUpdate(ss->mgr, &bdd_in, nall);
    pddlBDDDel(ss->mgr, nall);

    if (pddlBDDIsFalse(ss->mgr, bdd_in)){
        pddlBDDDel(ss->mgr, bdd_in);
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

        state->heur = state_in->heur + ss->trans.trans[tri].heur_change;
        state->f_value = state->cost;
        state->f_value.cost += roundOff(state->heur);

        statesOpenState(ss, states, state);
        if (other_search != NULL)
            checkGoal2(ss, search, other_search, state, err);
    }

    pddlBDDDel(ss->mgr, bdd_in);
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
            pddl_bdd_t *nall = pddlBDDNot(ss->mgr, search->state.all_closed);
            pddlBDDAndUpdate(ss->mgr, &state->bdd, nall);
            pddlBDDDel(ss->mgr, nall);
        }
    } while (state != NULL && pddlBDDIsFalse(ss->mgr, state->bdd));

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
    bdd = pddlBDDClone(ss->mgr, bdd);

    pddl_symbolic_state_t *next = statesOpenPeek(&search->state);
    while (next != NULL
            && pddlCostCmp(&state->f_value, &next->f_value) == 0
            && fabs(state->heur - next->heur) < 1E-4){
        ASSERT(borISetSize(&next->parent_ids) == 0);
        ASSERT(next->parent_id >= 0);

        next = statesNextOpen(&search->state);
        searchStateBDD(ss, search, next);
        pddl_bdd_t *nall = pddlBDDNot(ss->mgr, search->state.all_closed);
        pddlBDDAndUpdate(ss->mgr, &next->bdd, nall);
        pddlBDDDel(ss->mgr, nall);
        if (!pddlBDDIsFalse(ss->mgr, next->bdd)){
            pddlBDDOrUpdate(ss->mgr, &bdd, next->bdd);
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
        merged->f_value = state->f_value;
        merged->heur = state->heur;
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
    pddlBDDDel(ss->mgr, bdd);
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

    BOR_INFO(err, "%s: step cost: %d:%d, f: %d:%d / %.2f"
                  " states: %d, closed states: %d,"
                  " cudd mem: %.2fMB, gc: %d",
             (search->fw ? "fw" : "bw"),
             state->cost.cost,
             state->cost.zero_cost,
             state->f_value.cost,
             state->f_value.zero_cost,
             state->heur,
             search->state.num_states,
             search->state.num_closed,
             pddlBDDMem(ss->mgr),
             pddlBDDGCUsed(ss->mgr));

    pddl_bdd_t *state_bdd = searchStateBDD(ss, search, state);
    if (pddlBDDIsFalse(ss->mgr, state_bdd)){
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


static void prepareTask(pddl_symbolic_task_t *ss,
                        const pddl_fdr_t *fdr,
                        const pddl_symbolic_task_config_t *cfg,
                        bor_err_t *err)
{
    pddlFDRInitCopy(&ss->fdr, fdr);
    pddlMGStripsInitFDR(&ss->mg_strips, fdr);

    int *var_order = BOR_ALLOC_ARR(int, fdr->var.var_size + 1);
    pddl_cg_t cg;
    pddlCGInit(&cg, &fdr->var, &fdr->op, 0);
    pddlCGVarOrdering(&cg, &fdr->goal, var_order);
    pddlCGFree(&cg);

    for (int i = 0; i < fdr->var.var_size / 2; ++i){
        int tmp;
        BOR_SWAP(var_order[i], var_order[fdr->var.var_size - i - 1], tmp);
    }

    ASSERT_RUNTIME(ss->mg_strips.mg.mgroup_size == fdr->var.var_size);
    pddlMGStripsReorderMGroups(&ss->mg_strips, var_order);

#ifdef PDDL_DEBUG
    for (int i = 0; i < ss->mg_strips.mg.mgroup_size; ++i){
        for (int j = i + 1; j < ss->mg_strips.mg.mgroup_size; ++j){
            ASSERT(borISetIsDisjoint(&ss->mg_strips.mg.mgroup[i].mgroup,
                                     &ss->mg_strips.mg.mgroup[j].mgroup));
        }
    }
#endif /* PDDL_DEBUG */

    BOR_FREE(var_order);
}

static void initConstr(pddl_symbolic_task_t *ss,
                       const pddl_symbolic_task_config_t *cfg,
                       bor_err_t *err)
{
    pddl_mgroups_t mgs;
    pddlMGroupsInitEmpty(&mgs);
    for (int i = 0; i < ss->mg_strips.mg.mgroup_size; ++i){
        const pddl_mgroup_t *mgin = ss->mg_strips.mg.mgroup + i;
        pddl_mgroup_t *mg = pddlMGroupsAdd(&mgs, &mgin->mgroup);
        mg->is_exactly_one = mgin->is_exactly_one;
        mg->is_fam_group = mgin->is_fam_group;
        mg->is_goal = mgin->is_goal;
    }
    pddl_famgroup_config_t fam_cfg = PDDL_FAMGROUP_CONFIG_INIT;
    fam_cfg.maximal = 0;
    fam_cfg.goal = 1;
    pddlFAMGroupsInfer(&mgs, &ss->mg_strips.strips, &fam_cfg, err);
    pddlMGroupsRemoveSubsets(&mgs);
    pddlMGroupsRemoveSmall(&mgs, 1);
    pddlMGroupsSetExactlyOne(&mgs, &ss->mg_strips.strips);
    pddlMGroupsSetGoal(&mgs, &ss->mg_strips.strips);

    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitStrips(&mutex, &ss->mg_strips.strips);
    pddlH2FwBw(&ss->mg_strips.strips, &ss->mg_strips.mg, &mutex,
               NULL, NULL, 0., err);
    pddlMutexPairsAddMGroups(&mutex, &mgs);

    pddlSymbolicConstrInit(&ss->constr, &ss->vars, &mutex, &mgs,
                           ss->cfg.constr_max_nodes,
                           ss->cfg.constr_max_time,
                           err);
    pddlMGroupsFree(&mgs);
    pddlMutexPairsFree(&mutex);

}

pddl_symbolic_task_t *pddlSymbolicTaskNew(const pddl_fdr_t *fdr,
                                          const pddl_symbolic_task_config_t *cfg,
                                          bor_err_t *err)
{
    if (fdr->has_cond_eff){
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
    if (ss->cfg.use_op_constr)
        ss->cfg.use_constr = 0;

    prepareTask(ss, fdr, cfg, err);

    pddlSymbolicVarsInit(&ss->vars,
                         ss->mg_strips.strips.fact.fact_size,
                         &ss->mg_strips.mg);
    BOR_INFO(err, "Prepared %d BDD variables covering %d facts",
             ss->vars.bdd_var_size, ss->fact_size);

    ss->mgr = pddlBDDManagerNew(ss->vars.bdd_var_size, cfg->cache_size);
    if (ss->mgr == NULL){
        pddlSymbolicTaskDel(ss);
        BOR_ERR_RET2(err, NULL, "Initialization of CUDD failed.");
    }
    BOR_INFO2(err, "CUDD initialized.");

    pddlSymbolicVarsInitBDD(ss->mgr, &ss->vars);

    initConstr(ss, cfg, err);
    BOR_INFO2(err, "Constraints created.");

    // TODO
    pddl_hpot_config_t pot_cfg = PDDL_HPOT_CONFIG_INIT;
    pddl_hpot_t hpot;
    pddlHPotInit(&hpot, fdr, &pot_cfg, err);
    ss->heur_init = pddlHPotFDRStateEstimateDbl(&hpot, &fdr->var, fdr->init);

    pddlSymbolicTransSetsInit(&ss->trans, &ss->vars, &ss->constr,
                              &ss->mg_strips.strips,
                              cfg->use_op_constr,
                              cfg->trans_merge_max_nodes,
                              cfg->trans_merge_max_time,
                              hpot.pot[0],
                              err);
    BOR_INFO2(err, "Transitions created.");

    ss->init = pddlSymbolicVarsCreateState(&ss->vars,
                                           &ss->mg_strips.strips.init);
    BOR_INFO2(err, "Initial state created.");
    ss->goal = pddlSymbolicVarsCreatePartialState(&ss->vars,
                                                  &ss->mg_strips.strips.goal);
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
    //ASSERT(Cudd_DebugCheck(ss->mgr) == 0);
    /*
    BOR_INFO(err, "Symbolic task created."
                  " mem in use: %.2fMB, node count: %ld,"
                  " bdd variables: %d,"
                  " peak node count: %d,"
                  " peak live node count: %d,"
                  " garbage collections: %d",
             Cudd_ReadMemoryInUse(ss->mgr) / (1024. * 1024.),
             Cudd_ReadNodeCount(ss->mgr),
             Cudd_ReadSize(ss->mgr),
             Cudd_ReadPeakNodeCount(ss->mgr),
             Cudd_ReadPeakLiveNodeCount(ss->mgr),
             Cudd_ReadGarbageCollections(ss->mgr));
    */
    //Cudd_PrintInfo(ss->mgr, stderr);

    BOR_INFO_PREFIX_POP(err);
    return ss;
}

void pddlSymbolicTaskDel(pddl_symbolic_task_t *ss)
{
    pddlFDRFree(&ss->fdr);
    pddlMGStripsFree(&ss->mg_strips);
    pddlSymbolicConstrFree(&ss->constr);
    pddlSymbolicTransSetsFree(&ss->trans);
    if (ss->ordered_facts != NULL)
        BOR_FREE(ss->ordered_facts);
    if (ss->fact_to_order != NULL)
        BOR_FREE(ss->fact_to_order);
    if (ss->init != NULL)
        pddlBDDDel(ss->mgr, ss->init);
    if (ss->goal != NULL)
        pddlBDDDel(ss->mgr, ss->goal);
    //Cudd_PrintInfo(ss->mgr, stderr);
    pddlSymbolicVarsFree(&ss->vars);
    if (ss->mgr != NULL)
        pddlBDDManagerDel(ss->mgr);
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
    searchInit(ss, &fw_search, 1,
               pddlSymbolicTransSetImage, pddlSymbolicTransSetPreImage,
               pddlSymbolicConstrApplyFw, ss->init, ss->goal);
    int res = searchOneDir(ss, &fw_search, err);
    borIArrAppendArr(plan, &fw_search.plan);
    searchFree(ss, &fw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "plan: (%s) ;; id=%d, cost %d",
                 ss->mg_strips.strips.op.op[op_id]->name,
                 op_id,
                 ss->mg_strips.strips.op.op[op_id]->cost);
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
    searchInit(ss, &bw_search, 0,
               pddlSymbolicTransSetPreImage, pddlSymbolicTransSetImage,
               pddlSymbolicConstrApplyBw, ss->goal, ss->init);
    int res = searchOneDir(ss, &bw_search, err);
    borIArrAppendArr(plan, &bw_search.plan);
    searchFree(ss, &bw_search);

#ifdef PDDL_DEBUG
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        BOR_INFO(err, "plan: (%s) ;; id=%d, cost %d",
                 ss->mg_strips.strips.op.op[op_id]->name,
                 op_id,
                 ss->mg_strips.strips.op.op[op_id]->cost);
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
    pddl_bdd_t *cut = pddlBDDAnd(ss->mgr, fw_goal_bdd, bw_goal_bdd);
    ASSERT_RUNTIME(!pddlBDDIsFalse(ss->mgr, cut));

    // We need to choose one particular state before extracting plans from
    // fw and bw searches
    BOR_ISET(cut_fact_state);
    pddl_bdd_t *cut_state = bddStateSelectOne(ss, cut, &cut_fact_state);
    borISetFree(&cut_fact_state);
    pddlBDDDel(ss->mgr, cut);

    // Extract forward plan from init to cut_state
    plan_t fw_plan;
    planInit(ss, fw_search, &fw_plan, fw_goal_state, cut_state);
    planExtractFw(&fw_plan, &ss->mg_strips.strips, &fw_search->plan);
    planFree(&fw_plan);

    // Extract backward plan from cut_state to goal
    plan_t bw_plan;
    planInit(ss, bw_search, &bw_plan, bw_goal_state, cut_state);
    planReverse(&bw_plan);
    planExtractFw(&bw_plan, &ss->mg_strips.strips, &bw_search->plan);
    planFree(&bw_plan);

    pddlBDDDel(ss->mgr, cut_state);

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
    searchInit(ss, &fw_search, 1,
               pddlSymbolicTransSetImage, pddlSymbolicTransSetPreImage,
               pddlSymbolicConstrApplyFw, ss->init, ss->goal);
    BOR_INFO2(err, "fw-search created.");

    pddl_symbolic_search_t bw_search;
    searchInit(ss, &bw_search, 0,
               pddlSymbolicTransSetPreImage, pddlSymbolicTransSetImage,
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
                 ss->mg_strips.strips.op.op[op_id]->name,
                 op_id,
                 ss->mg_strips.strips.op.op[op_id]->cost);
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

static pddl_bdd_t *createFDRState(pddl_symbolic_task_t *ss, const int *state)
{
    BOR_ISET(st);
    for (int i = 0; i < ss->fdr.var.var_size; ++i)
        borISetAdd(&st, ss->fdr.var.var[i].val[state[i]].global_id);
    pddl_bdd_t *bdd_state = pddlSymbolicVarsCreateState(&ss->vars, &st);
    borISetFree(&st);
    return bdd_state;
}

int pddlSymbolicTaskCheckApplyFw(pddl_symbolic_task_t *ss,
                                 const int *state,
                                 const int *res_state,
                                 int op_id)
{
    int res = 1;
    pddl_bdd_t *bdd_state = createFDRState(ss, state);
    pddl_bdd_t *bdd_res_state = createFDRState(ss, res_state);
    for (int tri = 0; res && tri < ss->trans.trans_size; ++tri){
        pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
        if (!borISetIn(op_id, &trs->op))
            continue;

        pddl_bdd_t *next_states = pddlSymbolicTransSetImage(trs, bdd_state);
        pddlSymbolicConstrApplyFw(&ss->constr, &next_states);
        pddl_bdd_t *conj = pddlBDDAnd(ss->mgr, next_states, bdd_res_state);
        if (pddlBDDIsFalse(ss->mgr, conj)){
            res = 0;
        }
        pddlBDDDel(ss->mgr, conj);
        pddlBDDDel(ss->mgr, next_states);
    }
    pddlBDDDel(ss->mgr, bdd_state);
    pddlBDDDel(ss->mgr, bdd_res_state);

    return res;
}

int pddlSymbolicTaskCheckApplyBw(pddl_symbolic_task_t *ss,
                                 const int *state,
                                 const int *res_state,
                                 int op_id)
{
    int res = 1;
    pddl_bdd_t *bdd_state = createFDRState(ss, state);
    pddl_bdd_t *bdd_res_state = createFDRState(ss, res_state);
    for (int tri = 0; res && tri < ss->trans.trans_size; ++tri){
        pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
        if (!borISetIn(op_id, &trs->op))
            continue;

        pddl_bdd_t *next_states = pddlSymbolicTransSetPreImage(trs, bdd_state);
        pddlSymbolicConstrApplyBw(&ss->constr, &next_states);
        pddl_bdd_t *conj = pddlBDDAnd(ss->mgr, next_states, bdd_res_state);
        if (pddlBDDIsFalse(ss->mgr, conj)){
            res = 0;
        }
        pddlBDDDel(ss->mgr, conj);
        pddlBDDDel(ss->mgr, next_states);
    }
    pddlBDDDel(ss->mgr, bdd_state);
    pddlBDDDel(ss->mgr, bdd_res_state);

    return res;
}

int pddlSymbolicTaskCheckPlan(pddl_symbolic_task_t *ss,
                              const bor_iarr_t *op,
                              int plan_size)
{
    int res = 1;
    pddl_bdd_t **fw_node = BOR_ALLOC_ARR(pddl_bdd_t *, plan_size + 1);
    pddl_bdd_t **bw_node = BOR_ALLOC_ARR(pddl_bdd_t *, plan_size + 1);
    fw_node[0] = pddlBDDClone(ss->mgr, ss->init);
    bw_node[plan_size] = pddlBDDClone(ss->mgr, ss->goal);
    pddl_bdd_t *fw_closed = pddlBDDClone(ss->mgr, fw_node[0]);
    pddl_bdd_t *bw_closed = pddlBDDClone(ss->mgr, bw_node[plan_size]);
    for (int fi = 0; fi < plan_size; ++fi){
        int fw_op_id = borIArrGet(op, fi);
        for (int tri = 0; tri < ss->trans.trans_size; ++tri){
            pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
            if (!borISetIn(fw_op_id, &trs->op))
                continue;
            fw_node[fi + 1] = pddlSymbolicTransSetImage(trs, fw_node[fi]);
            if (ss->cfg.use_op_constr){
                pddl_bdd_t *tmp = pddlBDDClone(ss->mgr, fw_node[fi + 1]);
                pddlSymbolicConstrApplyFw(&ss->constr, &tmp);

                pddl_bdd_t *fwnot = pddlBDDNot(ss->mgr, fw_node[fi + 1]);
                pddl_bdd_t *diff;
                diff = pddlBDDAnd(ss->mgr, tmp, fwnot);
                //ASSERT(IS_FALSE(ss->mgr, diff));
                pddlBDDDel(ss->mgr, diff);
                pddlBDDDel(ss->mgr, fwnot);

                pddl_bdd_t *tmpnot = pddlBDDNot(ss->mgr, tmp);
                diff = pddlBDDAnd(ss->mgr, tmpnot, fw_node[fi + 1]);
                //ASSERT(IS_FALSE(ss->mgr, diff));
                pddlBDDDel(ss->mgr, diff);
                pddlBDDDel(ss->mgr, tmpnot);

                //if (tmp != fw_node[fi + 1])
                //    res = 0;
                //ASSERT(tmp == fw_node[fi + 1]);
                pddlBDDDel(ss->mgr, tmp);

            }else if (ss->cfg.use_constr){
                pddlSymbolicConstrApplyFw(&ss->constr, &fw_node[fi + 1]);
            }

            pddl_bdd_t *nclosed = pddlBDDNot(ss->mgr, fw_closed);
            pddlBDDAndUpdate(ss->mgr, &fw_node[fi + 1], nclosed);
            pddlBDDDel(ss->mgr, nclosed);
            pddlBDDOrUpdate(ss->mgr, &fw_closed, fw_node[fi + 1]);
        }

        int bw_op_id = borIArrGet(op, plan_size - fi - 1);
        for (int tri = 0; tri < ss->trans.trans_size; ++tri){
            pddl_symbolic_trans_set_t *trs = ss->trans.trans + tri;
            if (!borISetIn(bw_op_id, &trs->op))
                continue;
            int fi2 = plan_size - fi;
            bw_node[fi2 - 1] = pddlSymbolicTransSetPreImage(trs, bw_node[fi2]);
            if (ss->cfg.use_op_constr){
                pddl_bdd_t *tmp = pddlBDDClone(ss->mgr, bw_node[fi2 - 1]);
                pddlSymbolicConstrApplyBw(&ss->constr, &tmp);
                //if (tmp != bw_node[fi2 - 1])
                //    res = 0;
                //ASSERT(tmp == bw_node[fi2 - 1]);
                pddlBDDDel(ss->mgr, tmp);

            }else if (ss->cfg.use_constr){
                pddlSymbolicConstrApplyBw(&ss->constr, &bw_node[fi2 - 1]);
            }
            pddl_bdd_t *nclosed = pddlBDDNot(ss->mgr, bw_closed);
            pddlBDDAndUpdate(ss->mgr, &bw_node[fi2 - 1], nclosed);
            pddlBDDDel(ss->mgr, nclosed);
            pddlBDDOrUpdate(ss->mgr, &bw_closed, bw_node[fi2 - 1]);
        }
    }

    for (int fi = 0; fi < plan_size + 1; ++fi){
        pddl_bdd_t *conj = pddlBDDAnd(ss->mgr, fw_node[fi], bw_node[fi]);
        if (pddlBDDIsFalse(ss->mgr, conj)){
            fprintf(stderr, "FAIL1 %d\n", fi);
            fflush(stderr);
            res = 0;
        }
        pddlBDDDel(ss->mgr, conj);

        conj = pddlBDDAnd(ss->mgr, bw_node[fi], fw_closed);
        if (pddlBDDIsFalse(ss->mgr, conj)){
            fprintf(stderr, "FAIL2 %d\n", fi);
            fflush(stderr);
            res = 0;
        }
        pddlBDDDel(ss->mgr, conj);

        conj = pddlBDDAnd(ss->mgr, fw_node[fi], bw_closed);
        if (pddlBDDIsFalse(ss->mgr, conj)){
            fprintf(stderr, "FAIL3 %d\n", fi);
            fflush(stderr);
            res = 0;
        }
        pddlBDDDel(ss->mgr, conj);
    }

    pddlBDDDel(ss->mgr, fw_closed);
    pddlBDDDel(ss->mgr, bw_closed);
    for (int fi = 0; fi < plan_size + 1; ++fi){
        pddlBDDDel(ss->mgr, fw_node[fi]);
        pddlBDDDel(ss->mgr, bw_node[fi]);
    }
    BOR_FREE(fw_node);
    BOR_FREE(bw_node);

    return res;
}
