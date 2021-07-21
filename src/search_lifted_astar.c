/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
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
#include "pddl/search_lifted_astar.h"
#include "assert.h"


pddl_search_lifted_astar_t *pddlSearchLiftedAStar(const pddl_t *pddl,
                                                  bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Lifted A*: ");
    pddl_search_lifted_astar_t *astar;

    astar = BOR_ALLOC(pddl_search_lifted_astar_t);
    bzero(astar, sizeof(*astar));
    astar->pddl = pddl;
    // TODO: Check for conditional effects
    // TODO
    //astar->heur = heur;
    astar->err = err;

    astar->grounder = pddlSqlGrounderNew(pddl, err);
    pddlStripsMakerInit(&astar->strips, pddl);
    pddlStripsStateSpaceInit(&astar->state_space, err);
    astar->list = pddlOpenListSplayTree2();

    borISetInit(&astar->applicable);
    pddlStripsStateSpaceNodeInit(&astar->cur_node, &astar->state_space);
    pddlStripsStateSpaceNodeInit(&astar->next_node, &astar->state_space);

    BOR_INFO_PREFIX_POP(err);
    return astar;
}

void pddlSearchLiftedAStarDel(pddl_search_lifted_astar_t *astar)
{
    bor_err_t *err = astar->err;
    BOR_INFO_PREFIX_PUSH(err, "Lifted A*: ");
    if (astar->list)
        pddlOpenListDel(astar->list);
    pddlStripsStateSpaceNodeFree(&astar->cur_node);
    pddlStripsStateSpaceNodeFree(&astar->next_node);
    pddlStripsStateSpaceFree(&astar->state_space);
    borISetFree(&astar->applicable);
    pddlStripsMakerFree(&astar->strips);
    pddlSqlGrounderDel(astar->grounder);
    borISetFree(&astar->goal);
    for (int i = 0; i < astar->plan.plan_len; ++i)
        BOR_FREE(astar->plan.plan[i]);
    if (astar->plan.plan != NULL)
        BOR_FREE(astar->plan.plan);
    BOR_FREE(astar);
    BOR_INFO_PREFIX_POP(err);
}

static void push(pddl_search_lifted_astar_t *astar,
                 pddl_strips_state_space_node_t *node,
                 int h_value)
{
    int cost[2];
    cost[0] = node->g_value + h_value;
    cost[1] = h_value;
    if (node->status == PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED)
        --astar->_stat.closed;
    node->status = PDDL_STRIPS_STATE_SPACE_STATUS_OPEN;
    pddlOpenListPush(astar->list, cost, node->id);
    ++astar->_stat.open;
}

static int _setGoal(pddl_cond_t *c, void *_astar)
{
    pddl_search_lifted_astar_t *astar = _astar;
    const pddl_t *pddl = astar->pddl;

    if (c->type == PDDL_COND_ATOM){
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        if (pddlPredIsStatic(&pddl->pred.pred[atom->pred])){
            const pddl_ground_atom_t *ga;
            ga = pddlGroundAtomsFindAtom(&astar->strips.ground_atom_static,
                                         atom, NULL);
            if (ga == NULL){
                astar->goal_is_unreachable = 1;
                return -1;
            }

        }else{
            const pddl_ground_atom_t *ga;
            ga = pddlStripsMakerAddAtom(&astar->strips, atom, NULL, NULL);
            borISetAdd(&astar->goal, ga->id);
        }
        if (!pddlCondAtomIsGrounded(atom)){
            astar->goal_is_unreachable = 1;
            BOR_ERR_RET2(astar->err, -1, "Goal specification cannot contain"
                         " parametrized atoms.");
        }

        return 0;

    }else if (c->type == PDDL_COND_AND){
        return 0;

    }else if (c->type == PDDL_COND_BOOL){
        const pddl_cond_bool_t *b = PDDL_COND_CAST(c, bool);
        if (!b->val)
            astar->goal_is_unreachable = 1;
        return 0;

    }else{
        BOR_ERR(astar->err, "Only conjuctive goal specifications are supported."
                " (Goal contains %s.)", pddlCondTypeName(c->type));
        astar->goal_is_unreachable = 1;
        return -2;
    }
}

static void setGoal(pddl_search_lifted_astar_t *astar)
{
    borISetEmpty(&astar->goal);
    pddlCondTraverse(astar->pddl->goal, _setGoal, NULL, astar);
}

static pddl_state_id_t insertInitState(pddl_search_lifted_astar_t *astar)
{
    const pddl_t *pddl = astar->pddl;
    bor_list_t *item;
    BOR_ISET(init);
    BOR_LIST_FOR_EACH(&pddl->init->part, item){
        const pddl_cond_t *c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type == PDDL_COND_ATOM){
            const pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
            const pddl_ground_atom_t *ga;
            if (pddlPredIsStatic(&pddl->pred.pred[a->pred])){
                pddlStripsMakerAddStaticAtom(&astar->strips, a, NULL, NULL);
            }else{
                ga = pddlStripsMakerAddAtom(&astar->strips, a, NULL, NULL);
                borISetAdd(&init, ga->id);
            }
            pddlSqlGrounderInsertAtom(astar->grounder, a, astar->err);

        }else if (c->type == PDDL_COND_ASSIGN){
            const pddl_cond_func_op_t *ass = PDDL_COND_CAST(c, func_op);
            ASSERT(ass->fvalue == NULL);
            ASSERT(ass->lvalue != NULL);
            ASSERT(pddlCondAtomIsGrounded(ass->lvalue));
            pddlStripsMakerAddFunc(&astar->strips, ass, NULL, NULL);
        }
    }

    pddl_state_id_t sid;
    sid = pddlStripsStateSpaceInsert(&astar->state_space, &init);
    borISetFree(&init);

    return sid;
}

int pddlSearchLiftedAStarInitStep(pddl_search_lifted_astar_t *astar)
{
    BOR_INFO_PREFIX_PUSH(astar->err, "Lifted A*: ");
    int ret = PDDL_SEARCH_CONT;

    pddl_state_id_t state_id = insertInitState(astar);
    ASSERT_RUNTIME(state_id == 0);

    setGoal(astar);
    if (astar->goal_is_unreachable)
        ret = PDDL_SEARCH_UNSOLVABLE;

    pddlStripsStateSpaceGet(&astar->state_space, state_id, &astar->cur_node);
    astar->cur_node.parent_id = PDDL_NO_STATE_ID;
    astar->cur_node.op_id = -1;
    astar->cur_node.g_value = 0;

    /* TODO
    int h_value = pddlHeurEstimate(astar->heur,
                                   &astar->cur_node,
                                   &astar->state_space);
    */
    int h_value = 0;
    BOR_INFO(astar->err, "Heuristic value for the initial state: %d", h_value);
    ++astar->_stat.evaluated;
    if (h_value == PDDL_COST_DEAD_END){
        ++astar->_stat.dead_end;
        ret = PDDL_SEARCH_UNSOLVABLE;
    }

    ASSERT_RUNTIME(astar->cur_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_NEW);
    push(astar, &astar->cur_node, h_value);
    pddlStripsStateSpaceSet(&astar->state_space, &astar->cur_node);
    BOR_INFO_PREFIX_POP(astar->err);
    return ret;
}

static int isGoal(const pddl_search_lifted_astar_t *astar)
{
    return borISetIsSubset(&astar->goal, &astar->cur_node.state);
}

// TODO
static void insertNextState(pddl_search_lifted_astar_t *astar,
                            int args_id,
                            int op_cost)
{
    // Compute its g() value
    int next_g_value = astar->cur_node.g_value + op_cost;

    // Skip if we have better state already
    if (astar->next_node.status != PDDL_STRIPS_STATE_SPACE_STATUS_NEW
            && astar->next_node.g_value <= next_g_value){
        return;
    }

    astar->next_node.parent_id = astar->cur_node.id;
    astar->next_node.op_id = args_id;
    astar->next_node.g_value = next_g_value;

    /* TODO
    int h_value = pddlHeurEstimate(astar->heur, &astar->next_node,
                                   &astar->state_space);
    */
    int h_value = 0;
    ++astar->_stat.evaluated;

    if (h_value == PDDL_COST_DEAD_END){
        ++astar->_stat.dead_end;
        if (astar->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_OPEN)
            --astar->_stat.open;
        astar->next_node.status = PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED;
        ++astar->_stat.closed;

    }else if (astar->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_NEW
                || astar->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_OPEN){
        push(astar, &astar->next_node, h_value);

    }else if (astar->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED){
        push(astar, &astar->next_node, h_value);
        ++astar->_stat.reopen;
    }

    pddlStripsStateSpaceSet(&astar->state_space, &astar->next_node);
}

static void findApplicableOpsAction(pddl_search_lifted_astar_t *astar,
                                    int action_id,
                                    bor_iset_t *applicable_ops)
{
    if (pddlSqlGrounderActionStart(astar->grounder, action_id, astar->err) != 0)
        return;

    const pddl_prep_action_t *paction;
    paction = pddlSqlGrounderPrepAction(astar->grounder, action_id);
    ASSERT(paction->parent_action < 0);

    pddl_obj_id_t row[paction->param_size];
    while (pddlSqlGrounderActionNext(astar->grounder, row, astar->err)){
        pddl_ground_action_args_t *a;
        a = pddlStripsMakerAddAction(&astar->strips, action_id, 0, row, NULL);
        borISetAdd(applicable_ops, a->id);
    }
}

static void findApplicableOps(pddl_search_lifted_astar_t *astar,
                              bor_iset_t *applicable_ops)
{
    pddlSqlGrounderClearNonStatic(astar->grounder, astar->err);
    int fact;
    BOR_ISET_FOR_EACH(&astar->cur_node.state, fact){
        const pddl_ground_atom_t *ga;
        ga = pddlStripsMakerGroundAtom(&astar->strips, fact);
        pddlSqlGrounderInsertGroundAtom(astar->grounder, ga, astar->err);
    }
    int action_size = pddlSqlGrounderPrepActionSize(astar->grounder);
    for (int ai = 0; ai < action_size; ++ai)
        findApplicableOpsAction(astar, ai, applicable_ops);
}

static void applyAction(pddl_search_lifted_astar_t *astar,
                        int args_id,
                        int *cost)
{
    const pddl_ground_action_args_t *aargs;
    aargs = pddlStripsMakerActionArgs(&astar->strips, args_id);
    const pddl_prep_action_t *pa;
    pa = pddlSqlGrounderPrepAction(astar->grounder, aargs->action_id);

    BOR_ISET(eadd);
    BOR_ISET(edel);
    for (int i = 0; i < pa->add_eff.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(pa->add_eff.cond[i], atom);
        ASSERT(!pddlPredIsStatic(&astar->pddl->pred.pred[atom->pred]));

        pddl_ground_atom_t *ga;
        ga = pddlStripsMakerAddAtom(&astar->strips, atom, aargs->arg, NULL);
        borISetAdd(&eadd, ga->id);
    }

    for (int i = 0; i < pa->del_eff.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(pa->del_eff.cond[i], atom);
        ASSERT(!pddlPredIsStatic(&astar->pddl->pred.pred[atom->pred]));

        pddl_ground_atom_t *ga;
        ga = pddlStripsMakerAddAtom(&astar->strips, atom, aargs->arg, NULL);
        borISetAdd(&edel, ga->id);
    }

    *cost = 0;
    for (int i = 0; i < pa->increase.size && astar->pddl->metric; ++i){
        const pddl_cond_func_op_t *inc;
        inc = PDDL_COND_CAST(pa->increase.cond[i], func_op);
        if (inc->fvalue != NULL){
            const pddl_ground_atom_t *ga;
            ga = pddlGroundAtomsFindAtom(&astar->strips.ground_func,
                                         inc->fvalue, aargs->arg);
            *cost += ga->func_val;
        }else{
            *cost += inc->value;
        }
    }
    if (!astar->pddl->metric)
        *cost = 1;

    borISetMinus2(&astar->next_node.state, &astar->cur_node.state, &edel);
    borISetUnion(&astar->next_node.state, &eadd);
    borISetFree(&eadd);
    borISetFree(&edel);
}

static void addPlanOp(pddl_search_lifted_astar_t *astar, int op_id)
{
    pddl_lifted_plan_t *plan = &astar->plan;
    if (plan->plan_alloc == plan->plan_len){
        if (plan->plan_alloc == 0)
            plan->plan_alloc = 2;
        plan->plan_alloc *= 2;
        plan->plan = BOR_REALLOC_ARR(plan->plan, char *, plan->plan_alloc);
    }

    const pddl_ground_action_args_t *aargs;
    aargs = pddlStripsMakerActionArgs(&astar->strips, op_id);
    const pddl_prep_action_t *pa;
    pa = pddlSqlGrounderPrepAction(astar->grounder, aargs->action_id);
    const pddl_action_t *action = pa->action;

    static int maxlen = 512;
    char name[maxlen + 1];
    int len = snprintf(name, maxlen, "%s", action->name);
    for (int i = 0; i < pa->param_size; ++i){
        len += snprintf(name + len, maxlen - len, " %s",
                        astar->pddl->obj.obj[aargs->arg[i]].name);
    }
    name[maxlen] = 0;
    plan->plan[plan->plan_len++] = BOR_STRDUP(name);
}

static void extractPlan(pddl_search_lifted_astar_t *astar,
                        pddl_state_id_t goal_state_id)
{
    pddlStripsStateSpaceGetNoState(&astar->state_space, goal_state_id,
                                   &astar->cur_node);
    astar->plan.plan_cost = astar->cur_node.g_value;

    pddl_state_id_t state_id = goal_state_id;
    while (state_id != 0){
        pddlStripsStateSpaceGetNoState(&astar->state_space, state_id,
                                       &astar->cur_node);
        addPlanOp(astar, astar->cur_node.op_id);
        state_id = astar->cur_node.parent_id;
    }

    for (int i = 0; i < astar->plan.plan_len / 2; ++i){
        int j = astar->plan.plan_len - 1 - i;
        char *tmp;
        BOR_SWAP(astar->plan.plan[i], astar->plan.plan[j], tmp);
    }
}

int pddlSearchLiftedAStarStep(pddl_search_lifted_astar_t *astar)
{
    BOR_INFO_PREFIX_PUSH(astar->err, "Lifted A*: ");

    ++astar->_stat.steps;

    // Get next state from open list
    int cur_cost[2];
    pddl_state_id_t cur_state_id;
    if (pddlOpenListPop(astar->list, &cur_state_id, cur_cost) != 0){
        BOR_INFO_PREFIX_POP(astar->err);
        return PDDL_SEARCH_UNSOLVABLE;
    }

    // Load the current state
    pddlStripsStateSpaceGet(&astar->state_space, cur_state_id, &astar->cur_node);

    // Skip already closed nodes
    if (astar->cur_node.status != PDDL_STRIPS_STATE_SPACE_STATUS_OPEN){
        BOR_INFO_PREFIX_POP(astar->err);
        return PDDL_SEARCH_CONT;
    }

    // Close the current node
    astar->cur_node.status = PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED;
    pddlStripsStateSpaceSet(&astar->state_space, &astar->cur_node);
    --astar->_stat.open;
    ++astar->_stat.closed;
    astar->_stat.last_f_value = cur_cost[0];

    // Check whether it is a goal
    if (isGoal(astar)){
        extractPlan(astar, cur_state_id);
        BOR_INFO_PREFIX_POP(astar->err);
        return PDDL_SEARCH_FOUND;
    }

    // Find all applicable operators
    borISetEmpty(&astar->applicable);
    findApplicableOps(astar, &astar->applicable);
    ++astar->_stat.expanded;

    int args_id;
    BOR_ISET_FOR_EACH(&astar->applicable, args_id){
        int cost;
        applyAction(astar, args_id, &cost);

        // Insert the new state
        pddl_state_id_t next_state_id;
        next_state_id = pddlStripsStateSpaceInsert(&astar->state_space,
                                                   &astar->next_node.state);
        pddlStripsStateSpaceGetNoState(&astar->state_space,
                                       next_state_id, &astar->next_node);
        insertNextState(astar, args_id, cost);
    }
    BOR_INFO_PREFIX_POP(astar->err);
    return PDDL_SEARCH_CONT;
}

void pddlSearchLiftedAStarStat(const pddl_search_lifted_astar_t *astar,
                               pddl_search_stat_t *stat)
{
    *stat = astar->_stat;
    stat->generated = astar->state_space.num_states;
}
