/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
 * FAI Group at Saarland University, and
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

#include <boruvka/iarr.h>
#include "pddl/mgroup_projection.h"
#include "assert.h"

void pddlMGroupProjectionInit(pddl_mgroup_projection_t *p,
                              const pddl_strips_t *strips,
                              const bor_iset_t *mgroup,
                              const pddl_mutex_pairs_t *mutex,
                              const pddl_strips_fact_cross_ref_t *cref)
{
    bzero(p, sizeof(*p));
    p->num_states = borISetSize(mgroup) + 1;
    borISetUnion(&p->mgroup, mgroup);
    p->tr = BOR_CALLOC_ARR(bor_iset_t, p->num_states * p->num_states);

    BOR_ISET(ops_add);
    BOR_ISET(ops_del);
    for (int to = 0; to < p->mgroup.size; ++to){
        int fact_to = borISetGet(&p->mgroup, to);
        borISetEmpty(&ops_add);
        borISetUnion(&ops_add, &cref->fact[fact_to].op_add);

        // Keeps track of operators that delete fact_to but do not add any
        // other fact from this mutex group
        borISetEmpty(&ops_del);
        borISetUnion(&ops_del, &cref->fact[fact_to].op_del);

        // Transitions from -> to, where from is in the precondition and to
        // is in the add effect
        for (int from = 0; from < p->mgroup.size; ++from){
            if (from == to)
                continue;
            int fact_from = borISetGet(&p->mgroup, from);
            bor_iset_t *tr = p->tr + from * p->num_states + to;
            borISetIntersect2(tr, &cref->fact[fact_from].op_pre, &ops_add);
            borISetMinus(&ops_add, &cref->fact[fact_from].op_pre);

            borISetMinus(&ops_del, &cref->fact[fact_from].op_add);
        }

        // Now ops_add contains operators whose precondition has empty
        // intersection with mgroup, but adding fact_to.

        // Transitions \emptyset -> to
        borISetUnion(p->tr + (p->num_states - 1) * p->num_states + to, &ops_add);

        // Transitions from -> to if from is not in the precondition and it
        // is also not a mutex with the precondition
        int op_id;
        BOR_ISET_FOR_EACH(&ops_add, op_id){
            const pddl_strips_op_t *op = strips->op.op[op_id];
            for (int from = 0; from < p->mgroup.size; ++from){
                if (from == to)
                    continue;
                int fact_from = borISetGet(&p->mgroup, from);
                if (!pddlMutexPairsIsMutexFactSet(mutex, fact_from, &op->pre))
                    borISetAdd(p->tr + from * p->num_states + to, op_id);
            }
        }

        // Transitions to -> \emptyset
        borISetUnion(p->tr + to * p->num_states + (p->num_states - 1), &ops_del);
    }
    borISetFree(&ops_add);
    borISetFree(&ops_del);

    pddlMGroupProjectionPruneUnreachableFromInit(p, strips);
    pddlMGroupProjectionPruneUnreachableFromGoal(p, strips, mutex);
}

void pddlMGroupProjectionInitCopy(pddl_mgroup_projection_t *p,
                                  const pddl_mgroup_projection_t *src)
{
    bzero(p, sizeof(*p));
    p->num_states = src->num_states;
    borISetUnion(&p->mgroup, &src->mgroup);
    p->tr = BOR_CALLOC_ARR(bor_iset_t, p->num_states * p->num_states);
    for (int i = 0; i < p->num_states; ++i){
        for (int j = 0; j < p->num_states; ++j){
            borISetUnion(p->tr + i * p->num_states + j,
                         src->tr + i * p->num_states + j);
        }
    }
}

void pddlMGroupProjectionFree(pddl_mgroup_projection_t *p)
{
    borISetFree(&p->mgroup);
    for (int i = 0; i < p->num_states; ++i){
        for (int j = 0; j < p->num_states; ++j){
            borISetFree(p->tr + i * p->num_states + j);
        }
    }
    if (p->tr != NULL)
        BOR_FREE(p->tr);
}

static int outdegree(const pddl_mgroup_projection_t *p, int state)
{
    int outdegree = 0;
    for (int i = 0; i < p->num_states; ++i){
        if (i != state && borISetSize(p->tr + state * p->num_states + i) > 0)
            outdegree += borISetSize(p->tr + state * p->num_states + i);
            //outdegree += 1;
    }
    return outdegree;
}

int pddlMGroupProjectionMaxOutdegree(const pddl_mgroup_projection_t *p)
{
    int max = -1;
    for (int state = 0; state < p->num_states; ++state){
        int deg = outdegree(p, state);
        max = BOR_MAX(max, deg);
    }
    return max;
}

void pddlMGroupProjectionPruneUnreachable(pddl_mgroup_projection_t *p,
                                          const bor_iset_t *states,
                                          int backward)
{
    int *visited = BOR_CALLOC_ARR(int, p->num_states);
    BOR_IARR(queue);
    int state;
    BOR_ISET_FOR_EACH(states, state){
        borIArrAdd(&queue, state);
        visited[state] = 1;
    }
    for (int cur = 0; cur < borIArrSize(&queue); ++cur){
        int state = borIArrGet(&queue, cur);
        for (int to = 0; to < p->num_states; ++to){
            if (visited[to])
                continue;
            const bor_iset_t *tr;
            if (backward){
                tr = p->tr + to * p->num_states + state;
            }else{
                tr = p->tr + state * p->num_states + to;
            }
            if (borISetSize(tr) > 0){
                borIArrAdd(&queue, to);
                visited[to] = 1;
            }
        }
    }
    borIArrFree(&queue);

    for (int state = 0; state < p->num_states; ++state){
        if (visited[state])
            continue;
        for (int to = 0; to < p->num_states; ++to){
            borISetEmpty(p->tr + state * p->num_states + to);
            borISetEmpty(p->tr + to * p->num_states + state);
        }
    }
    BOR_FREE(visited);
}

void pddlMGroupProjectionPruneUnreachableFromInit(pddl_mgroup_projection_t *p,
                                                  const pddl_strips_t *strips)
{
    BOR_ISET(from_state);
    BOR_ISET(init);
    borISetIntersect2(&init, &p->mgroup, &strips->init);

    if (borISetSize(&init) > 1)
        BOR_FATAL2("The set of facts is not a mutex group!");

    if (borISetSize(&init) == 0){
        borISetAdd(&from_state, p->num_states - 1);
    }else{
        int init_fact = borISetGet(&init, 0);
        for (int i = 0; i < borISetSize(&p->mgroup); ++i){
            if (borISetGet(&p->mgroup, i) == init_fact){
                borISetAdd(&from_state, i);
                break;
            }
        }
    }
    ASSERT_RUNTIME(borISetSize(&from_state) == 1);
    pddlMGroupProjectionPruneUnreachable(p, &from_state, 0);
    borISetFree(&from_state);
    borISetFree(&init);
}

void pddlMGroupProjectionPruneUnreachableFromGoal(pddl_mgroup_projection_t *p,
                                                  const pddl_strips_t *strips,
                                                  const pddl_mutex_pairs_t *mx)
{
    BOR_ISET(from_state);
    BOR_ISET(goal);
    borISetIntersect2(&goal, &p->mgroup, &strips->goal);
    if (borISetSize(&goal) > 0){
        for (int i = 0; i < borISetSize(&p->mgroup); ++i){
            if (borISetIn(borISetGet(&p->mgroup, i), &goal))
                borISetAdd(&from_state, i);
        }

    }else{
        borISetAdd(&from_state, p->num_states - 1);
        for (int i = 0; i < borISetSize(&p->mgroup); ++i){
            int fact = borISetGet(&p->mgroup, i);
            if (!pddlMutexPairsIsMutexFactSet(mx, fact, &strips->goal))
                borISetAdd(&from_state, i);
        }
    }

    ASSERT_RUNTIME(borISetSize(&from_state) > 0);
    pddlMGroupProjectionPruneUnreachable(p, &from_state, 1);
    borISetFree(&from_state);
    borISetFree(&goal);
}

void pddlMGroupProjectionRestrictOps(pddl_mgroup_projection_t *p,
                                     const bor_iset_t *ops)
{
    for (int i = 0; i < p->num_states; ++i){
        for (int j = 0; j < p->num_states; ++j){
            borISetIntersect(p->tr + i * p->num_states + j, ops);
        }
    }
}

void pddlMGroupProjectionPrint(const pddl_mgroup_projection_t *p,
                               const pddl_strips_t *strips,
                               FILE *fout)
{
    fprintf(fout, "Proj (%d)\n", p->num_states);
    for (int i = 0; i < p->num_states; ++i){
        for (int j = 0; j < p->num_states; ++j){
            if (borISetSize(p->tr + i * p->num_states + j) == 0)
                continue;
            int fact_from = -1;
            if (i < p->num_states - 1)
                fact_from = borISetGet(&p->mgroup, i);
            int fact_to = -1;
            if (j < p->num_states - 1)
                fact_to = borISetGet(&p->mgroup, j);

            fprintf(fout, "%d[%d:(%s)] -> %d[%d:(%s)] (%d)\n",
                    i, fact_from,
                    (fact_from >= 0 ?  strips->fact.fact[fact_from]->name : ""),
                    j, fact_to,
                    (fact_to >= 0 ?  strips->fact.fact[fact_to]->name : ""),
                    borISetSize(p->tr + i * p->num_states + j));
            int op_id;
            BOR_ISET_FOR_EACH(p->tr + i * p->num_states + j, op_id){
                const pddl_strips_op_t *op = strips->op.op[op_id];
                fprintf(fout, "  %d:", op_id);
                pddlStripsOpPrintDebug(op, &strips->fact, fout);
            }
        }
    }
}
