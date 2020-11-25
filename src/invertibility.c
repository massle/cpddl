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

#include <boruvka/alloc.h>
#include "pddl/disambiguation.h"
#include "pddl/strips_fact_cross_ref.h"
#include "pddl/invertibility.h"

struct op {
    int op_id;
    const bor_iset_t *pre;
    bor_iset_t eff; /*! op applied on pre */
    bor_iset_t eff_pre; /*! op applied on pre \cup pre */
};
typedef struct op op_t;

struct fact {
    int fact_id;
    bor_iset_t op_add;
    bor_iset_t op_del;
    int is_invertible;
};
typedef struct fact fact_t;

static int factIsInvertible(const fact_t *fact, const op_t *op, bor_err_t *err)
{
    if (borISetSize(&fact->op_del) == 0
            || borISetSize(&fact->op_add) == 0)
        return 0;

    int add_op, del_op;
    BOR_ISET_FOR_EACH(&fact->op_del, del_op){
        int found = 0;
        BOR_ISET_FOR_EACH(&fact->op_add, add_op){
            if (borISetIsSubset(op[add_op].pre, &op[del_op].eff_pre)){
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }

    BOR_ISET_FOR_EACH(&fact->op_add, add_op){
        int found = 0;
        BOR_ISET_FOR_EACH(&fact->op_del, del_op){
            if (borISetIsSubset(op[del_op].pre, &op[add_op].eff_pre)){
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }

    return 1;
}

static pddl_invertible_mgroup_t *
        invertibleMGroupAdd(pddl_invertible_mgroups_t *invmgs)
{
    if (invmgs->mgroup_size == invmgs->mgroup_alloc){
        if (invmgs->mgroup_alloc == 0)
            invmgs->mgroup_alloc = 1;
        invmgs->mgroup_alloc *= 2;
        invmgs->mgroup = BOR_REALLOC_ARR(invmgs->mgroup,
                                         pddl_invertible_mgroup_t,
                                         invmgs->mgroup_alloc);
    }
    pddl_invertible_mgroup_t *mg = invmgs->mgroup + invmgs->mgroup_size++;
    bzero(mg, sizeof(*mg));
    mg->mgroup_id = -1;
    return mg;
}

static int invertibleMGroup(pddl_invertible_mgroups_t *invmgs,
                            const fact_t *fact,
                            int mgroup_id,
                            const pddl_mgroup_t *mgroup,
                            bor_err_t *err)
{
    BOR_ISET(inv);
    int fact_id;
    BOR_ISET_FOR_EACH(&mgroup->mgroup, fact_id){
        if (fact[fact_id].is_invertible)
            borISetAdd(&inv, fact_id);
    }

    if (borISetSize(&inv) > 0){
        pddl_invertible_mgroup_t *mg = invertibleMGroupAdd(invmgs);
        mg->mgroup_id = mgroup_id;
        borISetUnion(&mg->mgroup, &inv);
        borISetUnion(&invmgs->invertible_fact, &inv);
    }
    borISetFree(&inv);
    return 0;
}

int pddlInvertibleMGroupsFind(pddl_invertible_mgroups_t *invmgs,
                              const pddl_strips_t *strips,
                              const pddl_mgroups_t *mgroups,
                              const pddl_mutex_pairs_t *mutex,
                              bor_err_t *err)
{
    if (strips->has_cond_eff){
        BOR_FATAL2("pddlInvertibleMGroupsFind() does not support"
                   " conditional effects!");
    }

    int ret = 0;
    bzero(invmgs, sizeof(*invmgs));

    pddl_disambiguate_t dis;
    if (mutex != NULL)
        pddlDisambiguateInit(&dis, strips->fact.fact_size, mutex, mgroups);

    op_t *op = BOR_CALLOC_ARR(op_t, strips->op.op_size);
    fact_t *fact = BOR_CALLOC_ARR(fact_t, strips->fact.fact_size);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        const pddl_strips_op_t *sop = strips->op.op[op_id];
        op[op_id].op_id = op_id;
        op[op_id].pre = &sop->pre;
        if (mutex != NULL){
            pddlDisambiguate(&dis, &sop->pre, NULL, 0, 0, NULL,
                             &op[op_id].eff);
        }else{
            borISetUnion(&op[op_id].eff, &sop->pre);
        }
        borISetMinus(&op[op_id].eff, &sop->del_eff);
        //borISetMinus2(&op[op_id].eff, &sop->pre, &sop->del_eff);
        borISetUnion(&op[op_id].eff, &sop->add_eff);
        borISetUnion2(&op[op_id].eff_pre, &op[op_id].eff, &sop->pre);

        int fid;
        BOR_ISET_FOR_EACH(&sop->add_eff, fid)
            borISetAdd(&fact[fid].op_add, op_id);
        BOR_ISET_FOR_EACH(&sop->del_eff, fid)
            borISetAdd(&fact[fid].op_del, op_id);
    }

    if (mutex != NULL)
        pddlDisambiguateFree(&dis);

    for (int fid = 0; fid < strips->fact.fact_size; ++fid){
        fact[fid].fact_id = fid;
        fact[fid].is_invertible = factIsInvertible(fact + fid, op, err);
    }

    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mgroups->mgroup + mgi;
        ret = invertibleMGroup(invmgs, fact, mgi, mg, err);
        if (ret != 0)
            break;
    }

    for (int i = 0; i < invmgs->mgroup_size; ++i){
        const pddl_invertible_mgroup_t *img = invmgs->mgroup + i;
        const pddl_mgroup_t *mg = mgroups->mgroup + img->mgroup_id;

        if (borISetSize(&img->mgroup) != borISetSize(&mg->mgroup)){
            fprintf(stderr, "MG %d:", img->mgroup_id);
            int fact_id;
            BOR_ISET_FOR_EACH(&mg->mgroup, fact_id){
                fprintf(stderr, " %d", fact_id);
            }
            fprintf(stderr, " |");
            BOR_ISET_FOR_EACH(&img->mgroup, fact_id){
                fprintf(stderr, " %d:(%s)", fact_id,
                        strips->fact.fact[fact_id]->name);
            }
            fprintf(stderr, "\n");
        }else{
            fprintf(stderr, "MG %d: EXACT\n", img->mgroup_id);
        }
        fprintf(stderr, "     MG %d: ", img->mgroup_id);
        pddlMGroupPrint(NULL, strips, mg, stderr);
    }

    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        borISetFree(&op[op_id].eff);
    }
    BOR_FREE(op);
    for (int fid = 0; fid < strips->fact.fact_size; ++fid){
        borISetFree(&fact[fid].op_add);
        borISetFree(&fact[fid].op_del);
    }
    BOR_FREE(fact);
    return ret;
}


static int existOpWithPreSubsetPreAdd(const pddl_strips_t *strips,
                                      const bor_iset_t *ops,
                                      int op_pre_add_id)
{
    const pddl_strips_op_t *op = strips->op.op[op_pre_add_id];
    BOR_ISET(pre_add);
    borISetUnion2(&pre_add, &op->pre, &op->add_eff);

    int op_id;
    BOR_ISET_FOR_EACH(ops, op_id){
        const pddl_strips_op_t *op = strips->op.op[op_id];
        if (borISetIsSubset(&op->pre, &pre_add)){
            borISetFree(&pre_add);
            return 1;
        }
    }

    borISetFree(&pre_add);
    return 0;
}

static int forEveryOpExistsOpWithPreSubsetPreAdd(const pddl_strips_t *strips,
                                                 const bor_iset_t *for_every,
                                                 const bor_iset_t *exist)
{
    int op_id;
    BOR_ISET_FOR_EACH(for_every, op_id){
        if (!existOpWithPreSubsetPreAdd(strips, exist, op_id))
            return 0;
    }
    return 1;
}

static int factDelIsInvertible(const pddl_strips_t *strips,
                               const pddl_strips_fact_cross_ref_fact_t *cref)
{
    return forEveryOpExistsOpWithPreSubsetPreAdd(
                strips, &cref->op_del, &cref->op_add);
}

static int factAddIsInvertible(const pddl_strips_t *strips,
                               const pddl_strips_fact_cross_ref_fact_t *cref)
{
    return forEveryOpExistsOpWithPreSubsetPreAdd(
                strips, &cref->op_add, &cref->op_del);
}

int pddlRSEInvertibleFacts(const pddl_strips_t *strips,
                           const pddl_mgroups_t *fam_groups,
                           bor_iset_t *invertible_facts,
                           bor_err_t *err)
{
    pddl_strips_fact_cross_ref_t cref;
    pddlStripsFactCrossRefInit(&cref, strips, 0, 0, 0, 1, 1);

    int fact_size = strips->fact.fact_size;
    int *fam_fact = BOR_CALLOC_ARR(int, fact_size);
    for (int mgi = 0; mgi < fam_groups->mgroup_size; ++mgi){
        int fact;
        BOR_ISET_FOR_EACH(&fam_groups->mgroup[mgi].mgroup, fact)
            fam_fact[fact] = 1;
    }

    for (int fact = 0; fact < fact_size; ++fact){
        const pddl_strips_fact_cross_ref_fact_t *cref_fact = &cref.fact[fact];
        if (factDelIsInvertible(strips, cref_fact)
                && (!fam_fact[fact]
                        || factAddIsInvertible(strips, cref_fact))){
            borISetAdd(invertible_facts, fact);
        }
    }

    BOR_FREE(fam_fact);
    pddlStripsFactCrossRefFree(&cref);
    BOR_INFO2(err, "Invertible facts");
    return 0;
}
