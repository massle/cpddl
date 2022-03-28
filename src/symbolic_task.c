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

#include "alloc.h"
#include <boruvka/sort.h>
#include <pddl/extarr.h>
#include <boruvka/pairheap.h>
#include <pddl/rbtree.h>
#include <pddl/rand.h>
#include <pddl/timer.h>

#include "pddl/fdr.h"
#include "pddl/mg_strips.h"
#include "pddl/cg.h"
#include "pddl/critical_path.h"
#include "pddl/symbolic_vars.h"
#include "pddl/symbolic_constr.h"
#include "pddl/symbolic_trans.h"
#include "pddl/symbolic_state.h"
#include "pddl/symbolic_task.h"
#include "pddl/cost.h"
#include "pddl/time_limit.h"
#include "pddl/disambiguation.h"
#include "pddl/scc.h"
#include "pddl/famgroup.h"
#include "pddl/hpot.h"
#include "assert.h"
#include "fmt.h"

#define ROUND_EPS 0.001

struct pddl_symbolic_search {
    pddl_symbolic_search_config_t cfg; /*!< Configuration */
    int enabled;
    int fw; /*!< True if this is forward search */
    int use_heur; /*!< True if heuristics should be used */
    pddl_symbolic_trans_set_image_fn image; /*!< constructing image */
    pddl_symbolic_trans_set_image_fn pre_image; /*!< constructing pre-image */
    pddl_symbolic_constr_apply_fn constr_apply; /*!< applying constraints */
    pddl_cost_t heur_init;
    pddl_bdd_t *init; /*!< BDD of the inital state */
    pddl_bdd_t *goal; /*!< BDD describing the goal states */
    pddl_symbolic_trans_sets_t trans; /*!< BDD transitions */
    pddl_symbolic_states_t state; /*!< State space */
    pddl_iarr_t plan; /*!< Extracted plan */
    int plan_goal_id; /*!< This search's state where plan was reached */
    int plan_other_goal_id; /*!< Other search's state where plan was reached*/
    float next_step_estimate; /*!< Estimate of the duration of next step */
    size_t steps; /*!< Number of steps so far */
    pddl_timer_t steps_time; /*!< For measuring time between steps */
    unsigned long num_expanded_bdd_nodes;
    unsigned long num_expanded_states;
    float avg_expanded_bdd_nodes;
    int dirty;
};
typedef struct pddl_symbolic_search pddl_symbolic_search_t;

struct pddl_symbolic_task {
    pddl_symbolic_task_config_t cfg; /*!< Configuration */
    pddl_fdr_t fdr;
    pddl_mg_strips_t mg_strips;
    pddl_bdd_manager_t *mgr; /*!< Cudd manager */
    pddl_symbolic_vars_t vars; /*!< TODO */
    pddl_symbolic_constr_t constr; /*!< Constraints */
    pddl_bdd_t *init; /*!< Initial state */
    pddl_bdd_t *goal; /*!< Goal states */
    int goal_constr_failed; /*!< True if applying constraints on the goal
                                 failed */

    pddl_symbolic_search_t search_fw;
    pddl_symbolic_search_t search_bw;
};

#define LOG_SEARCH_CFG(N, T, F) \
    BOR_INFO(err, "cfg.%s." #N " = " F, dir, (T)cfg->N)
#define LOG_SEARCH_CFG_I(N) LOG_SEARCH_CFG(N, int, "%d")
static void logSearchConfig(const pddl_symbolic_search_config_t *cfg,
                            const char *dir,
                            bor_err_t *err)
{
    LOG_SEARCH_CFG_I(enabled);
    LOG_SEARCH_CFG(trans_merge_max_nodes, unsigned long, "%lu");
    LOG_SEARCH_CFG(trans_merge_max_time, float, "%.2f");
    LOG_SEARCH_CFG_I(use_constr);
    LOG_SEARCH_CFG_I(use_op_constr);
    LOG_SEARCH_CFG_I(use_pot_heur);
    LOG_SEARCH_CFG_I(use_pot_heur_inconsistent);
    LOG_SEARCH_CFG_I(use_pot_heur_sum_op_cost);

    char prefix[128];
    sprintf(prefix, "cfg.%s.pot_heur_config.", dir);
    pddlHPotConfigLog(&cfg->pot_heur_config, prefix, err);
}

static void logConfig(const pddl_symbolic_task_config_t *cfg, bor_err_t *err)
{
    BOR_INFO(err, "cfg.cache_size = %d", cfg->cache_size);
    BOR_INFO(err, "cfg.constr_max_nodes = %lu",
             (unsigned long)cfg->constr_max_nodes);
    BOR_INFO(err, "cfg.constr_max_time = %.2f", cfg->constr_max_time);
    BOR_INFO(err, "cfg.goal_constr_max_time = %.2f", cfg->goal_constr_max_time);
    BOR_INFO(err, "cfg.fam_groups = %d", cfg->fam_groups);
    logSearchConfig(&cfg->fw, "fw", err);
    logSearchConfig(&cfg->bw, "bw", err);
}


static int preparePotHeur(const pddl_fdr_t *fdr,
                          const pddl_symbolic_search_config_t *cfg,
                          pddl_cost_t **op_pot,
                          pddl_cost_t *init_h_value,
                          bor_err_t *err)
{
    *op_pot = NULL;
    pddlCostSetZero(init_h_value);
    if (!cfg->use_pot_heur && !cfg->use_pot_heur_inconsistent)
        return 0;

    pddl_pot_solutions_t pot;
    pddlPotSolutionsInit(&pot);
    pddl_hpot_config_t pot_cfg = cfg->pot_heur_config;
    pot_cfg.op_pot = 1;
    if (pddlHPot(&pot, fdr, &pot_cfg, err) != 0){
        BOR_ERR_RET2(err, -1, "Could not find a potential function.");
    }
    if (pot.sol_size != 1){
        BOR_ERR_RET(err, -1, "Symbolic search supports only a single"
                             " potential function, got %d",
                    pot.sol_size);
    }

    const pddl_pot_solution_t *sol = pot.sol + 0;
    BOR_INFO(err, "Sum of potentials for the initial state: %.4f",
             pddlPotSolutionEvalFDRStateFlt(sol, &fdr->var, fdr->init));
    init_h_value->cost = pddlPotSolutionEvalFDRState(sol, &fdr->var, fdr->init);
    *op_pot = CALLOC_ARR(pddl_cost_t, fdr->op.op_size);
    for (int i = 0; i < sol->op_pot_size && i < fdr->op.op_size; ++i){
        double change = sol->op_pot[i];
        change = floor(change);

        if (change >= PDDL_COST_DEAD_END){
            (*op_pot)[i].cost = PDDL_COST_DEAD_END;
        }else if (change <= PDDL_COST_MIN){
            (*op_pot)[i].cost = PDDL_COST_MIN;
        }else if (change >= PDDL_COST_MAX){
            (*op_pot)[i].cost = PDDL_COST_MAX;
        }else{
            (*op_pot)[i].cost = change;
        }
    }

    pddlPotSolutionsFree(&pot);
    return 0;
}

static int searchInit(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      int fw,
                      const pddl_symbolic_search_config_t *_cfg,
                      pddl_bdd_t *init,
                      pddl_bdd_t *goal,
                      bor_err_t *err)
{
    char prefix[20];
    sprintf(prefix, "Search create %s: ", (fw ? "fw" : "bw"));
    BOR_INFO_PREFIX_PUSH(err, prefix);
    BOR_INFO(err, "Creating %s direction", (fw ? "fw" : "bw"));
    bzero(search, sizeof(*search));
    search->cfg = *_cfg;
    search->enabled = 1;
    search->fw = fw;
    search->use_heur = search->cfg.use_pot_heur;
    if (fw){
        search->image = pddlSymbolicTransSetImage;
        search->pre_image = pddlSymbolicTransSetPreImage;
        if (search->cfg.use_constr)
            search->constr_apply = pddlSymbolicConstrApplyFw;
    }else{
        search->image = pddlSymbolicTransSetPreImage;
        search->pre_image = pddlSymbolicTransSetImage;
        if (search->cfg.use_constr)
            search->constr_apply = pddlSymbolicConstrApplyBw;
    }


    pddl_cost_t *op_pot;
    pddl_cost_t pot_init_h_value;
    if (preparePotHeur(&ss->fdr, &search->cfg,
                       &op_pot, &pot_init_h_value, err) != 0){
        BOR_INFO_PREFIX_POP(err);
        BOR_TRACE_RET(err, -1);
    }
    if (search->use_heur)
        search->heur_init = pot_init_h_value;

    search->init = init;
    if (search->init != NULL)
        search->init = pddlBDDClone(ss->mgr, search->init);

    search->goal = goal;
    if (search->goal != NULL)
        search->goal = pddlBDDClone(ss->mgr, search->goal);

    BOR_INFO(err, "Creating transitions."
                  " merge max nodes: %lu,"
                  " merge max time: %.2fs",
             search->cfg.trans_merge_max_nodes,
             search->cfg.trans_merge_max_time);
    BOR_INFO(err, "Heuristic value for the initial state: %s",
             F_COST(&pot_init_h_value));
    pddlSymbolicTransSetsInit(&search->trans, &ss->vars, &ss->constr,
                              &ss->mg_strips.strips,
                              search->cfg.use_op_constr,
                              search->cfg.trans_merge_max_nodes,
                              search->cfg.trans_merge_max_time,
                              op_pot,
                              search->cfg.use_pot_heur_sum_op_cost,
                              err);
    BOR_INFO2(err, "Transitions created.");
    if (op_pot != NULL)
        FREE(op_pot);

    pddlSymbolicStatesInit(&search->state, ss->mgr,
                           search->cfg.use_pot_heur_inconsistent, err);
    pddlSymbolicStatesAddInit(&search->state, ss->mgr, search->init,
                              (search->use_heur ? &search->heur_init : NULL));


    search->plan_goal_id = -1;
    search->plan_other_goal_id = -1;

    BOR_INFO2(err, "DONE");
    BOR_INFO_PREFIX_POP(err);
    return 0;
}

static void searchReinit(pddl_symbolic_task_t *ss,
                         pddl_symbolic_search_t *search,
                         bor_err_t *err)
{
    if (!search->dirty)
        return;
    BOR_INFO(err, "Re-init %s search...", (search->fw ? "fw" : "bw"));
    pddlSymbolicStatesFree(&search->state, ss->mgr);
    pddlSymbolicStatesInit(&search->state, ss->mgr,
                           search->cfg.use_pot_heur_inconsistent, err);
    pddlSymbolicStatesAddInit(&search->state, ss->mgr, search->init,
                              (search->use_heur ? &search->heur_init : NULL));
    pddlIArrFree(&search->plan);
    pddlIArrInit(&search->plan);
    search->plan_goal_id = -1;
    search->plan_other_goal_id = -1;
    search->next_step_estimate = 0.f;
    search->steps = 0ul;
    search->num_expanded_bdd_nodes = 0ul;
    search->num_expanded_states = 0ul;
    search->avg_expanded_bdd_nodes = 0.f;
    search->dirty = 0;
    BOR_INFO(err, "Re-init %s search. DONE", (search->fw ? "fw" : "bw"));
}

static void searchStart(pddl_symbolic_task_t *ss,
                        pddl_symbolic_search_t *search,
                        bor_err_t *err)
{
    searchReinit(ss, search, err);
    search->dirty = 1;
}

static void searchFree(pddl_symbolic_task_t *ss,
                       pddl_symbolic_search_t *search)
{
    pddlSymbolicTransSetsFree(&search->trans);
    pddlSymbolicStatesFree(&search->state, ss->mgr);
    if (search->init != NULL)
        pddlBDDDel(ss->mgr, search->init);
    if (search->goal != NULL)
        pddlBDDDel(ss->mgr, search->goal);
    pddlIArrFree(&search->plan);
}

static pddl_bdd_t *bddStateSelectOne(pddl_symbolic_task_t *ss,
                                 pddl_bdd_t *bdd,
                                 pddl_iset_t *state)
{
    pddlISetEmpty(state);
    char *cube = ALLOC_ARR(char, ss->vars.bdd_var_size);
    pddlBDDPickOneCube(ss->mgr, bdd, cube);
    for (int gi = 0; gi < ss->vars.group_size; ++gi){
        int fact_id = pddlSymbolicVarsFactFromBDDCube(&ss->vars, gi, cube);
        ASSERT(fact_id >= 0);
        pddlISetAdd(state, fact_id);
    }
    FREE(cube);
    return pddlSymbolicVarsCreateState(&ss->vars, state);
}

struct plan {
    int plan_len;
    pddl_iset_t *state;
    pddl_iset_t **tr_op;
};
typedef struct plan plan_t;

static const pddl_symbolic_state_t *
        planNextState(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      const pddl_symbolic_state_t *state,
                      pddl_bdd_t *bdd)
{
    if (pddlISetSize(&state->parent_ids) == 0)
        return state;

    int state_id;
    PDDL_ISET_FOR_EACH(&state->parent_ids, state_id){
        const pddl_symbolic_state_t *state;
        state = pddlSymbolicStatesGet(&search->state, state_id);
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
    plan->state = CALLOC_ARR(pddl_iset_t, alloc + 1);
    plan->tr_op = CALLOC_ARR(pddl_iset_t *, alloc);

    // Backtrack from the goal_state and extract one particular state at
    // each step.
    // Select one specific state -- it doesn't matter which one
    pddl_bdd_t *bdd = bddStateSelectOne(ss, reached_goal, plan->state + 0);
    const pddl_symbolic_state_t *state;
    state = planNextState(ss, search, goal_state, bdd);
    while (state->parent_id >= 0){
        ASSERT(pddlISetSize(&state->parent_ids) == 0);
        ASSERT(state->trans_id >= 0);
        ASSERT(state->parent_id >= 0);

        int idx = plan->plan_len++;
        if (idx == alloc){
            int old_alloc = alloc;
            alloc *= 2;
            plan->state = REALLOC_ARR(plan->state, pddl_iset_t, alloc + 1);
            bzero(plan->state + old_alloc + 1,
                  sizeof(pddl_iset_t) * (alloc - old_alloc));
            plan->tr_op = REALLOC_ARR(plan->tr_op, pddl_iset_t *, alloc);
        }

        plan->tr_op[idx] = &search->trans.trans[state->trans_id].op;
        const pddl_symbolic_state_t *prev_state;
        prev_state = pddlSymbolicStatesGet(&search->state, state->parent_id);

        // This step of the plan goes from prev_state to state.
        // So, compute the conjuction of the preimage of state and
        // state_prev.
        pddl_symbolic_trans_set_t *trset = search->trans.trans + state->trans_id;
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
        pddl_iset_t tmp;
        BOR_SWAP(plan->state[i], plan->state[plan->plan_len - i], tmp);
    }
    for (int i = 0; i < plan->plan_len / 2; ++i){
        pddl_iset_t *tmp;
        BOR_SWAP(plan->tr_op[i], plan->tr_op[plan->plan_len - i - 1], tmp);
    }
}

static void planFree(plan_t *plan)
{
    for (int i = 0; i < plan->plan_len + 1; ++i)
        pddlISetFree(plan->state + i);
    FREE(plan->state);
    FREE(plan->tr_op);
}

static void planReverse(plan_t *plan)
{
    pddl_iset_t state_tmp;
    int len = (plan->plan_len + 1) / 2;
    for (int i = 0; i < len; ++i){
        BOR_SWAP(plan->state[i],
                 plan->state[plan->plan_len - i],
                 state_tmp);
    }

    pddl_iset_t *tr_tmp;
    len = plan->plan_len / 2;
    for (int i = 0; i < len; ++i){
        BOR_SWAP(plan->tr_op[i],
                 plan->tr_op[plan->plan_len - i - 1],
                 tr_tmp);
    }
}

static void planExtractFw(plan_t *plan,
                          const pddl_strips_t *strips,
                          pddl_iarr_t *out)
{
    // Extract plan from the intermediate states
    PDDL_ISET(res_state);
    for (int si = 0; si < plan->plan_len; ++si){
        const pddl_iset_t *from = plan->state + si;
        const pddl_iset_t *to = plan->state + si + 1;

        int op_id;
        int found = 0;
        PDDL_ISET_FOR_EACH(plan->tr_op[si], op_id){
            const pddl_strips_op_t *op = strips->op.op[op_id];
            if (pddlISetIsSubset(&op->pre, from)){
                pddlISetMinus2(&res_state, from, &op->del_eff);
                pddlISetUnion(&res_state, &op->add_eff);
                if (pddlISetEq(&res_state, to)){
                    pddlIArrAdd(out, op_id);
                    found = 1;
                    break;
                }
            }
        }
        ASSERT_RUNTIME(found);
    }
    pddlISetFree(&res_state);
}


static pddl_bdd_t *searchStateBDD(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search,
                              pddl_symbolic_state_t *state)
{
    if (state->bdd == NULL){
        const pddl_symbolic_state_t *prev_state;
        prev_state = pddlSymbolicStatesGet(&search->state, state->parent_id);
        state->bdd = search->image(search->trans.trans + state->trans_id,
                                   prev_state->bdd);
        pddlSymbolicStatesRemoveClosedStates(&search->state, ss->mgr,
                                             &state->bdd, &state->cost);
        if (search->constr_apply)
            search->constr_apply(&ss->constr, &state->bdd);
    }
    return state->bdd;
}

static int searchNextOpenSize(pddl_symbolic_task_t *ss,
                              pddl_symbolic_search_t *search)
{
    pddl_symbolic_state_t *state = pddlSymbolicStatesOpenPeek(&search->state);
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
        //fprintf(stderr, "----\n");
        pddl_rbtree_node_t *rbs;
        PDDL_RBTREE_FOR_EACH(other_search->state.closed, rbs){
            const pddl_symbolic_state_t *closed_state;
            closed_state = pddl_container_of(rbs, pddl_symbolic_state_t, rbtree);
            if (!costStatesIsBetter(search, state, closed_state))
                break;

            pddl_bdd_t *goal = pddlBDDAnd(ss->mgr, state_bdd, closed_state->bdd);
            if (!pddlBDDIsFalse(ss->mgr, goal)){
                searchSetBestPlan(search, state, closed_state);
                searchSetBestPlan(other_search, closed_state, state);
                BOR_INFO(err, "%s: Found best plan so far: cost: %s",
                         (search->fw ? "fw" : "bw"), F_COST(&search->state.bound));
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
    pddlSymbolicStatesRemoveClosedStates(&search->state, ss->mgr,
                                         &bdd_in, &state_in->cost);

    if (pddlBDDIsFalse(ss->mgr, bdd_in)){
        pddlBDDDel(ss->mgr, bdd_in);
        return;
    }

    for (int tri = 0; tri < search->trans.trans_size; ++tri){
        const pddl_cost_t *tr_cost = &search->trans.trans[tri].cost;

        pddl_cost_t cost = state_in->cost;
        pddlCostSum(&cost, tr_cost);
        if (pddlCostCmp(&cost, &states->bound) >= 0)
            continue;

        // Increase heuristic estimate by the change incurred by this
        // transition
        pddl_cost_t heur = state_in->heur;
        if (search->use_heur)
            pddlCostSumSat(&heur, &search->trans.trans[tri].heur_change);
        if (pddlCostIsDeadEnd(&heur))
            continue;

        // Set f-value = cost + heur, but consider heur < 0 as zero
        pddl_cost_t f_value = cost;
        if (search->use_heur && pddlCostCmp(&heur, &pddl_cost_zero) > 0)
            pddlCostSumSat(&f_value, &heur);
        if (pddlCostCmp(&f_value, &states->bound) >= 0)
            continue;

        pddl_symbolic_state_t *state = pddlSymbolicStatesAdd(states);
        state->parent_id = state_in->id;
        state->trans_id = tri;
        state->cost = cost;
        state->heur = heur;
        state->f_value = f_value;
        DBG(err, "TR cost: %s, heur %s, f %s",
            F_COST(&state->cost), F_COST(&state->heur), F_COST(&state->f_value));

        // Deal with blown-up f-values
        // TODO
        if (pddlCostCmp(&state->f_value, &pddl_cost_zero) < 0
                || pddlCostCmp(&state->f_value, &pddl_cost_max) > 0){
            BOR_INFO2(err, "MAX f-value HIT");
            state->f_value = pddl_cost_max;
        }

        pddlSymbolicStatesOpenState(states, state);
        if (other_search != NULL)
            checkGoal2(ss, search, other_search, state, err);
    }
    search->num_expanded_bdd_nodes += pddlBDDSize(bdd_in);
    if (search->num_expanded_states == 0){
        search->avg_expanded_bdd_nodes = pddlBDDSize(bdd_in);
    }else{
        float avg = search->avg_expanded_bdd_nodes;
        avg *= search->num_expanded_states;
        avg += pddlBDDSize(bdd_in);
        avg /= search->num_expanded_states + 1;
        search->avg_expanded_bdd_nodes = avg;
    }
    ++search->num_expanded_states;

    pddlBDDDel(ss->mgr, bdd_in);
}

static pddl_symbolic_state_t *searchNextNonEmpty(pddl_symbolic_task_t *ss,
                                                 pddl_symbolic_search_t *search,
                                                 bor_err_t *err)
{
    pddl_symbolic_state_t *state;
    do {
        state = pddlSymbolicStatesNextOpen(&search->state);
        if (state != NULL){
            searchStateBDD(ss, search, state);
            pddlSymbolicStatesRemoveClosedStates(&search->state, ss->mgr,
                                                 &state->bdd, &state->cost);
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

    PDDL_ISET(parents);
    pddlISetAdd(&parents, state->id);
    pddl_bdd_t *bdd = searchStateBDD(ss, search, state);
    bdd = pddlBDDClone(ss->mgr, bdd);

    pddl_symbolic_state_t *next = pddlSymbolicStatesOpenPeek(&search->state);
    while (next != NULL
            && pddlCostCmp(&state->cost, &next->cost) == 0
            && pddlCostCmp(&state->heur, &next->heur) == 0){
        ASSERT(pddlISetSize(&next->parent_ids) == 0);
        ASSERT(next->parent_id >= 0);

        next = pddlSymbolicStatesNextOpen(&search->state);
        searchStateBDD(ss, search, next);
        pddlSymbolicStatesRemoveClosedStates(&search->state, ss->mgr,
                                             &next->bdd, &next->cost);
        if (!pddlBDDIsFalse(ss->mgr, next->bdd)){
            pddlBDDOrUpdate(ss->mgr, &bdd, next->bdd);
            pddlISetAdd(&parents, next->id);
        }

        next = pddlSymbolicStatesOpenPeek(&search->state);
    }

    if (pddlISetSize(&parents) > 1){
        pddl_symbolic_state_t *merged;
        merged = pddlSymbolicStatesAddBDD(&search->state, ss->mgr, bdd);
        merged->parent_id = -2;
        merged->trans_id = -1;
        merged->cost = state->cost;
        merged->f_value = state->f_value;
        merged->heur = state->heur;
        pddlISetUnion(&merged->parent_ids, &parents);
        pddlSymbolicStatesOpenState(&search->state, merged);

        DBG(err, "%s: Merged %d states when preparing next state (nodes: %d)",
            (search->fw ? "fw" : "bw"),
            pddlISetSize(&parents),
            pddlBDDSize(bdd));
    }else{
        pddlSymbolicStatesOpenState(&search->state, state);
    }

    pddlISetFree(&parents);
    pddlBDDDel(ss->mgr, bdd);
}

static void printStepLog(const pddl_symbolic_task_t *ss,
                         pddl_symbolic_search_t *search,
                         const pddl_symbolic_state_t *state,
                         bor_err_t *err)
{
    pddlTimerStop(&search->steps_time);
#ifdef PDDL_DEBUG
    if (1){
#else /* PDDL_DEBUG */
    if (search->steps == 1
            || search->steps % 1000ul == 0
            || pddlTimerElapsedInSF(&search->steps_time) > 1.){
#endif /* PDDL_DEBUG */
        BOR_INFO(err, "%s: step %lu, g: %s, h: %s, f: %s"
                      " states: %d, closed states: %d,"
                      " cudd mem: %.2fMB, gc: %d, expanded BDD nodes: %lu",
                 (search->fw ? "fw" : "bw"),
                 (unsigned long)search->steps,
                 F_COST(&state->cost),
                 F_COST(&state->heur),
                 F_COST(&state->f_value),
                 search->state.num_states,
                 search->state.num_closed,
                 pddlBDDMem(ss->mgr),
                 pddlBDDGCUsed(ss->mgr),
                 search->num_expanded_bdd_nodes);
        pddlTimerStart(&search->steps_time);
    }
}

static int searchStep(pddl_symbolic_task_t *ss,
                      pddl_symbolic_search_t *search,
                      pddl_symbolic_search_t *other_search,
                      bor_err_t *err)
{
    ++search->steps;
    pddl_timer_t timer;
    pddlTimerStart(&timer);
    pddl_symbolic_state_t *state = pddlSymbolicStatesNextOpen(&search->state);
    if (state == NULL){
        BOR_INFO(err, "%s: Plan does not exist, steps: %lu",
                 (search->fw ? "fw" : "bw"), (unsigned long)search->steps);
        pddlTimerStop(&timer);
        return PDDL_SYMBOLIC_PLAN_NOT_EXIST;
    }

    printStepLog(ss, search, state, err);


    pddl_bdd_t *state_bdd = searchStateBDD(ss, search, state);
    if (pddlBDDIsFalse(ss->mgr, state_bdd)){
        DBG(err, "%s: State is empty", (search->fw ? "fw" : "bw"));
        return PDDL_SYMBOLIC_CONT;
    }
    DBG(err, "Num states: %.2f",
        pddlBDDCountMinterm(ss->mgr, state_bdd, ss->vars.bdd_var_size / 2));
    DBG(err, "BDD Size: %d", pddlBDDSize(state_bdd));

    if (other_search != NULL){
        checkGoal2(ss, search, other_search, state, err);

    }else{ // search->goal != NULL
        if (checkGoal(ss, search, state, err)){
            BOR_INFO(err, "%s: Found plan, steps: %lu, cost: %s,"
                          " length: %d",
                     (search->fw ? "fw" : "bw"),
                     (unsigned long)search->steps,
                     F_COST(&state->cost),
                     pddlIArrSize(&search->plan));

            pddlTimerStop(&timer);
            searchSetNextStepEstimate(ss, search, state,
                                      pddlTimerElapsedInSF(&timer), err);
            return PDDL_SYMBOLIC_PLAN_FOUND;
        }
    }
    DBG2(err, "Goal checked");

    searchExpandState(ss, search, other_search, state, err);
    DBG2(err, "Expanded");
    pddlSymbolicStatesCloseState(&search->state, ss->mgr, state);
    pddlTimerStop(&timer);
    searchPrepareNext(ss, search, err);
    searchSetNextStepEstimate(ss, search, state,
                              pddlTimerElapsedInSF(&timer), err);
    return PDDL_SYMBOLIC_CONT;
}

static double orderComputeCost(const int *order,
                               const int *influence,
                               int size)
{
    double cost = 0.;
    for (int i = 0; i < size; ++i){
        for (int j = i + 1; j < size; ++j){
            if (influence[order[i] * size + order[j]])
                cost += (j - i) * (j - i);
        }
    }
    return cost;
}

static void orderSwap(int *order,
                      const int *influence,
                      int size,
                      double *cost,
                      pddl_rand_t *rnd)
{
    int swap_idx1 = pddlRand(rnd, 0, size);
    int swap_idx2 = pddlRand(rnd, 0, size);
    if (swap_idx1 == swap_idx2)
        return;

    double new_cost = *cost;
    for (int i = 0; i < size; ++i){
        if (i == swap_idx1 || i == swap_idx2)
            continue;

        if (influence[order[i] * size + order[swap_idx1]]){
            new_cost += (-1 * (i - swap_idx1) * (i - swap_idx1)
                            + (i - swap_idx2) * (i - swap_idx2));
        }

        if (influence[order[i] * size + order[swap_idx2]]){
            new_cost += (-1 * (i - swap_idx2) * (i - swap_idx2)
                            + (i - swap_idx1) * (i - swap_idx1));
        }
    }

    if (new_cost < *cost){
        int tmp;
        BOR_SWAP(order[swap_idx1], order[swap_idx2], tmp);
        *cost = new_cost;
    }
}

static double orderOptimize(int iterations,
                            int *order,
                            const int *influence,
                            int size,
                            pddl_rand_t *rnd)
{
    double cost = orderComputeCost(order, influence, size);
    for (int i = 0; i < iterations; ++i)
        orderSwap(order, influence, size, &cost, rnd);
    return cost;
}

static void orderRandomize(int *order, int size, pddl_rand_t *rnd)
{
    int *order2 = ALLOC_ARR(int, size);
    for (int i = 0; i < size; ++i)
        order2[i] = -1;
    for (int num = 0; num < size; ++num){
        while (1){
            int pos = pddlRand(rnd, 0, size);
            if (order2[pos] == -1){
                order2[pos] = num;
                break;
            }
        }
    }
    memcpy(order, order2, sizeof(int) * size);
    FREE(order2);
}

static void orderCompute(int *order,
                         int size,
                         const pddl_cg_t *cg,
                         bor_err_t *err)
{
    pddl_rand_t rnd;
    pddlRandInitSeed(&rnd, 1371);

    ASSERT_RUNTIME(cg->node_size == size);

    for (int i = 0; i < size / 2; ++i){
        int tmp;
        BOR_SWAP(order[i], order[size - i - 1], tmp);
    }

    int *influence = CALLOC_ARR(int, size * size);
    for (int f = 0; f < size; ++f){
        const pddl_cg_node_t *node = cg->node + f;
        for (int i = 0; i < node->fw_size; ++i){
            if (node->fw[i].end == f)
                continue;
            influence[f * size + node->fw[i].end] = 1;
            influence[node->fw[i].end * size + f] = 1;
        }
    }

    int *order2 = ALLOC_ARR(int, size);
    memcpy(order2, order, sizeof(int) * size);
    double cost = orderOptimize(50000, order2, influence, size, &rnd);
    memcpy(order, order2, sizeof(int) * size);
    BOR_INFO(err, "Init order cost: %.2f", cost);

    for (int i = 0; i < 20; ++i){
        memcpy(order2, order, sizeof(int) * size);
        orderRandomize(order2, size, &rnd);
        double new_cost = orderOptimize(50000, order2, influence, size, &rnd);
        if (new_cost < cost){
            memcpy(order, order2, sizeof(int) * size);
            cost = new_cost;
            BOR_INFO(err, "New order cost: %.2f", cost);
        }
    }
    FREE(order2);
    FREE(influence);
}

static void prepareTask(pddl_symbolic_task_t *ss,
                        const pddl_fdr_t *fdr,
                        const pddl_symbolic_task_config_t *cfg,
                        bor_err_t *err)
{
    pddlFDRInitCopy(&ss->fdr, fdr);
    pddlMGStripsInitFDR(&ss->mg_strips, fdr);

    int *var_order = ALLOC_ARR(int, fdr->var.var_size + 1);
    pddl_cg_t cg;
    pddlCGInit(&cg, &fdr->var, &fdr->op, 1);
    pddlCGVarOrdering(&cg, &fdr->goal, var_order);
    orderCompute(var_order, fdr->var.var_size, &cg, err);
    pddlCGFree(&cg);

    ASSERT_RUNTIME(ss->mg_strips.mg.mgroup_size == fdr->var.var_size);
    pddlMGStripsReorderMGroups(&ss->mg_strips, var_order);
    BOR_INFO2(err, "Order computed and applied");

#ifdef PDDL_DEBUG
    for (int i = 0; i < ss->mg_strips.mg.mgroup_size; ++i){
        for (int j = i + 1; j < ss->mg_strips.mg.mgroup_size; ++j){
            ASSERT(pddlISetIsDisjoint(&ss->mg_strips.mg.mgroup[i].mgroup,
                                     &ss->mg_strips.mg.mgroup[j].mgroup));
        }
    }
#endif /* PDDL_DEBUG */

    FREE(var_order);
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
    if (cfg->fam_groups > 0){
        BOR_INFO(err, "Inferring fam-groups: %d exactly-1 mutex groups in"
                      " the input", mgs.mgroup_size);
        pddl_famgroup_config_t fam_cfg = PDDL_FAMGROUP_CONFIG_INIT;
        fam_cfg.maximal = 0;
        fam_cfg.goal = 1;
        fam_cfg.limit = cfg->fam_groups;
        pddlFAMGroupsInfer(&mgs, &ss->mg_strips.strips, &fam_cfg, err);
    }
    pddlMGroupsRemoveSubsets(&mgs);
    pddlMGroupsRemoveSmall(&mgs, 1);
    pddlMGroupsSetExactlyOne(&mgs, &ss->mg_strips.strips);
    pddlMGroupsSetGoal(&mgs, &ss->mg_strips.strips);
    BOR_INFO(err, "%d exactly-1 mutex groups overall", mgs.mgroup_size);

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

static void fixSearchConfig(pddl_symbolic_search_config_t *cfg)
{
    if (cfg->use_op_constr)
        cfg->use_constr = 0;
    if (cfg->use_pot_heur
            || cfg->use_pot_heur_inconsistent
            || cfg->use_pot_heur_sum_op_cost)
        cfg->use_pot_heur = 1;
}

static void fixConfig(pddl_symbolic_task_config_t *cfg)
{
    fixSearchConfig(&cfg->fw);
    fixSearchConfig(&cfg->bw);
}

pddl_symbolic_task_t *pddlSymbolicTaskNew(const pddl_fdr_t *fdr,
                                          const pddl_symbolic_task_config_t *cfg,
                                          bor_err_t *err)
{
    if (fdr->has_cond_eff){
        BOR_ERR_RET2(err, NULL, "Symbolic tasks does not support conditional"
                                " effects yet.");
    }

    if (((cfg->fw.use_pot_heur
            || cfg->fw.use_pot_heur_inconsistent
            || cfg->fw.use_pot_heur_sum_op_cost)
                && pddlHPotConfigIsEnsemble(&cfg->fw.pot_heur_config))
        ||
        ((cfg->bw.use_pot_heur
            || cfg->bw.use_pot_heur_inconsistent
            || cfg->bw.use_pot_heur_sum_op_cost)
                && pddlHPotConfigIsEnsemble(&cfg->bw.pot_heur_config))){
        BOR_ERR_RET2(err, NULL, "Symbolic tasks can use only a single"
                                " potential heuristic.");
    }

    BOR_INFO_PREFIX_PUSH(err, "symbolic: ");

    pddl_symbolic_task_t *ss;
    BOR_INFO2(err, "Constructing symbolic task.");

    ss = ALLOC(pddl_symbolic_task_t);
    bzero(ss, sizeof(*ss));
    ss->cfg = *cfg;
    fixConfig(&ss->cfg);
    logConfig(&ss->cfg, err);

    prepareTask(ss, fdr, &ss->cfg, err);

    pddlSymbolicVarsInit(&ss->vars,
                         ss->mg_strips.strips.fact.fact_size,
                         &ss->mg_strips.mg);
    BOR_INFO(err, "Prepared %d BDD variables covering %d facts and %d mgroups",
             ss->vars.bdd_var_size,
             ss->mg_strips.strips.fact.fact_size,
             ss->mg_strips.mg.mgroup_size);

    ss->mgr = pddlBDDManagerNew(ss->vars.bdd_var_size, ss->cfg.cache_size);
    if (ss->mgr == NULL){
        pddlSymbolicTaskDel(ss);
        BOR_ERR_RET2(err, NULL, "Initialization of CUDD failed.");
    }
    BOR_INFO2(err, "CUDD initialized.");

    pddlSymbolicVarsInitBDD(ss->mgr, &ss->vars);

    initConstr(ss, &ss->cfg, err);
    BOR_INFO2(err, "Constraints created.");

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

    if (ss->cfg.fw.enabled)
        searchInit(ss, &ss->search_fw, 1, &ss->cfg.fw, ss->init, ss->goal, err);
    if (ss->cfg.bw.enabled)
        searchInit(ss, &ss->search_bw, 0, &ss->cfg.bw, ss->goal, ss->init, err);


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
    if (ss->init != NULL)
        pddlBDDDel(ss->mgr, ss->init);
    if (ss->goal != NULL)
        pddlBDDDel(ss->mgr, ss->goal);
    //Cudd_PrintInfo(ss->mgr, stderr);
    pddlSymbolicVarsFree(&ss->vars);
    if (ss->search_fw.enabled)
        searchFree(ss, &ss->search_fw);
    if (ss->search_bw.enabled)
        searchFree(ss, &ss->search_bw);
    if (ss->mgr != NULL)
        pddlBDDManagerDel(ss->mgr);
    FREE(ss);
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
    BOR_INFO(err, "Expanded BDD Nodes: %lu", search->num_expanded_bdd_nodes);
    BOR_INFO(err, "Expanded States: %lu", search->num_expanded_states);
    BOR_INFO(err, "Avg. Expanded BDD Nodes: %.2f",
             search->avg_expanded_bdd_nodes);
    return res;
}


int pddlSymbolicTaskSearchFw(pddl_symbolic_task_t *ss,
                             pddl_iarr_t *plan,
                             bor_err_t *err)
{
    if (!ss->search_fw.enabled)
        BOR_FATAL2("Symbolic Task wasn't initialzed with fw search!");
    BOR_INFO_PREFIX_PUSH(err, "symbolic search fw: ");
    searchStart(ss, &ss->search_fw, err);
    int res = searchOneDir(ss, &ss->search_fw, err);
    pddlIArrAppendArr(plan, &ss->search_fw.plan);

#ifdef PDDL_DEBUG
    int op_id;
    PDDL_IARR_FOR_EACH(plan, op_id){
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
                             pddl_iarr_t *plan,
                             bor_err_t *err)
{
    if (!ss->search_bw.enabled)
        BOR_FATAL2("Symbolic Task wasn't initialzed with bw search!");
    BOR_INFO_PREFIX_PUSH(err, "symbolic search bw: ");
    searchStart(ss, &ss->search_bw, err);
    int res = searchOneDir(ss, &ss->search_bw, err);
    pddlIArrAppendArr(plan, &ss->search_bw.plan);

#ifdef PDDL_DEBUG
    int op_id;
    PDDL_IARR_FOR_EACH(plan, op_id){
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
                            pddl_iarr_t *plan,
                            bor_err_t *err)
{
    const pddl_symbolic_state_t *fw_goal_state, *bw_goal_state;
    fw_goal_state = pddlSymbolicStatesGet(&fw_search->state, fw_search->plan_goal_id);
    bw_goal_state = pddlSymbolicStatesGet(&bw_search->state, bw_search->plan_goal_id);

    pddl_bdd_t *fw_goal_bdd = fw_goal_state->bdd;
    pddl_bdd_t *bw_goal_bdd = bw_goal_state->bdd;

    // Compute cut between forward and backward search frontier
    pddl_bdd_t *cut = pddlBDDAnd(ss->mgr, fw_goal_bdd, bw_goal_bdd);
    ASSERT_RUNTIME(!pddlBDDIsFalse(ss->mgr, cut));

    // We need to choose one particular state before extracting plans from
    // fw and bw searches
    PDDL_ISET(cut_fact_state);
    pddl_bdd_t *cut_state = bddStateSelectOne(ss, cut, &cut_fact_state);
    pddlISetFree(&cut_fact_state);
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
    PDDL_IARR_FOR_EACH(&fw_search->plan, op_id)
        pddlIArrAdd(plan, op_id);
    PDDL_IARR_FOR_EACH(&bw_search->plan, op_id)
        pddlIArrAdd(plan, op_id);
}

int pddlSymbolicTaskSearchFwBw(pddl_symbolic_task_t *ss,
                               pddl_iarr_t *plan,
                               bor_err_t *err)
{
    if (!ss->search_fw.enabled)
        BOR_FATAL2("Symbolic Task wasn't initialzed with fw search!");
    if (!ss->search_bw.enabled)
        BOR_FATAL2("Symbolic Task wasn't initialzed with bw search!");
    BOR_INFO_PREFIX_PUSH(err, "symbolic search fw+bw: ");
    BOR_INFO2(err, "start");
    searchStart(ss, &ss->search_fw, err);
    searchStart(ss, &ss->search_bw, err);

    int res = PDDL_SYMBOLIC_FAIL;
    pddl_cost_t zero_cost;
    pddlCostSetZero(&zero_cost);

    int fw_cont = searchStep(ss, &ss->search_fw, &ss->search_bw, err);
    int bw_cont = searchStep(ss, &ss->search_bw, &ss->search_fw, err);

    while (!borPairHeapEmpty(ss->search_fw.state.open)
            && !borPairHeapEmpty(ss->search_bw.state.open)){
        if (fw_cont != PDDL_SYMBOLIC_CONT && bw_cont != PDDL_SYMBOLIC_CONT)
            break;

        const pddl_cost_t *min_fw_cost = pddlSymbolicStatesMinOpenCost(&ss->search_fw.state);
        if (min_fw_cost == NULL)
            min_fw_cost = &zero_cost;
        const pddl_cost_t *min_bw_cost = pddlSymbolicStatesMinOpenCost(&ss->search_bw.state);
        if (min_bw_cost == NULL)
            min_bw_cost = &zero_cost;
        const pddl_cost_t *bound = &ss->search_fw.state.bound;
        ASSERT(pddlCostCmp(bound, &ss->search_bw.state.bound) == 0);
        if (pddlCostCmpSum(min_fw_cost, min_bw_cost, bound) >= 0)
            break;

        float fw_est = ss->search_fw.next_step_estimate;
        float bw_est = ss->search_bw.next_step_estimate;
        int fw_step = 0;
        if (fw_cont == PDDL_SYMBOLIC_CONT && fw_est <= bw_est)
            fw_step = 1;

        DBG(err, "fw est: %.2f, bw est: %.2f, fw open: %s,"
                 " bw open: %s, bound: %s, use fw: %d"
                 " fw-closed size: %d, bw-closed size: %d",
                 fw_est, bw_est,
                 F_COST(min_fw_cost),
                 F_COST(min_bw_cost),
                 F_COST(&ss->search_fw.state.bound),
                 fw_step,
                 pddlBDDSize(ss->search_fw.state.all_closed),
                 pddlBDDSize(ss->search_bw.state.all_closed));
        if (fw_step){
            fw_cont = searchStep(ss, &ss->search_fw, &ss->search_bw, err);
        }else{
            bw_cont = searchStep(ss, &ss->search_bw, &ss->search_fw, err);
        }
    }
    ASSERT(pddlCostCmp(&ss->search_fw.state.bound, &ss->search_bw.state.bound) == 0);
    ASSERT(ss->search_fw.plan_goal_id == ss->search_bw.plan_other_goal_id);
    ASSERT(ss->search_fw.plan_other_goal_id == ss->search_bw.plan_goal_id);

    if (ss->search_fw.plan_goal_id == -1){
        res = PDDL_SYMBOLIC_PLAN_NOT_EXIST;
    }else{
        res = PDDL_SYMBOLIC_PLAN_FOUND;
        fwbwExtractPlan(ss, &ss->search_fw, &ss->search_bw, plan, err);
        BOR_INFO(err, "Found plan, cost: %s, length: %d",
                 F_COST(&ss->search_fw.state.bound), pddlIArrSize(plan));
    }

    BOR_INFO(err, "Fw Expanded BDD Nodes: %lu",
             ss->search_fw.num_expanded_bdd_nodes);
    BOR_INFO(err, "Fw Expanded States: %lu",
             ss->search_fw.num_expanded_states);
    BOR_INFO(err, "Fw Avg. Expanded BDD Nodes: %.2f",
             ss->search_fw.avg_expanded_bdd_nodes);

    BOR_INFO(err, "Bw Expanded BDD Nodes: %lu",
             ss->search_bw.num_expanded_bdd_nodes);
    BOR_INFO(err, "Bw Expanded States: %lu",
             ss->search_bw.num_expanded_states);
    BOR_INFO(err, "Bw Avg. Expanded BDD Nodes: %.2f",
             ss->search_bw.avg_expanded_bdd_nodes);

    BOR_INFO(err, "Expanded BDD Nodes: %lu",
             ss->search_fw.num_expanded_bdd_nodes
                + ss->search_bw.num_expanded_bdd_nodes);
    BOR_INFO(err, "Expanded States: %lu",
             ss->search_fw.num_expanded_states + ss->search_bw.num_expanded_states);
    float avg = ss->search_fw.avg_expanded_bdd_nodes
                    * ss->search_fw.num_expanded_states;
    avg += ss->search_bw.avg_expanded_bdd_nodes
                * ss->search_bw.num_expanded_states;
    avg /= ss->search_fw.num_expanded_states + ss->search_bw.num_expanded_states;
    BOR_INFO(err, "Avg. Expanded BDD Nodes: %.2f", avg);

#ifdef PDDL_DEBUG
    int op_id;
    PDDL_IARR_FOR_EACH(plan, op_id){
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

int pddlSymbolicTaskSearch(pddl_symbolic_task_t *ss,
                           pddl_iarr_t *plan,
                           bor_err_t *err)
{
    if (ss->cfg.fw.enabled && ss->cfg.bw.enabled){
        return pddlSymbolicTaskSearchFwBw(ss, plan, err);
    }else if (ss->cfg.fw.enabled){
        return pddlSymbolicTaskSearchFw(ss, plan, err);
    }else if (ss->cfg.bw.enabled){
        return pddlSymbolicTaskSearchBw(ss, plan, err);
    }else{
        BOR_ERR_RET2(err, -1, "Neither of search directions was initialized");
    }
}

static pddl_bdd_t *createFDRState(pddl_symbolic_task_t *ss, const int *state)
{
    PDDL_ISET(st);
    for (int i = 0; i < ss->fdr.var.var_size; ++i)
        pddlISetAdd(&st, ss->fdr.var.var[i].val[state[i]].global_id);
    pddl_bdd_t *bdd_state = pddlSymbolicVarsCreateState(&ss->vars, &st);
    pddlISetFree(&st);
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
    for (int tri = 0; res && tri < ss->search_fw.trans.trans_size; ++tri){
        pddl_symbolic_trans_set_t *trs = ss->search_fw.trans.trans + tri;
        if (!pddlISetIn(op_id, &trs->op))
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
    for (int tri = 0; res && tri < ss->search_bw.trans.trans_size; ++tri){
        pddl_symbolic_trans_set_t *trs = ss->search_bw.trans.trans + tri;
        if (!pddlISetIn(op_id, &trs->op))
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
                              const pddl_iarr_t *op,
                              int plan_size)
{
    int res = 1;
    pddl_bdd_t **fw_node = ALLOC_ARR(pddl_bdd_t *, plan_size + 1);
    pddl_bdd_t **bw_node = ALLOC_ARR(pddl_bdd_t *, plan_size + 1);
    fw_node[0] = pddlBDDClone(ss->mgr, ss->init);
    bw_node[plan_size] = pddlBDDClone(ss->mgr, ss->goal);
    pddl_bdd_t *fw_closed = pddlBDDClone(ss->mgr, fw_node[0]);
    pddl_bdd_t *bw_closed = pddlBDDClone(ss->mgr, bw_node[plan_size]);
    for (int fi = 0; fi < plan_size; ++fi){
        int fw_op_id = pddlIArrGet(op, fi);
        for (int tri = 0; tri < ss->search_fw.trans.trans_size; ++tri){
            pddl_symbolic_trans_set_t *trs = ss->search_fw.trans.trans + tri;
            if (!pddlISetIn(fw_op_id, &trs->op))
                continue;
            fw_node[fi + 1] = pddlSymbolicTransSetImage(trs, fw_node[fi]);
            if (ss->cfg.fw.use_op_constr){
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

            }else if (ss->cfg.fw.use_constr){
                pddlSymbolicConstrApplyFw(&ss->constr, &fw_node[fi + 1]);
            }

            pddl_bdd_t *nclosed = pddlBDDNot(ss->mgr, fw_closed);
            pddlBDDAndUpdate(ss->mgr, &fw_node[fi + 1], nclosed);
            pddlBDDDel(ss->mgr, nclosed);
            pddlBDDOrUpdate(ss->mgr, &fw_closed, fw_node[fi + 1]);
        }

        int bw_op_id = pddlIArrGet(op, plan_size - fi - 1);
        for (int tri = 0; tri < ss->search_bw.trans.trans_size; ++tri){
            pddl_symbolic_trans_set_t *trs = ss->search_bw.trans.trans + tri;
            if (!pddlISetIn(bw_op_id, &trs->op))
                continue;
            int fi2 = plan_size - fi;
            bw_node[fi2 - 1] = pddlSymbolicTransSetPreImage(trs, bw_node[fi2]);
            if (ss->cfg.bw.use_op_constr){
                pddl_bdd_t *tmp = pddlBDDClone(ss->mgr, bw_node[fi2 - 1]);
                pddlSymbolicConstrApplyBw(&ss->constr, &tmp);
                //if (tmp != bw_node[fi2 - 1])
                //    res = 0;
                //ASSERT(tmp == bw_node[fi2 - 1]);
                pddlBDDDel(ss->mgr, tmp);

            }else if (ss->cfg.bw.use_constr){
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
    FREE(fw_node);
    FREE(bw_node);

    return res;
}
