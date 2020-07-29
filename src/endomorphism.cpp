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

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define IL_STD
#include <ilcp/cp.h>

#include <boruvka/alloc.h>
#include <boruvka/iarr.h>
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include "pddl/endomorphism.h"
#include "assert.h"

struct pre_eff_vars {
    int group_id;
    bor_iset_t pre;
    bor_iset_t eff;
    bor_htable_key_t key;
    bor_list_t htable;
};
typedef struct pre_eff_vars pre_eff_vars_t;

struct op_groups {
    bor_iset_t *group;
    int group_size;
    int group_alloc;
    bor_htable_t *htable;
};
typedef struct op_groups op_groups_t;

static bor_htable_key_t preEffComputeHash(const pre_eff_vars_t *v)
{
    uint64_t key;
    ((uint32_t *)&key)[0] = borFastHash_32(v->pre.s, v->pre.size, 13);
    ((uint32_t *)&key)[1] = borFastHash_32(v->eff.s, v->eff.size, 13);
    return key;
}

static bor_htable_key_t preEffHash(const bor_list_t *l, void *_)
{
    const pre_eff_vars_t *v = BOR_LIST_ENTRY(l, pre_eff_vars_t, htable);
    return v->key;
}

static int preEffEq(const bor_list_t *l1, const bor_list_t *l2, void *_)
{
    const pre_eff_vars_t *v1 = BOR_LIST_ENTRY(l1, pre_eff_vars_t, htable);
    const pre_eff_vars_t *v2 = BOR_LIST_ENTRY(l2, pre_eff_vars_t, htable);
    return borISetEq(&v1->pre, &v2->pre) && borISetEq(&v1->eff, &v2->eff);
}

static void assignToGroup(op_groups_t *opgs, const pddl_fdr_op_t *op)
{
    pre_eff_vars_t *pev = BOR_ALLOC(pre_eff_vars_t);
    bzero(pev, sizeof(*pev));
    for (int fi = 0; fi < op->pre.fact_size; ++fi)
        borISetAdd(&pev->pre, op->pre.fact[fi].var);
    for (int fi = 0; fi < op->eff.fact_size; ++fi)
        borISetAdd(&pev->eff, op->eff.fact[fi].var);
    pev->key = preEffComputeHash(pev);
    borListInit(&pev->htable);

    bor_list_t *found;
    if ((found = borHTableInsertUnique(opgs->htable, &pev->htable)) == NULL){
        if (opgs->group_size == opgs->group_alloc){
            if (opgs->group_alloc == 0)
                opgs->group_alloc = 2;
            opgs->group_alloc *= 2;
            opgs->group = BOR_REALLOC_ARR(opgs->group, bor_iset_t,
                                          opgs->group_alloc);
        }
        int group_id = opgs->group_size++;
        bor_iset_t *g = opgs->group + group_id;
        borISetInit(g);
        pev->group_id = group_id;
        borISetAdd(g, op->id);

    }else{
        pev = BOR_LIST_ENTRY(found, pre_eff_vars_t, htable);
        borISetAdd(opgs->group + pev->group_id, op->id);
    }
}

static void opGroupsInit(op_groups_t *opg, const pddl_fdr_t *fdr)
{
    bzero(opg, sizeof(*opg));
    opg->htable = borHTableNew(preEffHash, preEffEq, NULL);
    for (int oi = 0; oi < fdr->op.op_size; ++oi)
        assignToGroup(opg, fdr->op.op[oi]);
}

static void opGroupsFree(op_groups_t *opg)
{
    for (int i = 0; i < opg->group_size; ++i)
        borISetFree(opg->group + i);
    if (opg->group != NULL)
        BOR_FREE(opg->group);

    bor_list_t list;
    borListInit(&list);
    borHTableGather(opg->htable, &list);
    while (!borListEmpty(&list)){
        bor_list_t *item = borListNext(&list);
        borListDel(item);
        pre_eff_vars_t *v = BOR_LIST_ENTRY(item, pre_eff_vars_t, htable);
        borISetFree(&v->pre);
        borISetFree(&v->eff);
        BOR_FREE(v);
    }
    borHTableDel(opg->htable);
}

static void setOpPreConstr(const pddl_fdr_op_t *op,
                           const bor_iset_t *group,
                           const pddl_fdr_t *fdr,
                           IloEnv &env,
                           IloModel &model,
                           IloIntVarArray &fact_var,
                           IloIntVarArray &op_var,
                           bor_err_t *err)
{
    int other_op_id;

    int pre_size = op->pre.fact_size + 1;
    IloIntTupleSet pre(env, pre_size);
    BOR_ISET_FOR_EACH(group, other_op_id){
        const pddl_fdr_op_t *other_op = fdr->op.op[other_op_id];
        if (other_op->cost > op->cost)
            continue;
        IloIntArray vals(env, pre_size);
        vals[0] = other_op->id;
        for (int fi = 0; fi < other_op->pre.fact_size; ++fi)
            vals[fi + 1] = other_op->pre.fact[fi].val;
        pre.add(vals);
    }

    IloIntVarArray pre_var(env, pre_size);
    pre_var[0] = op_var[op->id];
    for (int fi = 0; fi < op->pre.fact_size; ++fi){
        int pvar = op->pre.fact[fi].var;
        int pval = op->pre.fact[fi].val;
        pre_var[fi + 1] = fact_var[fdr->var.var[pvar].val[pval].global_id];
    }

    model.add(IloAllowedAssignments(env, pre_var, pre));
}

static void setOpEffConstr(const pddl_fdr_op_t *op,
                           const bor_iset_t *group,
                           const pddl_fdr_t *fdr,
                           IloEnv &env,
                           IloModel &model,
                           IloIntVarArray &fact_var,
                           IloIntVarArray &op_var,
                           bor_err_t *err)
{
    int other_op_id;

    int eff_size = op->eff.fact_size + 1;
    IloIntTupleSet eff(env, eff_size);
    BOR_ISET_FOR_EACH(group, other_op_id){
        const pddl_fdr_op_t *other_op = fdr->op.op[other_op_id];
        if (other_op->cost > op->cost)
            continue;
        IloIntArray vals(env, eff_size);
        vals[0] = other_op->id;
        for (int fi = 0; fi < other_op->eff.fact_size; ++fi)
            vals[fi + 1] = other_op->eff.fact[fi].val;
        eff.add(vals);
    }

    IloIntVarArray eff_var(env, eff_size);
    eff_var[0] = op_var[op->id];
    for (int fi = 0; fi < op->eff.fact_size; ++fi){
        int pvar = op->eff.fact[fi].var;
        int pval = op->eff.fact[fi].val;
        eff_var[fi + 1] = fact_var[fdr->var.var[pvar].val[pval].global_id];
    }

    model.add(IloAllowedAssignments(env, eff_var, eff));
}

static void setOpConstr(int op_id,
                        const bor_iset_t *group,
                        const pddl_fdr_t *fdr,
                        IloEnv &env,
                        IloModel &model,
                        IloIntVarArray &fact_var,
                        IloIntVarArray &op_var,
                        bor_err_t *err)
{
    const pddl_fdr_op_t *op = fdr->op.op[op_id];
    setOpPreConstr(op, group, fdr, env, model, fact_var, op_var, err);
    setOpEffConstr(op, group, fdr, env, model, fact_var, op_var, err);
}

void pddlEndomorphismFindMaximal(const pddl_fdr_t *fdr,
                                 bor_err_t *err)
{
    BOR_INFO2(err, "Endomorphism on FDR ...");
    op_groups_t opg;
    opGroupsInit(&opg, fdr);
    BOR_INFO(err, "  Operators grouped into %d groups", opg.group_size);

    IloEnv env;
    IloModel model(env);

    // Create fact variables
    IloIntVarArray var_fact(env, fdr->var.global_id_size);
    for (int fi = 0; fi < fdr->var.global_id_size; ++fi){
        const pddl_fdr_val_t *val = fdr->var.global_id_to_val[fi];
        const pddl_fdr_var_t *var = fdr->var.var + val->var_id;
        var_fact[fi] = IloIntVar(env, 0, var->val_size - 1, val->name);
    }

    // Create operator variables
    IloIntVarArray var_op(env, fdr->op.op_size);
    for (int oi = 0; oi < fdr->op.op_size; ++oi){
        const pddl_fdr_op_t *op = fdr->op.op[oi];
        var_op[oi] = IloIntVar(env, 0, fdr->op.op_size - 1, op->name);
    }
    BOR_INFO(err, "  Created %d fact and %d operator variables",
             fdr->var.global_id_size, fdr->op.op_size);

    // Set init constraint
    for (int vi = 0; vi < fdr->var.var_size; ++vi){
        int fact_id = fdr->var.var[vi].val[fdr->init[vi]].global_id;
        model.add(var_fact[fact_id] == fdr->init[vi]);
    }

    // Set goal constraint
    for (int fi = 0; fi < fdr->goal.fact_size; ++fi){
        int var = fdr->goal.fact[fi].var;
        int val = fdr->goal.fact[fi].val;
        int fact_id = fdr->var.var[var].val[val].global_id;
        model.add(var_fact[fact_id] == val);
    }
    BOR_INFO2(err, "  Added init and goal constraints");

    // Set operator constraints
    for (int group_id = 0; group_id < opg.group_size; ++group_id){
        int op_id;
        const bor_iset_t *group = &opg.group[group_id];
        BOR_ISET_FOR_EACH(group, op_id)
            setOpConstr(op_id, group, fdr, env, model, var_fact, var_op, err);
        //BOR_INFO(err, "  Created operator constraints %d",
        //         borISetSize(&opg.group[group_id]));
    }
    BOR_INFO2(err, "  Added operator constraints");
    opGroupsFree(&opg);

    IloOr non_identity(env);
    for (int op_id = 0; op_id < fdr->op.op_size; ++op_id)
        non_identity.add(var_op[op_id] != op_id);
    model.add(non_identity);
    BOR_INFO2(err, "  Added non-identity constraint");

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function min(count-diff())");

    IloCP cp(model);
    //cp.dumpModel("model.cpo");
    //cp.setParameter(IloCP::LogVerbosity, IloCP::Quiet);
    BOR_INFO2(err, "  Solving model ...");
    if (cp.solve()){
        BOR_INFO2(err, "  Found maximum");
        for (int op_id = 0; op_id < fdr->op.op_size; ++op_id){
            int value = cp.getValue(var_op[op_id]);
            if (value != op_id){
                BOR_INFO(err, "    :: op %d -> %d", op_id, value);
            }
        }
    }else{
        BOR_INFO2(err, "UNSOLVABLE");
    }
}
