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
#include "pddl/search_astar.h"
#include "assert.h"


pddl_search_astar_t *pddlSearchAStar(const pddl_fdr_t *fdr,
                                     int use_pathmax)
{
    pddl_search_astar_t *astar;

    astar = BOR_ALLOC(pddl_search_astar_t);
    astar->fdr = fdr;
    astar->pathmax = use_pathmax;

    pddlFDRStateSpaceInit(&astar->state_space, &fdr->var);
    astar->list = pddlOpenListSplayTree();

    pddlFDRAppOpInit(&astar->app_op, &fdr->var, &fdr->op, &fdr->goal);

    astar->goal_state_id = PDDL_NO_STATE_ID;

    borISetInit(&astar->applicable);
    pddlFDRStateSpaceNodeInit(&astar->cur_node, &astar->state_space);
    pddlFDRStateSpaceNodeInit(&astar->next_node, &astar->state_space);

    return astar;
}

void pddlSearchAStarDel(pddl_search_astar_t *astar)
{
    pddlFDRAppOpFree(&astar->app_op);
    if (astar->list)
        pddlOpenListDel(astar->list);
    pddlFDRStateSpaceNodeFree(&astar->cur_node);
    pddlFDRStateSpaceNodeFree(&astar->next_node);
    pddlFDRStateSpaceFree(&astar->state_space);
    borISetFree(&astar->applicable);
    BOR_FREE(astar);
}

static void push(pddl_search_astar_t *astar,
                 pddl_fdr_state_space_node_t *node)
{
    int cost = node->g_value + node->h_value;
    node->status = PDDL_FDR_STATE_SPACE_STATUS_OPEN;
    pddlOpenListPush(astar->list, cost, node->id);
    // TODO
    //++search->stat.open;
}

int pddlSearchAStarInitStep(pddl_search_astar_t *astar)
{
    pddl_state_id_t state_id;
    state_id = pddlFDRStateSpaceInsert(&astar->state_space, astar->fdr->init);
    ASSERT_RUNTIME(state_id == 0);
    pddlFDRStateSpaceGetNoState(&astar->state_space,
                                state_id, &astar->cur_node);
    astar->cur_node.parent_id = PDDL_NO_STATE_ID;
    astar->cur_node.op_id = -1;
    astar->cur_node.g_value = 0;

    // TODO
    astar->cur_node.h_value = 0;

    pddlFDRStateSpaceSet(&astar->state_space, &astar->cur_node);

    ASSERT_RUNTIME(astar->cur_node.status == PDDL_FDR_STATE_SPACE_STATUS_NEW);
    push(astar, &astar->cur_node);
    // TODO
    //++search->stat.generated;
    return PDDL_SEARCH_CONT;
}

#if 0
static void insertState(pddl_search_astar_t *astar,
                        const pddl_fdr_op_t *op,
                        pddl_fdr_state_space_node_t *parent_node)
{
    int heur, g_value = 0;
    pddl_state_id_t parent_state_id = PDDL_NO_STATE_ID;
    pddl_fdr_state_space_node_t *node = &astar->next_node;

    if (parent_node){
        g_value += parent_node->g_value;
        parent_state_id = parent_node->state_id;
    }

    if (op)
        g_value += op->cost;

    node->parent_state_id = parent_state_id;
    node->op              = op;
    node->cost            = g_value;

    // Force to open the node and compute heuristic if necessary
    if (planStateSpaceNodeIsNew(node)){
        ++search->stat.generated;
        planStateSpaceOpen(&search->state_space, node);

        // Compute heuristic value
        _pddlSearchNextHeur(search);
        heur = search->next_heur.heur;

        if (astar->pathmax && op != NULL && parent_node != NULL)
            heur = BOR_MAX(heur, parent_node->heur - op->cost);

    }else{
        if (planStateSpaceNodeIsClosed(node)){
            planStateSpaceReopen(&search->state_space, node);
            ++search->stat.reopen;
        }

        // The node was already opened, so we have already computed
        // heuristic
        heur = node->heur;
    }

    // Set heuristic value to the node
    node->heur = heur;

    // Skip dead-end states
    if (heur == PLAN_COST_DEAD_END){
        planStateSpaceClose(&search->state_space, node);
        ++search->stat.closed;
        ++search->stat.dead_end;
        return;
    }

    // Set up costs for open-list -- ties are broken by heuristic value
    heur = BOR_MAX(heur, 0);
    cost[0] = g_value + heur; // f-value: f() = g() + h()
    cost[1] = heur; // tie-breaking value

    // Insert into open-list
    planListPush(astar->list, cost, node->state_id);
    ++search->stat.open;
    // TODO: stats: pddlSearchStatIncGeneratedStates(&search->stat);
}
#endif

static int isGoal(const pddl_search_astar_t *astar)
{
    return pddlFDRPartStateIsConsistentWithState(&astar->fdr->goal,
                                                 astar->cur_node.state);
}

static void insertNextState(pddl_search_astar_t *astar,
                            const pddl_fdr_op_t *op)
{
    // Compute its g() value
    int next_g_value = astar->cur_node.g_value + op->cost;

    // Skip if we have better state already
    if (astar->next_node.status != PDDL_FDR_STATE_SPACE_STATUS_NEW
            && astar->next_node.g_value <= next_g_value){
        return;
    }

    astar->next_node.parent_id = astar->cur_node.id;
    astar->next_node.op_id = op->id;
    astar->next_node.g_value = next_g_value;
    astar->next_node.h_value = 0; // TODO
    /* TODO
    if (astar->pathmax)
        heur = BOR_MAX(heur, parent_node->heur - op->cost);
    */

    if (astar->next_node.h_value == PDDL_COST_DEAD_END){
        astar->next_node.status = PDDL_FDR_STATE_SPACE_STATUS_CLOSED;
        // TODO
        //if (astar->next_node.status != PDDL_FDR_STATE_SPACE_STATUS_CLOSED){
        //++search->stat.closed;
        //++search->stat.dead_end;
        //}

    }else if (astar->next_node.status == PDDL_FDR_STATE_SPACE_STATUS_NEW){
        push(astar, &astar->next_node);

    }else if (astar->next_node.status == PDDL_FDR_STATE_SPACE_STATUS_CLOSED){
        push(astar, &astar->next_node);
        // TODO
        //++search->stat.reopen;
    }

    pddlFDRStateSpaceSet(&astar->state_space, &astar->next_node);
}

int pddlSearchAStarStep(pddl_search_astar_t *astar)
{
    int cur_f_value;
    pddl_state_id_t cur_state_id;

    // Get next state from open list
    if (pddlOpenListPop(astar->list, &cur_state_id, &cur_f_value) != 0)
        return PDDL_SEARCH_UNSOLVABLE;
    // TODO
    //--search->stat.open;

    // Load the current state
    pddlFDRStateSpaceGet(&astar->state_space, cur_state_id, &astar->cur_node);

    // Skip already closed nodes
    if (astar->cur_node.status != PDDL_FDR_STATE_SPACE_STATUS_OPEN)
        return PDDL_SEARCH_CONT;

    // Close the current state node
    astar->cur_node.status = PDDL_FDR_STATE_SPACE_STATUS_CLOSED;
    // TODO
    //++search->stat.closed;

    // Check whether it is a goal
    if (isGoal(astar)){
        astar->goal_state_id = cur_state_id;
        return PDDL_SEARCH_FOUND;
    }

    // Find all applicable operators
    borISetEmpty(&astar->applicable);
    pddlFDRAppOpFind(&astar->app_op, astar->cur_node.state, &astar->applicable);
    // TODO: stats: pddlSearchStatIncExpandedStates(&search->stat);

    int op_id;
    BOR_ISET_FOR_EACH(&astar->applicable, op_id){
        const pddl_fdr_op_t *op = astar->fdr->op.op[op_id];

        // Create a new state
        pddlFDROpApplyOnState2(op, astar->next_node.var_size,
                               astar->cur_node.state,
                               astar->next_node.state);

        // Insert the new state
        pddl_state_id_t next_state_id;
        next_state_id = pddlFDRStateSpaceInsert(&astar->state_space,
                                                astar->next_node.state);
        pddlFDRStateSpaceGetNoState(&astar->state_space,
                                    next_state_id, &astar->next_node);
        // TODO
        //++search->stat.generated;
        insertNextState(astar, op);
    }
    return PDDL_SEARCH_CONT;
}
