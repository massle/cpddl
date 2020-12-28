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

static void prepareMutex(pddl_mutex_pairs_t *mutex,
                         const pddl_mutex_pairs_t *mutex_in,
                         const pddl_mgroups_t *mgroups_in,
                         bor_err_t *err)
{
    pddlMutexPairsInitCopy(mutex, mutex_in);
    for (int mgi = 0; mgi < mgroups_in->mgroup_size; ++mgi)
        pddlMutexPairsAddMGroup(mutex, &mgroups_in->mgroup[mgi]);
}

static void prepareStrips(pddl_strips_t *strips,
                          const pddl_strips_t *strips_in,
                          const pddl_mutex_pairs_t *mutex,
                          bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Clean Strips: ");
    pddlStripsInitCopy(strips, strips_in);
    BOR_ISET(unreachable_ops);
    pddlStripsFindUnreachableOps(strips, mutex, &unreachable_ops, err);
    pddlStripsReduce(strips, NULL, &unreachable_ops);
    pddlStripsRemoveUselessDelEffs(strips, mutex, NULL, err);
    borISetFree(&unreachable_ops);
    BOR_INFO_PREFIX_POP(err);
}

static void prepareMGroups(pddl_mgroups_t *mgroups,
                           const pddl_black_mgroups_t *black_mgroups,
                           const pddl_mgroups_t *mgroups_in,
                           bor_err_t *err)
{
    BOR_ISET(black_facts);
    pddlMGroupsInitEmpty(mgroups);
    // Put black mgroups first
    for (int mgi = 0; mgi < black_mgroups->mgroup_size; ++mgi){
        const pddl_black_mgroup_t *bmg = black_mgroups->mgroup + mgi;
        borISetUnion(&black_facts, &bmg->mgroup);
        pddlMGroupsAdd(mgroups, &bmg->mgroup);
    }
    // Next, copy the input mgroups without black facts
    for (int mgi = 0; mgi < mgroups_in->mgroup_size; ++mgi){
        const pddl_mgroup_t *mgin = mgroups_in->mgroup + mgi;
        BOR_ISET(m);
        borISetMinus2(&m, &mgin->mgroup, &black_facts);
        if (borISetSize(&m) > 0)
            pddlMGroupsAdd(mgroups, &m);
        borISetFree(&m);
    }
    //pddlMGroupsPrintTable(NULL, &strips, &mgroups, NULL, err);
    borISetFree(&black_facts);
    BOR_INFO(err, "Created %d mutex groups of which %d are black",
             mgroups->mgroup_size, black_mgroups->mgroup_size);
}

static void setBlackVars(pddl_fdr_t *fdr,
                         const pddl_black_mgroups_t *black_mgroups,
                         int *none_of_those,
                         bor_err_t *err)
{
    int num_none_of_those = 0;
    for (int mgi = 0; mgi < black_mgroups->mgroup_size; ++mgi){
        none_of_those[mgi] = -1;
        const pddl_black_mgroup_t *bmg = black_mgroups->mgroup + mgi;
        int first_fact = borISetGet(&bmg->mgroup, 0);
        int val_id = borISetGet(&fdr->var.strips_id_to_val[first_fact], 0);
        int var_id = fdr->var.global_id_to_val[val_id]->var_id;
        if (fdr->var.var[var_id].val_none_of_those >= 0){
            none_of_those[mgi] = var_id;
            ++num_none_of_those;
        }
        fdr->var.var[var_id].is_black = 1;
    }
    BOR_INFO(err, "Black variables with none-of-those: %d", num_none_of_those);
}

static void setNoneOfThoseInPre(pddl_fdr_t *fdr,
                                const pddl_strips_t *strips,
                                const pddl_black_mgroups_t *black_mgroups,
                                const int *none_of_those,
                                bor_err_t *err)
{
    int num_set = 0;
    pddl_strips_fact_cross_ref_t cref;
    pddlStripsFactCrossRefInit(&cref, strips, 0, 0, 1, 0, 0);
    for (int mgi = 0; mgi < black_mgroups->mgroup_size; ++mgi){
        int set_var = none_of_those[mgi];
        if (set_var < 0)
            continue;

        int set_val = fdr->var.var[set_var].val_none_of_those;
        ASSERT(set_val >= 0);
        const pddl_black_mgroup_t *bmg = black_mgroups->mgroup + mgi;
        int mutex_fact;
        BOR_ISET_FOR_EACH(&bmg->mutex_facts, mutex_fact){
            int opi;
            BOR_ISET_FOR_EACH(&cref.fact[mutex_fact].op_pre, opi){
                pddl_fdr_op_t *op = fdr->op.op[opi];
                ASSERT(!pddlFDRPartStateIsSet(&op->pre, set_var)
                        || pddlFDRPartStateGet(&op->pre, set_var) == set_val);
                if (!pddlFDRPartStateIsSet(&op->pre, set_var)){
                    pddlFDRPartStateSet(&op->pre, set_var, set_val);
                    ++num_set;
                }
            }
        }
    }
    BOR_INFO(err, "Set %d additional none-of-those preconditions", num_set);
    pddlStripsFactCrossRefFree(&cref);
}

static void fdrStat(const pddl_fdr_t *fdr, bor_err_t *err)
{
    int num_black_vars = 0;
    int num_black_facts = 0;
    int num_black_facts_with_none_of_those = 0;
    for (int vari = 0; vari < fdr->var.var_size; ++vari){
        const pddl_fdr_var_t *var = fdr->var.var + vari;
        if (!var->is_black)
            continue;
        ++num_black_vars;
        num_black_facts += var->val_size;
        if (var->val_none_of_those >= 0)
            --num_black_facts;
        num_black_facts_with_none_of_those += var->val_size;
    }
    BOR_INFO(err, "Num black variables: %d", num_black_vars);
    BOR_INFO(err, "Num black STRIPS facts: %d", num_black_facts);
    BOR_INFO(err, "Num black FDR facts: %d",
             num_black_facts_with_none_of_those);
}

static int constructFDR(pddl_fdr_t *fdr,
                        const pddl_strips_t *strips,
                        const pddl_mgroups_t *mgroups_in,
                        const pddl_mutex_pairs_t *mutex,
                        const pddl_black_mgroups_t *black_mgroups,
                        bor_err_t *err)
{
    // Re-create the set of mutex groups
    pddl_mgroups_t mgroups;
    prepareMGroups(&mgroups, black_mgroups, mgroups_in, err);

    // Construct FDR
    unsigned fdr_var_flags = PDDL_FDR_VARS_LARGEST_FIRST;
    unsigned fdr_flags = 0;
    int ret = pddlFDRInitFromStrips(fdr, strips, &mgroups, mutex,
                                    fdr_var_flags, fdr_flags, err);
    ASSERT_RUNTIME(fdr->op.op_size == strips->op.op_size);

    // Find black variables and remember which of them has none-of-those value
    int *none_of_those = BOR_CALLOC_ARR(int, black_mgroups->mgroup_size);
    setBlackVars(fdr, black_mgroups, none_of_those, err);

    // Set none-of-those in preconditions of operators
    setNoneOfThoseInPre(fdr, strips, black_mgroups, none_of_those, err);

    BOR_FREE(none_of_those);
    pddlMGroupsFree(&mgroups);

    fdrStat(fdr, err);

    return ret;
}

int pddlBlackFDRInitFromStrips(pddl_fdr_t *fdr,
                               const pddl_strips_t *strips_in,
                               const pddl_mgroups_t *mgroups_in,
                               const pddl_mutex_pairs_t *mutex_in,
                               const pddl_black_mgroups_config_t *black_cfg,
                               bor_err_t *err)
{
    bor_timer_t timer;
    borTimerStart(&timer);
    BOR_INFO_PREFIX_PUSH(err, "Black-FDR: ");
    BOR_INFO2(err, "Construction of FDR with black variables...");

    // Make sure that mutex groups are contained in the mutex pairs
    pddl_mutex_pairs_t mutex;
    prepareMutex(&mutex, mutex_in, mgroups_in, err);

    // Cleanup strips planning task
    pddl_strips_t strips;
    prepareStrips(&strips, strips_in, &mutex, err);

    // Find black mutex groups
    pddl_black_mgroups_t black_mgroups[black_cfg->num_solutions];
    pddlBlackMGroupsInfer(black_mgroups, &strips, mgroups_in, black_cfg, err);

    // Construct FDR
    int num_created = 0;
    for (int i = 0; i < black_cfg->num_solutions; ++i){
        if (black_mgroups[i].mgroup_size == 0 && i > 0)
            break;

        if (constructFDR(fdr + i, &strips, mgroups_in, &mutex,
                         black_mgroups + i, err) != 0){
            for (int i = 0; i < black_cfg->num_solutions; ++i)
                pddlBlackMGroupsFree(black_mgroups + i);
            pddlStripsFree(&strips);
            pddlMutexPairsFree(&mutex);
            BOR_INFO_PREFIX_POP(err);
            BOR_TRACE_RET(err, -1);
        }
        num_created += 1;
    }

    for (int i = 0; i < black_cfg->num_solutions; ++i)
        pddlBlackMGroupsFree(black_mgroups + i);
    pddlStripsFree(&strips);
    pddlMutexPairsFree(&mutex);

    borTimerStop(&timer);
    BOR_INFO(err, "Translation took %.2f seconds",
             borTimerElapsedInSF(&timer));
    BOR_INFO_PREFIX_POP(err);
    return num_created;
}
