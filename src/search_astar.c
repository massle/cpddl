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
    bzero(astar, sizeof(*astar));
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
    if (node->status == PDDL_FDR_STATE_SPACE_STATUS_CLOSED)
        --astar->_stat.closed;
    node->status = PDDL_FDR_STATE_SPACE_STATUS_OPEN;
    pddlOpenListPush(astar->list, cost, node->id);
    ++astar->_stat.open;
}

int pddlSearchAStarInitStep(pddl_search_astar_t *astar)
{
    int ret = PDDL_SEARCH_CONT;
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
    ++astar->_stat.evaluated;
    if (astar->cur_node.h_value == PDDL_COST_DEAD_END){
        ++astar->_stat.dead_end;
        ret = PDDL_SEARCH_UNSOLVABLE;
    }

    ASSERT_RUNTIME(astar->cur_node.status == PDDL_FDR_STATE_SPACE_STATUS_NEW);
    push(astar, &astar->cur_node);
    pddlFDRStateSpaceSet(&astar->state_space, &astar->cur_node);
    return ret;
}

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

    if (astar->next_node.status == PDDL_FDR_STATE_SPACE_STATUS_NEW){
        astar->next_node.h_value = 0; // TODO
        ++astar->_stat.evaluated;
        if (astar->next_node.h_value == PDDL_COST_DEAD_END){
            ++astar->_stat.dead_end;
        }else if (astar->pathmax){
            int parent_heur = astar->cur_node.h_value + op->cost;
            int heur = astar->next_node.h_value;
            heur = BOR_MAX(heur, parent_heur);
            astar->next_node.h_value = heur;
        }
    }

    if (astar->next_node.h_value == PDDL_COST_DEAD_END){
        if (astar->next_node.status == PDDL_FDR_STATE_SPACE_STATUS_OPEN)
            --astar->_stat.open;
        astar->next_node.status = PDDL_FDR_STATE_SPACE_STATUS_CLOSED;
        ++astar->_stat.closed;

    }else if (astar->next_node.status == PDDL_FDR_STATE_SPACE_STATUS_NEW){
        push(astar, &astar->next_node);

    }else if (astar->next_node.status == PDDL_FDR_STATE_SPACE_STATUS_CLOSED){
        push(astar, &astar->next_node);
        ++astar->_stat.reopen;
    }

    pddlFDRStateSpaceSet(&astar->state_space, &astar->next_node);
}

int pddlSearchAStarStep(pddl_search_astar_t *astar)
{
    int cur_f_value;
    pddl_state_id_t cur_state_id;

    ++astar->_stat.steps;

    // Get next state from open list
    if (pddlOpenListPop(astar->list, &cur_state_id, &cur_f_value) != 0)
        return PDDL_SEARCH_UNSOLVABLE;

    // Load the current state
    pddlFDRStateSpaceGet(&astar->state_space, cur_state_id, &astar->cur_node);

    // Skip already closed nodes
    if (astar->cur_node.status != PDDL_FDR_STATE_SPACE_STATUS_OPEN)
        return PDDL_SEARCH_CONT;

    // Close the current node
    astar->cur_node.status = PDDL_FDR_STATE_SPACE_STATUS_CLOSED;
    --astar->_stat.open;
    ++astar->_stat.closed;
    astar->_stat.last_f_value = cur_f_value;

    // Check whether it is a goal
    if (isGoal(astar)){
        astar->goal_state_id = cur_state_id;
        return PDDL_SEARCH_FOUND;
    }

    // Find all applicable operators
    borISetEmpty(&astar->applicable);
    pddlFDRAppOpFind(&astar->app_op, astar->cur_node.state, &astar->applicable);
    ++astar->_stat.expanded;

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
        insertNextState(astar, op);
    }
    return PDDL_SEARCH_CONT;
}

void pddlSearchAStarStat(const pddl_search_astar_t *astar,
                         pddl_search_stat_t *stat)
{
    *stat = astar->_stat;
    stat->generated = astar->state_space.state_pool.num_states;
}
