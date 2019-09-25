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

static void makeMGroupExactlyOne(pddl_mg_strips_t *mg_strips,
                                 const pddl_mgroup_t *mg_in)
{
    if (borISetIsDisjunct(&mg_in->mgroup, &mg_strips->strips.init))
        return;

    BOR_ISET(facts);
    borISetUnion(&facts, &mg_in->mgroup);

    pddl_mgroup_t *mg = pddlMGroupsAdd(&mg_strips->mg, &facts);
    if (!pddlStripsIsExactlyOneMGroup(&mg_strips->strips, &facts)){
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

    borISetFree(&facts);
}

static void encodeBinaryFact(pddl_mg_strips_t *mg_strips, int fact_id,
                             int *covered)
{
    pddl_fact_t *fact = mg_strips->strips.fact.fact[fact_id];

    int not_id;
    if (fact->neg_of >= 0){
        not_id = fact->neg_of;
        covered[not_id] = 1;
    }else{
        pddl_fact_t not;
        pddlFactInit(&not);
        int name_size = strlen(mg_strips->strips.fact.fact[fact_id]->name) + 5;
        not.name = BOR_ALLOC_ARR(char, name_size);
        sprintf(not.name, "NOT:%s", mg_strips->strips.fact.fact[fact_id]->name);
        not_id = pddlFactsAdd(&mg_strips->strips.fact, &not);
        ASSERT(not_id == mg_strips->strips.fact.fact_size - 1);
        pddlFactFree(&not);

        fact->neg_of = not_id;
        mg_strips->strips.fact.fact[not_id]->neg_of = fact_id;
    }
    covered[fact_id] = 1;

    for (int op_id = 0; op_id < mg_strips->strips.op.op_size; ++op_id){
        pddl_strips_op_t *op = mg_strips->strips.op.op[op_id];
        int in_del = borISetIn(fact_id, &op->del_eff);
        int in_add = borISetIn(fact_id, &op->add_eff);
        int in_del_not = borISetIn(not_id, &op->del_eff);
        int in_add_not = borISetIn(not_id, &op->add_eff);

        if (in_del && !in_add)
            borISetAdd(&op->add_eff, not_id);
        if (!in_del && in_add)
            borISetAdd(&op->del_eff, not_id);
        if (in_del_not && !in_add_not)
            borISetAdd(&op->add_eff, fact_id);
        if (!in_del_not && in_add_not)
            borISetAdd(&op->del_eff, fact_id);
    }

    if (!borISetIn(fact_id, &mg_strips->strips.init))
        borISetAdd(&mg_strips->strips.init, not_id);

    BOR_ISET(facts);
    BOR_ISET_SET(&facts, fact_id, not_id);
    pddlMGroupsAdd(&mg_strips->mg, &facts);
    borISetFree(&facts);
}

static void encodeBinaryFacts(pddl_mg_strips_t *mg_strips)
{
    int fact_size = mg_strips->strips.fact.fact_size;
    int *covered;

    covered = BOR_CALLOC_ARR(int, fact_size);
    for (int mi = 0; mi < mg_strips->mg.mgroup_size; ++mi){
        const pddl_mgroup_t *mg = mg_strips->mg.mgroup + mi;
        int fact_id;
        BOR_ISET_FOR_EACH(&mg->mgroup, fact_id)
            covered[fact_id] = 1;
    }

    for (int fact_id = 0; fact_id < fact_size; ++fact_id){
        if (!covered[fact_id])
            encodeBinaryFact(mg_strips, fact_id, covered);
    }

    BOR_FREE(covered);
}

static void encodeMGroups(pddl_mg_strips_t *mg_strips,
                          pddl_mgroups_t *mgroups)
{
    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg_in = mgroups->mgroup + mgi;
        if (borISetSize(&mg_in->mgroup) <= 1)
            continue;

        if (pddlStripsIsExactlyOneMGroup(&mg_strips->strips, &mg_in->mgroup)){
            // Copy exactly-one mutex groups directly to mg-strips
            pddlMGroupsAdd(&mg_strips->mg, &mg_in->mgroup);

        }else{
            // Mutex groups that are not exactly-one need "none-of-those"
            // fact
            makeMGroupExactlyOne(mg_strips, mg_in);
        }
    }
}

static void findUncoveredDelEffs(bor_iset_t *out, const pddl_strips_t *strips)
{
    BOR_ISET(tmp);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        const pddl_strips_op_t *op = strips->op.op[op_id];
        borISetMinus2(&tmp, &op->del_eff, &op->pre);
        borISetUnion(out, &tmp);
    }
    borISetFree(&tmp);
}

static void prepareMGroups(pddl_mgroups_t *dst, const pddl_mgroups_t *src,
                           const bor_iset_t *uncovered_del_effs)
{
    pddlMGroupsInitCopy(dst, src);

    for (int mi = 0; mi < dst->mgroup_size; ++mi)
        borISetMinus(&dst->mgroup[mi].mgroup, uncovered_del_effs);

    for (int mi = 0; mi < dst->mgroup_size; ++mi){
        pddl_mgroup_t *m1 = dst->mgroup + mi;
        int m1size = borISetSize(&m1->mgroup);
        if (m1size <= 1)
            continue;

        for (int mi2 = mi + 1; mi2 < dst->mgroup_size; ++mi2){
            pddl_mgroup_t *m2 = dst->mgroup + mi2;
            int m2size = borISetSize(&m1->mgroup);
            if (m2size <= 1)
                continue;

            if (m1size == m2size){
                continue;
            }else if (m1size < m2size){
                if (borISetIsSubset(&m1->mgroup, &m2->mgroup)){
                    borISetEmpty(&m1->mgroup);
                    break;
                }
            }else{
                if (borISetIsSubset(&m2->mgroup, &m1->mgroup))
                    borISetEmpty(&m2->mgroup);
            }
        }
    }
    pddlMGroupsRemoveSmall(dst, 1);
    pddlMGroupsSortUniq(dst);
    pddlMGroupsSortBySizeDesc(dst);
}
                                

void pddlMGStripsInit(pddl_mg_strips_t *mg_strips,
                      const pddl_strips_t *strips,
                      const pddl_mgroups_t *mgroups_in)
{
    if (strips->has_cond_eff)
        BOR_FATAL2("pddlMGStripsInit: conditional effects not yet supported.");

    // Find facts that appear in delete effects but not in the precondition
    BOR_ISET(uncovered_del_effs);
    findUncoveredDelEffs(&uncovered_del_effs, strips);

    // Prepare mutex groups: remove subsets, remove mutex groups having
    // less than two facts and sort them.
    pddl_mgroups_t mgroups;
    prepareMGroups(&mgroups, mgroups_in, &uncovered_del_effs);

    pddlStripsInitCopy(&mg_strips->strips, strips);
    pddlMGroupsInitEmpty(&mg_strips->mg);

    encodeMGroups(mg_strips, &mgroups);
    encodeBinaryFacts(mg_strips);

    borISetFree(&uncovered_del_effs);
    pddlMGroupsFree(&mgroups);

    //pddlStripsPrintDebug(&mg_strips->strips, stderr);
    //pddlMGroupsPrint(NULL, &mg_strips->strips, &mg_strips->mg, stderr);

    // Set flags
    for (int mi = 0; mi < mg_strips->mg.mgroup_size; ++mi){
        pddl_mgroup_t *mg = mg_strips->mg.mgroup + mi;
        mg->is_exactly_one = 1;
        mg->is_goal = !borISetIsDisjunct(&mg->mgroup, &mg_strips->strips.goal);
        mg->is_fam_group = pddlStripsIsFAMGroup(&mg_strips->strips,
                                                &mg->mgroup);
        ASSERT_RUNTIME(!borISetIsDisjunct(&mg_strips->strips.init,
                                          &mg->mgroup));
        ASSERT_RUNTIME(pddlStripsIsExactlyOneMGroup(&mg_strips->strips,
                                                    &mg->mgroup));
    }
}

void pddlMGStripsFree(pddl_mg_strips_t *mg_strips)
{
    pddlStripsFree(&mg_strips->strips);
    pddlMGroupsFree(&mg_strips->mg);
}
