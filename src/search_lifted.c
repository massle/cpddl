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

#include "pddl/open_list.h"
#include "pddl/strips_state_space.h"
#include "pddl/strips_maker.h"
#include "pddl/sql_grounder.h"
#include "pddl/search_lifted.h"
#include "alloc.h"
#include "assert.h"
#include "err.h"

typedef void (*search_del_fn)(pddl_search_lifted_t *);
typedef int (*search_init_step_fn)(pddl_search_lifted_t *);
typedef int (*search_step_fn)(pddl_search_lifted_t *);

struct pddl_search_lifted {
    const pddl_t *pddl;
    pddl_err_t *err;
    pddl_sql_grounder_t *grounder;
    pddl_strips_maker_t strips;
    pddl_strips_state_space_t state_space;

    pddl_iset_t applicable;
    pddl_strips_state_space_node_t cur_node;
    pddl_strips_state_space_node_t next_node;
    pddl_search_stat_t _stat;

    pddl_iset_t goal;
    int goal_is_unreachable;
    pddl_lifted_plan_t plan;

    search_del_fn del_fn;
    search_init_step_fn init_step_fn;
    search_step_fn step_fn;

    const char *err_prefix;
};

struct pddl_search_lifted_bfs {
    pddl_search_lifted_t search;
    pddl_homomorphism_heur_t *heur;
    int g_weight;
    int h_weight;
    int is_lazy;
    pddl_open_list_t *list;
};
typedef struct pddl_search_lifted_bfs pddl_search_lifted_bfs_t;

#define BFS(S) pddl_container_of((S), pddl_search_lifted_bfs_t, search)

static void setGoal(pddl_search_lifted_t *s);
static pddl_state_id_t insertInitState(pddl_search_lifted_t *s);
static int isGoal(const pddl_search_lifted_t *s);
static void findApplicableOps(pddl_search_lifted_t *s,
                              pddl_iset_t *applicable_ops);
static void applyAction(pddl_search_lifted_t *s, int args_id, int *cost);
static void extractPlan(pddl_search_lifted_t *s, pddl_state_id_t goal_state_id);

static int searchInit(pddl_search_lifted_t *s,
                      const pddl_t *pddl,
                      search_del_fn del_fn,
                      search_init_step_fn init_step_fn,
                      search_step_fn step_fn,
                      const char *err_prefix,
                      pddl_err_t *err)
{
    s->pddl = pddl;
    s->err = err;

    s->grounder = pddlSqlGrounderNew(pddl, err);
    pddlStripsMakerInit(&s->strips, pddl);
    pddlStripsStateSpaceInit(&s->state_space, err);

    pddlISetInit(&s->applicable);
    pddlStripsStateSpaceNodeInit(&s->cur_node, &s->state_space);
    pddlStripsStateSpaceNodeInit(&s->next_node, &s->state_space);
    s->del_fn = del_fn;
    s->init_step_fn = init_step_fn;
    s->step_fn = step_fn;
    s->err_prefix = err_prefix;
    return 0;
}

static void searchFree(pddl_search_lifted_t *s)
{
    pddlStripsStateSpaceNodeFree(&s->cur_node);
    pddlStripsStateSpaceNodeFree(&s->next_node);
    pddlStripsStateSpaceFree(&s->state_space);
    pddlISetFree(&s->applicable);
    pddlStripsMakerFree(&s->strips);
    pddlSqlGrounderDel(s->grounder);
    pddlISetFree(&s->goal);
    for (int i = 0; i < s->plan.plan_len; ++i)
        FREE(s->plan.plan[i]);
    if (s->plan.plan != NULL)
        FREE(s->plan.plan);
}

static void bfsDel(pddl_search_lifted_t *bfs);
static int bfsInitStep(pddl_search_lifted_t *bfs);
static int bfsStep(pddl_search_lifted_t *bfs);

static pddl_search_lifted_t *bfsNew(const pddl_t *pddl,
                                    pddl_homomorphism_heur_t *heur,
                                    int g_weight,
                                    int h_weight,
                                    int is_lazy,
                                    const char *err_prefix,
                                    pddl_err_t *err)
{
    CTX(err, "bfs", err_prefix);
    pddl_search_lifted_bfs_t *bfs;

    bfs = ALLOC(pddl_search_lifted_bfs_t);
    bzero(bfs, sizeof(*bfs));
    searchInit(&bfs->search, pddl, bfsDel, bfsInitStep, bfsStep,
               err_prefix, err);
    // TODO: Check for conditional effects
    bfs->heur = heur;
    bfs->g_weight = g_weight;
    bfs->h_weight = h_weight;
    bfs->is_lazy = is_lazy;
    bfs->list = pddlOpenListSplayTree2();

    CTXEND(err);
    return &bfs->search;
}

static void bfsDel(pddl_search_lifted_t *s)
{
    pddl_err_t *err = s->err;
    CTX(err, "bfs", s->err_prefix);
    searchFree(s);

    pddl_search_lifted_bfs_t *bfs = BFS(s);
    if (bfs->list)
        pddlOpenListDel(bfs->list);
    FREE(bfs);
    CTXEND(err);
}


static void bfsPush(pddl_search_lifted_bfs_t *bfs,
                    pddl_strips_state_space_node_t *node,
                    int h_value)
{
    int cost[2];
    cost[0] = (bfs->g_weight * node->g_value) + (bfs->h_weight * h_value);
    cost[1] = h_value;
    if (node->status == PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED)
        --bfs->search._stat.closed;
    node->status = PDDL_STRIPS_STATE_SPACE_STATUS_OPEN;
    pddlOpenListPush(bfs->list, cost, node->id);
    ++bfs->search._stat.open;
}

static int bfsInitStep(pddl_search_lifted_t *s)
{
    pddl_search_lifted_bfs_t *bfs = BFS(s);
    CTX(s->err, "bfs", s->err_prefix);
    int ret = PDDL_SEARCH_CONT;

    pddl_state_id_t state_id = insertInitState(s);
    ASSERT_RUNTIME(state_id == 0);

    setGoal(s);
    if (s->goal_is_unreachable)
        ret = PDDL_SEARCH_UNSOLVABLE;

    pddlStripsStateSpaceGet(&s->state_space, state_id, &s->cur_node);
    s->cur_node.parent_id = PDDL_NO_STATE_ID;
    s->cur_node.op_id = -1;
    s->cur_node.g_value = 0;

    int h_value = 0;
    if (bfs->heur != NULL && !bfs->is_lazy){
        h_value = pddlHomomorphismHeurEval(bfs->heur,
                                           &s->cur_node.state,
                                           &s->strips.ground_atom);
        ++s->_stat.evaluated;
    }

    PDDL_INFO(s->err, "Heuristic value for the initial state: %d", h_value);
    if (h_value == PDDL_COST_DEAD_END){
        ++s->_stat.dead_end;
        ret = PDDL_SEARCH_UNSOLVABLE;
    }

    ASSERT_RUNTIME(s->cur_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_NEW);
    bfsPush(bfs, &s->cur_node, h_value);
    pddlStripsStateSpaceSet(&s->state_space, &s->cur_node);
    CTXEND(s->err);
    return ret;
}

static void bfsInsertNextState(pddl_search_lifted_bfs_t *bfs,
                               int args_id,
                               int op_cost,
                               int in_h_value)
{
    pddl_search_lifted_t *s = &bfs->search;
    // Compute its g() value
    int next_g_value = s->cur_node.g_value + op_cost;

    // Skip if we have better state already
    if (s->next_node.status != PDDL_STRIPS_STATE_SPACE_STATUS_NEW
            && s->next_node.g_value <= next_g_value){
        return;
    }

    s->next_node.parent_id = s->cur_node.id;
    s->next_node.op_id = args_id;
    s->next_node.g_value = next_g_value;
 
    int h_value = 0;
    if (in_h_value >= 0){
        h_value = in_h_value;
    }else if (bfs->heur != NULL){
        h_value = pddlHomomorphismHeurEval(bfs->heur,
                                           &s->next_node.state,
                                           &s->strips.ground_atom);
        ++s->_stat.evaluated;
    }

    if (h_value == PDDL_COST_DEAD_END){
        ++s->_stat.dead_end;
        if (s->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_OPEN)
            --s->_stat.open;
        s->next_node.status = PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED;
        ++s->_stat.closed;

    }else if (s->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_NEW
                || s->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_OPEN){
        bfsPush(bfs, &s->next_node, h_value);

    }else if (s->next_node.status == PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED){
        bfsPush(bfs, &s->next_node, h_value);
        ++s->_stat.reopen;
    }

    pddlStripsStateSpaceSet(&s->state_space, &s->next_node);
}

static int bfsStep(pddl_search_lifted_t *s)
{
    pddl_search_lifted_bfs_t *bfs = BFS(s);
    CTX(s->err, "bfs", s->err_prefix);

    ++s->_stat.steps;

    // Get next state from open list
    int cur_cost[2];
    pddl_state_id_t cur_state_id;
    if (pddlOpenListPop(bfs->list, &cur_state_id, cur_cost) != 0){
        CTXEND(s->err);
        return PDDL_SEARCH_UNSOLVABLE;
    }

    // Load the current state
    pddlStripsStateSpaceGet(&s->state_space, cur_state_id, &s->cur_node);

    // Skip already closed nodes
    if (s->cur_node.status != PDDL_STRIPS_STATE_SPACE_STATUS_OPEN){
        CTXEND(s->err);
        return PDDL_SEARCH_CONT;
    }

    // Close the current node
    s->cur_node.status = PDDL_STRIPS_STATE_SPACE_STATUS_CLOSED;
    pddlStripsStateSpaceSet(&s->state_space, &s->cur_node);
    --s->_stat.open;
    ++s->_stat.closed;
    s->_stat.last_f_value = cur_cost[0];

    // Check whether it is a goal
    if (isGoal(s)){
        extractPlan(s, cur_state_id);
        CTXEND(s->err);
        return PDDL_SEARCH_FOUND;
    }

    // Find all applicable operators
    pddlISetEmpty(&s->applicable);
    findApplicableOps(s, &s->applicable);
    ++s->_stat.expanded;

    int h_value = -1;
    if (pddlISetSize(&s->applicable) > 0 && bfs->is_lazy){
        h_value = 0;
        if (bfs->heur != NULL){
            h_value = pddlHomomorphismHeurEval(bfs->heur,
                                               &s->cur_node.state,
                                               &s->strips.ground_atom);
            ++s->_stat.evaluated;
        }
    }

    int args_id;
    PDDL_ISET_FOR_EACH(&s->applicable, args_id){
        int cost;
        applyAction(s, args_id, &cost);

        // Insert the new state
        pddl_state_id_t next_state_id;
        next_state_id = pddlStripsStateSpaceInsert(&s->state_space,
                                                   &s->next_node.state);
        pddlStripsStateSpaceGetNoState(&s->state_space,
                                       next_state_id, &s->next_node);
        bfsInsertNextState(bfs, args_id, cost, h_value);
    }
    CTXEND(s->err);
    return PDDL_SEARCH_CONT;
}




void pddlSearchLiftedDel(pddl_search_lifted_t *s)
{
    s->del_fn(s);
}

int pddlSearchLiftedInitStep(pddl_search_lifted_t *s)
{
    return s->init_step_fn(s);
}

int pddlSearchLiftedStep(pddl_search_lifted_t *s)
{
    return s->step_fn(s);
}

void pddlSearchLiftedStat(const pddl_search_lifted_t *s,
                          pddl_search_stat_t *stat)
{
    *stat = s->_stat;
    stat->generated = s->state_space.num_states;
}

void pddlSearchLiftedStatLog(const pddl_search_lifted_t *s, pddl_err_t *err)
{
    pddl_search_stat_t stat;
    pddlSearchLiftedStat(s, &stat);
    PDDL_INFO(err, "Search steps: %lu, expand: %lu, eval: %lu,"
              " gen: %lu, open: %lu, closed: %lu,"
              " reopen: %lu, de: %lu, f: %d",
              stat.steps,
              stat.expanded,
              stat.evaluated,
              stat.generated,
              stat.open,
              stat.closed,
              stat.reopen,
              stat.dead_end,
              stat.last_f_value);
}





static int _setGoal(pddl_cond_t *c, void *_s)
{
    pddl_search_lifted_t *s = _s;
    const pddl_t *pddl = s->pddl;

    if (c->type == PDDL_COND_ATOM){
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        if (pddlPredIsStatic(&pddl->pred.pred[atom->pred])){
            const pddl_ground_atom_t *ga;
            ga = pddlGroundAtomsFindAtom(&s->strips.ground_atom_static,
                                         atom, NULL);
            if (ga == NULL){
                s->goal_is_unreachable = 1;
                return -1;
            }

        }else{
            const pddl_ground_atom_t *ga;
            ga = pddlStripsMakerAddAtom(&s->strips, atom, NULL, NULL);
            pddlISetAdd(&s->goal, ga->id);
        }
        if (!pddlCondAtomIsGrounded(atom)){
            s->goal_is_unreachable = 1;
            PDDL_ERR_RET2(s->err, -1, "Goal specification cannot contain"
                          " parametrized atoms.");
        }

        return 0;

    }else if (c->type == PDDL_COND_AND){
        return 0;

    }else if (c->type == PDDL_COND_BOOL){
        const pddl_cond_bool_t *b = PDDL_COND_CAST(c, bool);
        if (!b->val)
            s->goal_is_unreachable = 1;
        return 0;

    }else{
        PDDL_ERR(s->err, "Only conjuctive goal specifications are supported."
                 " (Goal contains %s.)", pddlCondTypeName(c->type));
        s->goal_is_unreachable = 1;
        return -2;
    }
}

static void setGoal(pddl_search_lifted_t *s)
{
    pddlISetEmpty(&s->goal);
    pddlCondTraverse(s->pddl->goal, _setGoal, NULL, s);
}

static pddl_state_id_t insertInitState(pddl_search_lifted_t *s)
{
    const pddl_t *pddl = s->pddl;
    pddl_list_t *item;
    PDDL_ISET(init);
    PDDL_LIST_FOR_EACH(&pddl->init->part, item){
        const pddl_cond_t *c = PDDL_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type == PDDL_COND_ATOM){
            const pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
            const pddl_ground_atom_t *ga;
            if (pddlPredIsStatic(&pddl->pred.pred[a->pred])){
                pddlStripsMakerAddStaticAtom(&s->strips, a, NULL, NULL);
            }else{
                ga = pddlStripsMakerAddAtom(&s->strips, a, NULL, NULL);
                pddlISetAdd(&init, ga->id);
            }
            pddlSqlGrounderInsertAtom(s->grounder, a, s->err);

        }else if (c->type == PDDL_COND_ASSIGN){
            const pddl_cond_func_op_t *ass = PDDL_COND_CAST(c, func_op);
            ASSERT(ass->fvalue == NULL);
            ASSERT(ass->lvalue != NULL);
            ASSERT(pddlCondAtomIsGrounded(ass->lvalue));
            pddlStripsMakerAddFunc(&s->strips, ass, NULL, NULL);
        }
    }

    pddl_state_id_t sid;
    sid = pddlStripsStateSpaceInsert(&s->state_space, &init);
    pddlISetFree(&init);

    return sid;
}

static int isGoal(const pddl_search_lifted_t *s)
{
    return pddlISetIsSubset(&s->goal, &s->cur_node.state);
}


static void findApplicableOpsAction(pddl_search_lifted_t *s,
                                    int action_id,
                                    pddl_iset_t *applicable_ops)
{
    if (pddlSqlGrounderActionStart(s->grounder, action_id, s->err) != 0)
        return;

    const pddl_prep_action_t *paction;
    paction = pddlSqlGrounderPrepAction(s->grounder, action_id);
    ASSERT(paction->parent_action < 0);

    pddl_obj_id_t row[paction->param_size];
    while (pddlSqlGrounderActionNext(s->grounder, row, s->err)){
        pddl_ground_action_args_t *a;
        a = pddlStripsMakerAddAction(&s->strips, action_id, 0, row, NULL);
        pddlISetAdd(applicable_ops, a->id);
    }
}

static void findApplicableOps(pddl_search_lifted_t *s,
                              pddl_iset_t *applicable_ops)
{
    pddlSqlGrounderClearNonStatic(s->grounder, s->err);
    int fact;
    PDDL_ISET_FOR_EACH(&s->cur_node.state, fact){
        const pddl_ground_atom_t *ga;
        ga = pddlStripsMakerGroundAtom(&s->strips, fact);
        pddlSqlGrounderInsertGroundAtom(s->grounder, ga, s->err);
    }
    int action_size = pddlSqlGrounderPrepActionSize(s->grounder);
    for (int ai = 0; ai < action_size; ++ai)
        findApplicableOpsAction(s, ai, applicable_ops);
}

static void applyAction(pddl_search_lifted_t *s,
                        int args_id,
                        int *cost)
{
    const pddl_ground_action_args_t *aargs;
    aargs = pddlStripsMakerActionArgs(&s->strips, args_id);
    const pddl_prep_action_t *pa;
    pa = pddlSqlGrounderPrepAction(s->grounder, aargs->action_id);

    PDDL_ISET(eadd);
    PDDL_ISET(edel);
    for (int i = 0; i < pa->add_eff.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(pa->add_eff.cond[i], atom);
        ASSERT(!pddlPredIsStatic(&s->pddl->pred.pred[atom->pred]));

        pddl_ground_atom_t *ga;
        ga = pddlStripsMakerAddAtom(&s->strips, atom, aargs->arg, NULL);
        pddlISetAdd(&eadd, ga->id);
    }

    for (int i = 0; i < pa->del_eff.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(pa->del_eff.cond[i], atom);
        ASSERT(!pddlPredIsStatic(&s->pddl->pred.pred[atom->pred]));

        pddl_ground_atom_t *ga;
        ga = pddlStripsMakerAddAtom(&s->strips, atom, aargs->arg, NULL);
        pddlISetAdd(&edel, ga->id);
    }

    *cost = 0;
    for (int i = 0; i < pa->increase.size && s->pddl->metric; ++i){
        const pddl_cond_func_op_t *inc;
        inc = PDDL_COND_CAST(pa->increase.cond[i], func_op);
        if (inc->fvalue != NULL){
            const pddl_ground_atom_t *ga;
            ga = pddlGroundAtomsFindAtom(&s->strips.ground_func,
                                         inc->fvalue, aargs->arg);
            *cost += ga->func_val;
        }else{
            *cost += inc->value;
        }
    }
    if (!s->pddl->metric)
        *cost = 1;

    pddlISetMinus2(&s->next_node.state, &s->cur_node.state, &edel);
    pddlISetUnion(&s->next_node.state, &eadd);
    pddlISetFree(&eadd);
    pddlISetFree(&edel);
}

static void addPlanOp(pddl_search_lifted_t *s, int op_id)
{
    pddl_lifted_plan_t *plan = &s->plan;
    if (plan->plan_alloc == plan->plan_len){
        if (plan->plan_alloc == 0)
            plan->plan_alloc = 2;
        plan->plan_alloc *= 2;
        plan->plan = REALLOC_ARR(plan->plan, char *, plan->plan_alloc);
    }

    const pddl_ground_action_args_t *aargs;
    aargs = pddlStripsMakerActionArgs(&s->strips, op_id);
    const pddl_prep_action_t *pa;
    pa = pddlSqlGrounderPrepAction(s->grounder, aargs->action_id);
    const pddl_action_t *action = pa->action;

    static int maxlen = 512;
    char name[maxlen + 1];
    int len = snprintf(name, maxlen, "%s", action->name);
    for (int i = 0; i < pa->param_size; ++i){
        len += snprintf(name + len, maxlen - len, " %s",
                        s->pddl->obj.obj[aargs->arg[i]].name);
    }
    name[maxlen] = 0;
    plan->plan[plan->plan_len++] = STRDUP(name);
}

static void extractPlan(pddl_search_lifted_t *s,
                        pddl_state_id_t goal_state_id)
{
    pddlStripsStateSpaceGetNoState(&s->state_space, goal_state_id,
                                   &s->cur_node);
    s->plan.plan_cost = s->cur_node.g_value;

    pddl_state_id_t state_id = goal_state_id;
    while (state_id != 0){
        pddlStripsStateSpaceGetNoState(&s->state_space, state_id,
                                       &s->cur_node);
        addPlanOp(s, s->cur_node.op_id);
        state_id = s->cur_node.parent_id;
    }

    for (int i = 0; i < s->plan.plan_len / 2; ++i){
        int j = s->plan.plan_len - 1 - i;
        char *tmp;
        PDDL_SWAP(s->plan.plan[i], s->plan.plan[j], tmp);
    }
}


pddl_search_lifted_t *pddlSearchLiftedAStar(const pddl_t *pddl,
                                            pddl_homomorphism_heur_t *heur,
                                            pddl_err_t *err)
{
    return bfsNew(pddl, heur, 1, 1, 0, "Lifted A*: ", err);
}

pddl_search_lifted_t *pddlSearchLiftedGBFS(const pddl_t *pddl,
                                           pddl_homomorphism_heur_t *heur,
                                           pddl_err_t *err)
{
    return bfsNew(pddl, heur, 0, 1, 0, "Lifted GBFS: ", err);
}

pddl_search_lifted_t *pddlSearchLiftedLazy(const pddl_t *pddl,
                                           pddl_homomorphism_heur_t *heur,
                                           pddl_err_t *err)
{
    return bfsNew(pddl, heur, 0, 1, 1, "Lifted Lazy: ", err);
}

const pddl_lifted_plan_t *pddlSearchLiftedPlan(const pddl_search_lifted_t *s)
{
    return &s->plan;
}

void pddlSearchLiftedPlanPrint(const pddl_search_lifted_t *s, FILE *fout)
{
    const pddl_lifted_plan_t *plan = &s->plan;
    fprintf(fout, ";; Cost: %d\n", plan->plan_cost);
    fprintf(fout, ";; Length: %d\n", plan->plan_len);
    for (int i = 0; i < plan->plan_len; ++i){
        fprintf(fout, "(%s)\n", plan->plan[i]);
    }
}
