/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/strips_fact_cross_ref.h"
#include "pddl/black_fdr.h"
#include "assert.h"

int pddlBlackFDRInitFromStrips(pddl_fdr_t *fdr,
                               const pddl_strips_t *strips_in,
                               const pddl_mgroups_t *mgroups_in,
                               const pddl_mutex_pairs_t *mutex_in,
                               const pddl_black_mgroups_config_t *black_cfg,
                               bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Black-FDR: ");
    BOR_INFO2(err, "Construction of FDR with black variables...");

    // Make sure that mutex groups are contained in the mutex pairs
    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitCopy(&mutex, mutex_in);
    for (int mgi = 0; mgi < mgroups_in->mgroup_size; ++mgi)
        pddlMutexPairsAddMGroup(&mutex, &mgroups_in->mgroup[mgi]);

    // Cleanup strips planning task
    BOR_INFO_PREFIX_PUSH(err, "Clean Strips: ");
    pddl_strips_t strips;
    pddlStripsInitCopy(&strips, strips_in);
    BOR_ISET(unreachable_ops);
    pddlStripsFindUnreachableOps(&strips, &mutex, &unreachable_ops, err);
    pddlStripsReduce(&strips, NULL, &unreachable_ops);
    pddlStripsRemoveUselessDelEffs(&strips, &mutex, NULL, err);
    borISetFree(&unreachable_ops);
    BOR_INFO_PREFIX_POP(err);
    //pddlMGroupsLogTable(NULL, &strips, mgroups_in, err);

    pddl_black_mgroups_t black_mgroups;
    pddlBlackMGroups(&black_mgroups, &strips, mgroups_in, black_cfg, err);

    // Re-create the set of mutex groups
    BOR_ISET(black_facts);
    pddl_mgroups_t mgroups;
    pddlMGroupsInitEmpty(&mgroups);
    // Put black mgroups first
    for (int mgi = 0; mgi < black_mgroups.mgroup_size; ++mgi){
        const pddl_black_mgroup_t *bmg = black_mgroups.mgroup + mgi;
        borISetUnion(&black_facts, &bmg->mgroup);
        pddlMGroupsAdd(&mgroups, &bmg->mgroup);
    }
    // Next, copy the input mgroups without black facts
    for (int mgi = 0; mgi < mgroups_in->mgroup_size; ++mgi){
        const pddl_mgroup_t *mgin = mgroups_in->mgroup + mgi;
        BOR_ISET(m);
        borISetMinus2(&m, &mgin->mgroup, &black_facts);
        if (borISetSize(&m) > 0)
            pddlMGroupsAdd(&mgroups, &m);
        borISetFree(&m);
    }
    //pddlMGroupsLogTable(NULL, &strips, &mgroups, err);


    // Construct FDR
    unsigned fdr_var_flags = PDDL_FDR_VARS_LARGEST_FIRST;
    unsigned fdr_flags = 0;
    int ret = pddlFDRInitFromStrips(fdr, &strips, &mgroups, &mutex,
                                    fdr_var_flags, fdr_flags, err);
    ASSERT_RUNTIME(fdr->op.op_size == strips.op.op_size);

    // Set none-of-those in preconditions
    int *none_of_those = BOR_CALLOC_ARR(int, black_mgroups.mgroup_size);
    for (int mgi = 0; mgi < black_mgroups.mgroup_size; ++mgi){
        none_of_those[mgi] = -1;
        const pddl_black_mgroup_t *bmg = black_mgroups.mgroup + mgi;
        int first_fact = borISetGet(&bmg->mgroup, 0);
        int val_id = borISetGet(&fdr->var.strips_id_to_val[first_fact], 0);
        int var_id = fdr->var.global_id_to_val[val_id]->var_id;
        if (fdr->var.var[var_id].val_none_of_those >= 0)
            none_of_those[mgi] = var_id;
    }

    // TODO: refactor
    int num_set = 0;
    pddl_strips_fact_cross_ref_t cref;
    pddlStripsFactCrossRefInit(&cref, &strips, 0, 0, 1, 0, 0);
    for (int mgi = 0; mgi < black_mgroups.mgroup_size; ++mgi){
        int set_var = none_of_those[mgi];
        if (set_var < 0)
            continue;

        int set_val = fdr->var.var[set_var].val_none_of_those;
        const pddl_black_mgroup_t *bmg = black_mgroups.mgroup + mgi;
        for (int fami = 0; fami < bmg->fam_groups.mgroup_size; ++fami){
            const bor_iset_t *famg = &bmg->fam_groups.mgroup[fami].mgroup;
            BOR_ISET(pre_facts);
            borISetMinus2(&pre_facts, famg, &bmg->mgroup);
            int fact;
            BOR_ISET_FOR_EACH(&pre_facts, fact){
                int opi;
                BOR_ISET_FOR_EACH(&cref.fact[fact].op_pre, opi){
                    pddl_fdr_op_t *op = fdr->op.op[opi];
                    ASSERT(!pddlFDRPartStateIsSet(&op->pre, set_var)
                          || pddlFDRPartStateGet(&op->pre, set_var) == set_val);
                    if (!pddlFDRPartStateIsSet(&op->pre, set_var)){
                        pddlFDRPartStateSet(&op->pre, set_var, set_val);
                        ++num_set;
                    }
                }
            }
            borISetFree(&pre_facts);
        }
    }
    BOR_INFO(err, "Set %d additional none-of-those preconditions", num_set);
    pddlStripsFactCrossRefFree(&cref);
    BOR_FREE(none_of_those);


    pddlMGroupsFree(&mgroups);
    pddlBlackMGroupsFree(&black_mgroups);
    borISetFree(&black_facts);
    pddlStripsFree(&strips);
    pddlMutexPairsFree(&mutex);
    BOR_INFO_PREFIX_POP(err);
    return ret;
}
