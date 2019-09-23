/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
 * AI Center, Department of Computer Science,
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

#include "pddl/mg_strips.h"
#include "assert.h"

static void mgroupsFacts(bor_iset_t *facts,
                         bor_iset_t *not_facts,
                         const pddl_mgroups_t *mgroups,
                         int fact_size)
{
    borISetEmpty(facts);
    borISetEmpty(not_facts);

    for (int mi = 0; mi < mgroups->mgroup_size; ++mi)
        borISetUnion(facts, &mgroups->mgroup[mi].mgroup);
    int cur = 0;
    for (int fact_id = 0; fact_id < fact_size; ++fact_id){
        if (cur < borISetSize(facts) && borISetGet(facts, cur) == fact_id){
            ++cur;
        }else{
            borISetAdd(not_facts, fact_id);
        }
    }
}

static void makeMGroupExactlyOne(pddl_mg_strips_t *mg_strips,
                                 const bor_iset_t *uncovered_del_effs,
                                 const pddl_mgroup_t *mg_in)
{
    if (borISetIsDisjunct(&mg_in->mgroup, &mg_strips->strips.init))
        return;

    BOR_ISET(facts);
    borISetUnion(&facts, &mg_in->mgroup);
    borISetMinus(&facts, uncovered_del_effs);

    pddl_mgroup_t *mg = pddlMGroupsAdd(&mg_strips->mg, &facts);
    if (!pddlMGroupIsExactlyOne(mg, &mg_strips->strips)){
        pddl_fact_t none_of_those;
        pddlFactInit(&none_of_those);
        int name_size = 5;
        int fact_id;
        BOR_ISET_FOR_EACH(&facts, fact_id){
            name_size += strlen(mg_strips->strips.fact.fact[fact_id]->name);
            name_size += 1;
        }
        none_of_those.name = BOR_ALLOC_ARR(char, name_size);
        char *cur = none_of_those.name;
        cur += sprintf(cur, "NOT:");
        BOR_ISET_FOR_EACH(&facts, fact_id){
            cur += sprintf(cur, "%s;",
                           mg_strips->strips.fact.fact[fact_id]->name);
        }
        *(cur - 1) = 0x0;

        int new_fact_id = pddlFactsAdd(&mg_strips->strips.fact, &none_of_those);
        ASSERT(new_fact_id == mg_strips->strips.fact.fact_size - 1);
        borISetAdd(&mg->mgroup, new_fact_id);
        pddlFactFree(&none_of_those);

        for (int op_id = 0; op_id < mg_strips->strips.op.op_size; ++op_id){
            pddl_strips_op_t *op = mg_strips->strips.op.op[op_id];
            int in_del = !borISetIsDisjunct(&op->del_eff, &facts);
            int in_add = !borISetIsDisjunct(&op->add_eff, &facts);
            if (in_del && !in_add)
                borISetAdd(&op->add_eff, new_fact_id);
            if (!in_del && in_add){
#ifdef PDDL_DEBUG
                BOR_ISET(test);
                borISetIntersect2(&test, &op->del_eff, &op->pre);
                borISetIntersect(&test, uncovered_del_effs);
                borISetIntersect(&test, &mg_in->mgroup);
                ASSERT(borISetSize(&test) > 0);
                borISetFree(&test);
#endif /* PDDL_DEBUG */
                borISetAdd(&op->del_eff, new_fact_id);
            }
        }

        if (borISetIsDisjunct(&mg_strips->strips.init, &facts))
            borISetAdd(&mg_strips->strips.init, new_fact_id);
    }

    mg->is_goal = !borISetIsDisjunct(&mg->mgroup, &mg_strips->strips.goal);
    mg->is_exactly_one = 1;

    borISetFree(&facts);

    ASSERT(pddlMGroupIsExactlyOne(mg, &mg_strips->strips));
}

static void encodeBinaryFact(pddl_mg_strips_t *mg_strips, int fact_id)
{
    ASSERT(mg_strips->strips.fact.fact[fact_id]->neg_of < 0);
    int name_size = strlen(mg_strips->strips.fact.fact[fact_id]->name) + 4;

    pddl_fact_t not;
    pddlFactInit(&not);
    not.name = BOR_ALLOC_ARR(char, name_size);
    sprintf(not.name, "NOT:%s", mg_strips->strips.fact.fact[fact_id]->name);
    int not_id = pddlFactsAdd(&mg_strips->strips.fact, &not);
    ASSERT(not_id == mg_strips->strips.fact.fact_size - 1);

    for (int op_id = 0; op_id < mg_strips->strips.op.op_size; ++op_id){
        pddl_strips_op_t *op = mg_strips->strips.op.op[op_id];
        int in_del = borISetIn(fact_id, &op->del_eff);
        int in_add = borISetIn(fact_id, &op->add_eff);

        if (in_del && !in_add)
            borISetAdd(&op->add_eff, not_id);
        if (!in_del && in_add)
            borISetAdd(&op->del_eff, not_id);
    }

    if (!borISetIn(fact_id, &mg_strips->strips.init))
        borISetAdd(&mg_strips->strips.init, not_id);
    pddlFactFree(&not);

    BOR_ISET(facts);
    BOR_ISET_SET(&facts, fact_id, not_id);
    pddl_mgroup_t *mg = pddlMGroupsAdd(&mg_strips->mg, &facts);
    mg->is_exactly_one = 1;
    mg->is_goal = !borISetIsDisjunct(&mg->mgroup, &mg_strips->strips.goal);
    borISetFree(&facts);

    ASSERT(pddlMGroupIsExactlyOne(mg, &mg_strips->strips));
}

static void encodeBinaryFacts(pddl_mg_strips_t *mg_strips)
{
    BOR_ISET(covered);
    BOR_ISET(not_covered);

    mgroupsFacts(&covered, &not_covered, &mg_strips->mg,
                 mg_strips->strips.fact.fact_size);

    int fact_id;
    BOR_ISET_FOR_EACH(&not_covered, fact_id)
        encodeBinaryFact(mg_strips, fact_id);

    borISetFree(&covered);
    borISetFree(&not_covered);
}

void pddlMGStripsInit(pddl_mg_strips_t *mg_strips,
                      const pddl_strips_t *strips,
                      const pddl_mgroups_t *mgroups,
                      const pddl_mutex_pairs_t *mutex)
{
    if (strips->has_cond_eff)
        BOR_FATAL2("pddlMGStripsInit: conditional effects not yet supported.");

    // TODO: Remove subsets from mgroups

    pddlStripsInitCopy(&mg_strips->strips, strips);
    pddlMGroupsInitEmpty(&mg_strips->mg);

    // Find facts that appear in delete effects but not in the precondition
    BOR_ISET(uncovered_del_effs);
    BOR_ISET(tmp);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        const pddl_strips_op_t *op = strips->op.op[op_id];
        borISetMinus2(&tmp, &op->del_eff, &op->pre);
        borISetUnion(&uncovered_del_effs, &tmp);
    }
    borISetFree(&tmp);

    // First add binary facts we know
    for (int fact_id = 0; fact_id < strips->fact.fact_size; ++fact_id){
        const pddl_fact_t *fact = strips->fact.fact[fact_id];
        if (fact->neg_of > fact_id){
            BOR_ISET(facts);
            BOR_ISET_SET(&facts, fact_id, fact->neg_of);
            pddl_mgroup_t *mg = pddlMGroupsAdd(&mg_strips->mg, &facts);
            mg->is_exactly_one = 1;
            borISetFree(&facts);
        }
    }

    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg_in = mgroups->mgroup + mgi;
        if (borISetSize(&mg_in->mgroup) <= 1)
            continue;

        if (pddlMGroupIsExactlyOne(mg_in, strips)){
            // Copy exactly-one mutex groups directly to mg-strips
            pddl_mgroup_t *mg = pddlMGroupsAdd(&mg_strips->mg, &mg_in->mgroup);
            mg->is_exactly_one = 1;
            mg->is_fam_group = mg_in->is_fam_group;
            mg->is_goal = mg_in->is_goal;

        }else{
            // Mutex groups that are not exactly-one need "none-of-those"
            // fact
            makeMGroupExactlyOne(mg_strips, &uncovered_del_effs, mg_in);
        }
    }

    encodeBinaryFacts(mg_strips);

    borISetFree(&uncovered_del_effs);

    pddlStripsPrintDebug(&mg_strips->strips, stderr);
    pddlMGroupsPrint(NULL, &mg_strips->strips, &mg_strips->mg, stderr);
}

void pddlMGStripsFree(pddl_mg_strips_t *mg_strips)
{
    pddlStripsFree(&mg_strips->strips);
    pddlMGroupsFree(&mg_strips->mg);
}
