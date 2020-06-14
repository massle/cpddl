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

#include <boruvka/alloc.h>
#include <boruvka/iarr.h>
#include "pddl/dtg.h"

void pddlUnreachableInMGroupDTG(int init_fact,
                                const pddl_mgroup_t *mgroup,
                                const pddl_strips_ops_t *ops,
                                const pddl_strips_fact_cross_ref_t *cref,
                                bor_iset_t *unreachable_facts,
                                bor_iset_t *unreachable_ops)
{
    if (ops->op_size == 0
            || borISetSize(&mgroup->mgroup) == 0
            || !borISetIn(init_fact, &mgroup->mgroup)){
        return;
    }

    int size = borISetSize(&mgroup->mgroup);
    int max_fact = borISetGet(&mgroup->mgroup, size - 1);
    bor_iset_t *reaches = BOR_CALLOC_ARR(bor_iset_t, size);

    int *fact_to_id = BOR_CALLOC_ARR(int, max_fact + 1);
    for (int mi = 0; mi < size; ++mi){
        int fact = borISetGet(&mgroup->mgroup, mi);
        fact_to_id[fact] = mi;
    }

    BOR_ISET(pre);
    for (int mi = 0; mi < size; ++mi){
        int to = borISetGet(&mgroup->mgroup, mi);
        int op_id;
        BOR_ISET_FOR_EACH(&cref->fact[to].op_add, op_id){
            const pddl_strips_op_t *op = ops->op[op_id];
            borISetIntersect2(&pre, &op->pre, &mgroup->mgroup);
            if (borISetSize(&pre) == 0){
                for (int from = 0; from < size; ++from)
                    borISetAdd(reaches + from, mi);

            }else if (borISetSize(&pre) > 1){
                borISetAdd(unreachable_ops, op_id);

            }else if (borISetGet(&pre, 0) != to){ // borISetSize(&pre) == 1
                int from = borISetGet(&pre, 0);
                borISetAdd(reaches + fact_to_id[from], mi);
            }
        }
    }
    borISetFree(&pre);

    BOR_IARR(queue);
    int *reached = BOR_CALLOC_ARR(int, size);
    borIArrAdd(&queue, fact_to_id[init_fact]);
    reached[fact_to_id[init_fact]] = 1;
    while (borIArrSize(&queue) > 0){
        int fid = queue.arr[--queue.size];
        int to;
        BOR_ISET_FOR_EACH(reaches + fid, to){
            if (!reached[to]){
                borIArrAdd(&queue, to);
                reached[to] = 1;
            }
        }
    }

    for (int mi = 0; mi < size; ++mi){
        if (!reached[mi]){
            int fact = borISetGet(&mgroup->mgroup, mi);
            borISetAdd(unreachable_facts, fact);
            // Unreachable operators are those that have unreachable facts
            // in their preconditions
            borISetUnion(unreachable_ops, &cref->fact[fact].op_pre);
            borISetUnion(unreachable_ops, &cref->fact[fact].op_add);
        }
    }

    BOR_FREE(reached);
    borIArrFree(&queue);
    for (int mi = 0; mi < size; ++mi)
        borISetFree(reaches + mi);
    BOR_FREE(fact_to_id);
    BOR_FREE(reaches);
}

void pddlUnreachableInMGroupsDTGs(const pddl_strips_t *strips,
                                  const pddl_mgroups_t *mgroups,
                                  bor_iset_t *unreachable_facts,
                                  bor_iset_t *unreachable_ops,
                                  bor_err_t *err)
{
    if (mgroups->mgroup_size == 0)
        return;

    BOR_INFO2(err, "Pruning using mutex group DTGs...");
    pddl_strips_fact_cross_ref_t cref;
    pddlStripsFactCrossRefInit(&cref, strips, 0, 0, 1, 1, 0);

    BOR_ISET(init);
    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mgroups->mgroup + mgi;
        borISetIntersect2(&init, &strips->init, &mg->mgroup);
        if (borISetSize(&init) != 1)
            continue;

        int init_fact = borISetGet(&init, 0);
        pddlUnreachableInMGroupDTG(init_fact, mg, &strips->op, &cref,
                                   unreachable_facts, unreachable_ops);
    }
    borISetFree(&init);

    pddlStripsFactCrossRefFree(&cref);
    BOR_INFO(err, "Pruning using mutex group DTGs DONE."
                  " unreachable facts: %d, unreachable ops: %d",
                  borISetSize(unreachable_facts),
                  borISetSize(unreachable_ops));
}
