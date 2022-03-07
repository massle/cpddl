/***
 * cpddl
 * -------
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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

#include <boruvka/rbtree.h>
#include "pddl/disambiguation.h"
#include "pddl/symbolic_split_goal.h"
#include "assert.h"

#define EPS 1E-5

struct states {
    double key;
    pddl_bdd_t *states;
    bor_rbtree_node_t rbtree;
};
typedef struct states states_t;

static states_t *statesNew(double key, pddl_bdd_t *bdd)
{
    states_t *s = BOR_ALLOC(states_t);
    s->key = key;
    s->states = bdd;
    return s;
}

static states_t *statesClone(states_t *s, pddl_bdd_manager_t *mgr)
{
    states_t *new = BOR_ALLOC(states_t);
    new->key = s->key;
    new->states = pddlBDDClone(mgr, s->states);
    return new;
}

static void statesDel(states_t *states, pddl_bdd_manager_t *mgr)
{
    pddlBDDDel(mgr, states->states);
    BOR_FREE(states);
}


static int statesRBTreeCmp(const bor_rbtree_node_t *n1,
                           const bor_rbtree_node_t *n2,
                           void *data)
{
    const states_t *s1 = bor_container_of(n1, states_t, rbtree);
    const states_t *s2 = bor_container_of(n2, states_t, rbtree);
    if (fabs(s1->key - s2->key) < EPS)
        return 0;
    if (s1->key < s2->key)
        return -1;
    return 1;
}

static bor_rbtree_t *statesMapNew(void)
{
    bor_rbtree_t *map = borRBTreeNew(statesRBTreeCmp, NULL);
    return map;
}

static void statesMapDel(bor_rbtree_t *map,
                         pddl_bdd_manager_t *mgr)
{
    bor_rbtree_node_t *n;
    while ((n = borRBTreeExtractMin(map)) != NULL){
        states_t *s = bor_container_of(n, states_t, rbtree);
        statesDel(s, mgr);
    }
    borRBTreeDel(map);
}

static int statesMapInsert(bor_rbtree_t *map,
                           pddl_bdd_manager_t *mgr,
                           states_t *s)
{
    bor_rbtree_node_t *find;
    if ((find = borRBTreeFind(map, &s->rbtree)) != NULL){
        states_t *sfound = bor_container_of(find, states_t, rbtree);
        int ret = pddlBDDOrUpdate(mgr, &sfound->states, s->states);
        ASSERT_RUNTIME(ret == 0);
        return 0;

    }else{
        states_t *el = statesClone(s, mgr);
        bor_rbtree_node_t *n = borRBTreeInsert(map, &el->rbtree);
        ASSERT_RUNTIME(n == NULL);
        return 1;
    }
}

static bor_rbtree_t *statesMapNewMGroup(const bor_iset_t *mg,
                                        pddl_symbolic_vars_t *symb_vars,
                                        pddl_bdd_manager_t *mgr,
                                        const double *pot)
{
    bor_rbtree_t *map = statesMapNew();
    int fact;
    BOR_ISET_FOR_EACH(mg, fact){
        double key = pot[fact];
        BOR_ISET(ps);
        borISetAdd(&ps, fact);
        pddl_bdd_t *bdd = pddlSymbolicVarsCreatePartialState(symb_vars, &ps);
        borISetFree(&ps);

        states_t *s = statesNew(key, bdd);
        if (statesMapInsert(map, mgr, s) == 0)
            statesDel(s, mgr);
    }
    return map;
}

static bor_rbtree_t *statesMapMerge(bor_rbtree_t *map1,
                                    bor_rbtree_t *map2,
                                    pddl_bdd_manager_t *mgr)
{
    bor_rbtree_t *map = statesMapNew();
    bor_rbtree_node_t *n1;
    BOR_RBTREE_FOR_EACH(map1, n1){
        states_t *s1 = bor_container_of(n1, states_t, rbtree);
        bor_rbtree_node_t *n2;
        BOR_RBTREE_FOR_EACH(map2, n2){
            states_t *s2 = bor_container_of(n2, states_t, rbtree);
            double key = s1->key + s2->key;
            states_t *s;
            s = statesNew(key, pddlBDDAnd(mgr, s1->states, s2->states));
            if (statesMapInsert(map, mgr, s) == 0)
                statesDel(s, mgr);
        }
    }
    return map;
}

void pddlSymbolicSplitGoalByPot(const bor_iset_t *goal,
                                const pddl_mgroups_t *mgroups,
                                const pddl_mutex_pairs_t *mutex,
                                const double *pot,
                                pddl_symbolic_vars_t *symb_vars,
                                pddl_bdd_manager_t *mgr,
                                pddl_bdds_t *bdds,
                                bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "SplitGoalByPot: ");
    pddl_disambiguate_t disamb;
    if (pddlDisambiguateInit(&disamb, symb_vars->fact_size,
                             mutex, mgroups) != 0){
        BOR_FATAL2("Disambiguation failed because there are"
                   " no exactly-1 mutex groups");
    }
    BOR_INFO2(err, "Disambiguation created.");

    pddl_mgroups_t mgs;
    pddlMGroupsInitEmpty(&mgs);
    for (int i = 0; i < mgroups->mgroup_size; ++i){
        const pddl_mgroup_t *mgin = mgroups->mgroup + i;
        if (!mgin->is_exactly_one)
            continue;

        BOR_ISET(mg_fact);
        if (!borISetIsDisjoint(goal, &mgin->mgroup)){
            borISetIntersect2(&mg_fact, goal, &mgin->mgroup);
            ASSERT(borISetSize(&mg_fact) == 1);
            pddlMGroupsAdd(&mgs, &mg_fact);

        }else{
            int dret = pddlDisambiguate(&disamb, goal, &mgin->mgroup,
                                        1, 0, NULL, &mg_fact);
            if (dret < 0){
                pddlMGroupsFree(&mgs);
                pddlDisambiguateFree(&disamb);
                ASSERT_RUNTIME(0);
                // TODO: Unsolvable task
            }else if (dret == 0){
                pddlMGroupsAdd(&mgs, &mgin->mgroup);
            }else{
                borISetIntersect(&mg_fact, &mgin->mgroup);
                pddlMGroupsAdd(&mgs, &mg_fact);
            }
        }
        borISetFree(&mg_fact);
    }

    int maps_alloc = 2;
    int maps_size = 0;
    bor_rbtree_t **maps = BOR_ALLOC_ARR(bor_rbtree_t *, maps_alloc);
    for (int i = 0; i < mgs.mgroup_size; ++i){
        bor_rbtree_t *map = statesMapNewMGroup(&mgs.mgroup[i].mgroup,
                                               symb_vars, mgr, pot);
        if (maps_size == maps_alloc){
            maps_alloc *= 2;
            maps = BOR_REALLOC_ARR(maps, bor_rbtree_t *, maps_alloc);
        }
        maps[maps_size++] = map;
    }

    for (int mi = 0; mi < maps_size - 1; mi = mi + 2){
        bor_rbtree_t *map = statesMapMerge(maps[mi], maps[mi + 1], mgr);
        if (maps_size == maps_alloc){
            maps_alloc *= 2;
            maps = BOR_REALLOC_ARR(maps, bor_rbtree_t *, maps_alloc);
        }
        maps[maps_size++] = map;
        statesMapDel(maps[mi], mgr);
        statesMapDel(maps[mi + 1], mgr);
    }

    bor_rbtree_t *map = maps[maps_size - 1];
    bor_rbtree_t *rounded_map = statesMapNew();
    bor_rbtree_node_t *node;
    BOR_RBTREE_FOR_EACH(map, node){
        states_t *states = bor_container_of(node, states_t, rbtree);
        double key = ceil(states->key - EPS);
        if (key > 0)
            continue;
        states_t *rounded = statesNew(key, pddlBDDClone(mgr, states->states));
        if (statesMapInsert(rounded_map, mgr, rounded) == 0)
            statesDel(rounded, mgr);
    }

    BOR_RBTREE_FOR_EACH(rounded_map, node){
        states_t *states = bor_container_of(node, states_t, rbtree);
        BOR_INFO(err, "Found a set of goal states."
                      " h-value: %.2f, bdd-size: %d, num-states: %.2f",
                 states->key,
                 pddlBDDSize(states->states),
                 pddlBDDCountMinterm(mgr, states->states,
                                     symb_vars->bdd_var_size / 2));
        // TODO: In reality we also need to store the heuristic value so
        //       pddl_bdds_t won't do...
        pddlBDDsAdd(mgr, bdds, states->states);
    }
    statesMapDel(rounded_map, mgr);
    statesMapDel(map, mgr);

#ifdef PDDL_DEBUG
    fprintf(stderr, "TEST\n");
    for (int i = 0; i < bdds->bdd_size; ++i){
        for (int j = i + 1; j < bdds->bdd_size; ++j){
            pddl_bdd_t *b = pddlBDDAnd(mgr, bdds->bdd[i], bdds->bdd[j]);
            ASSERT(pddlBDDIsFalse(mgr, b));
        }
    }
#endif

    pddlMGroupsFree(&mgs);
    pddlDisambiguateFree(&disamb);
    BOR_INFO_PREFIX_POP(err);
}

