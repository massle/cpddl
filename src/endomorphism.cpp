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

//#define DEBUG_PRINT_OP_MAPPING

#include "pddl/config.h"
#include "pddl/endomorphism.h"

#ifdef PDDL_CPOPTIMIZER
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define IL_STD
#include <ilcp/cp.h>
#include <ilcplex/cpxconst.h>

#include <boruvka/alloc.h>
#include <boruvka/iarr.h>
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include "assert.h"

#if CPX_VERSION_VERSION < 12 || CPX_VERSION_RELEASE < 9
# define NO_LOGGER
#endif

#ifndef NO_LOGGER
class Logger : public IloCP::Callback {
    bor_err_t *err;

  public:
    Logger(bor_err_t *err) : err(err){}
#if CPX_VERSION_VERSION == 12
# if CPX_VERSION_RELEASE == 9
    virtual void invoke(IloCP cp, Callback::Type reason)
# endif
# if CPX_VERSION_RELEASE == 10
    virtual void invoke(IloCP cp, Callback::Reason reason)
# endif
#endif
    {
        if (reason == Periodic){
            BOR_INFO(err, "    cpoptimizer: mem: %ldMB, solutions: %d",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions));

        }else if (reason == Solution){
            BOR_INFO(err, "    cpoptimizer: mem: %ldMB, solutions: %d",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions));

        }else if (reason == Proof){
            BOR_INFO(err, "    cpoptimizer: mem: %ldMB, solutions: %d,"
                          " proof",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions));

        }else if (reason == ObjBound){
            BOR_INFO(err, "    cpoptimizer: mem: %ldMB, solutions: %d,"
                          " new bound: %.2f",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions),
                     (double)cp.getObjBound());
        }
    }
};
#endif /* NO_LOGGER */

struct mg_strips_op {
    int cost;
    pddl_fdr_part_state_t pre;
    pddl_fdr_part_state_t eff;
};
typedef struct mg_strips_op mg_strips_op_t;

struct mg_strips {
    const pddl_strips_t *strips;
    const pddl_mgroups_t *mgroup;
    int fact_size;
    bor_iset_t *fact_to_mgroup;
    int op_size;
    mg_strips_op_t *op;

    int *fact_to_cvar;
    int *fact_identity;
    int *op_identity;
    int cvar_fact_size;
    int non_identity_cvar_op_size;
};
typedef struct mg_strips mg_strips_t;

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

static void assignOpToGroup(op_groups_t *opgs,
                            int op_id,
                            const pddl_fdr_part_state_t *pre,
                            const pddl_fdr_part_state_t *eff)
{
    pre_eff_vars_t *pev = BOR_ALLOC(pre_eff_vars_t);
    bzero(pev, sizeof(*pev));
    for (int fi = 0; fi < pre->fact_size; ++fi)
        borISetAdd(&pev->pre, pre->fact[fi].var);
    for (int fi = 0; fi < eff->fact_size; ++fi)
        borISetAdd(&pev->eff, eff->fact[fi].var);
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
        borISetAdd(g, op_id);

    }else{
        pev = BOR_LIST_ENTRY(found, pre_eff_vars_t, htable);
        borISetAdd(opgs->group + pev->group_id, op_id);
    }
}

static void opGroupsInitFDR(op_groups_t *opg, const pddl_fdr_t *fdr)
{
    bzero(opg, sizeof(*opg));
    opg->htable = borHTableNew(preEffHash, preEffEq, NULL);
    for (int oi = 0; oi < fdr->op.op_size; ++oi){
        const pddl_fdr_op_t *op = fdr->op.op[oi];
        assignOpToGroup(opg, op->id, &op->pre, &op->eff);
    }
}

static void opGroupsInitMGStrips(op_groups_t *opg, const mg_strips_t *mgs)
{
    bzero(opg, sizeof(*opg));
    opg->htable = borHTableNew(preEffHash, preEffEq, NULL);
    for (int oi = 0; oi < mgs->op_size; ++oi){
        const mg_strips_op_t *op = mgs->op + oi;
        assignOpToGroup(opg, oi, &op->pre, &op->eff);
    }
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

static int solve(IloModel &model,
                 IloIntVarArray &var_op,
                 int *values,
                 bor_err_t *err)
{
    int ret = 0;
    IloCP cp(model);
    //cp.dumpModel("model.cpo");
#ifndef NO_LOGGER
    Logger *logger = new Logger(err);
    cp.addCallback(logger);
#endif /* NO_LOGGER */
    cp.setParameter(IloCP::LogVerbosity, IloCP::Quiet);
    cp.setParameter(IloCP::Workers, 1);
    // TODO
    //cp.setParameter(IloCP::TimeLimit, 3600);
    //cp.setParameter(IloCP::TimeLimit, 5);

    BOR_INFO2(err, "  Solving model ...");
    if (cp.solve()){
        BOR_INFO2(err, "  Found Optimal Solution");
        for (int i = 0; i < var_op.getSize(); ++i)
            values[i] = cp.getValue(var_op[i]);
        ret = 0;

    }else{
        switch (cp.getInfo(IloCP::FailStatus)){
            case IloCP::SearchHasNotFailed:
                BOR_INFO2(err, "  Solution not found: search-not-failed");
                break;
            case IloCP::SearchHasFailedNormally:
                BOR_INFO2(err, "  Solution not found: Provably No Solution");
                break;
            case IloCP::SearchStoppedByLimit:
                BOR_INFO2(err, "  Solution not found:"
                               " Terminated by a time or fail limit");
                break;
            case IloCP::SearchStoppedByLabel:
                BOR_INFO2(err, "  Solution not found:"
                               " Terminated -- stopped-by-label");
                break;
            case IloCP::SearchStoppedByExit:
                BOR_INFO2(err, "  Solution not found:"
                               " Terminated by exitSearch()");
                break;
            case IloCP::SearchStoppedByAbort:
                BOR_INFO2(err, "  Solution not found: Aborted");
                break;
            case IloCP::UnknownFailureStatus:
                BOR_INFO2(err, "  Solution not found: unknown failure");
                break;
        }
        ret = -1;
    }

#ifndef NO_LOGGER
    delete logger;
#endif /* NO_LOGGER */

    return ret;
}

static int fdrOpPreConstr(const pddl_fdr_op_t *op,
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
    return 1;
}

static int fdrOpEffConstr(const pddl_fdr_op_t *op,
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
    return 1;
}

static int fdrOpConstr(int op_id,
                       const bor_iset_t *group,
                       const pddl_fdr_t *fdr,
                       IloEnv &env,
                       IloModel &model,
                       IloIntVarArray &fact_var,
                       IloIntVarArray &op_var,
                       bor_err_t *err)
{
    int num = 0;
    const pddl_fdr_op_t *op = fdr->op.op[op_id];
    num += fdrOpPreConstr(op, group, fdr, env, model, fact_var, op_var, err);
    num += fdrOpEffConstr(op, group, fdr, env, model, fact_var, op_var, err);
    return num;
}

void pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                     bor_iset_t *redundant_ops,
                                     bor_err_t *err)
{
    BOR_INFO2(err, "Endomorphism on FDR ...");
    op_groups_t opg;
    opGroupsInitFDR(&opg, fdr);
    BOR_INFO(err, "  Operators grouped into %d groups", opg.group_size);

    IloEnv env;
    IloModel model(env);

    // Create fact variables
    IloIntVarArray var_fact(env, fdr->var.global_id_size);
    for (int fi = 0; fi < fdr->var.global_id_size; ++fi){
        const pddl_fdr_val_t *val = fdr->var.global_id_to_val[fi];
        const pddl_fdr_var_t *var = fdr->var.var + val->var_id;
        char name[128];
        snprintf(name, 128, "%d:(%s)", fi, val->name);
        var_fact[fi] = IloIntVar(env, 0, var->val_size - 1, name);
    }

    // Create operator variables
    IloIntVarArray var_op(env, fdr->op.op_size);
    for (int oi = 0; oi < fdr->op.op_size; ++oi){
        const pddl_fdr_op_t *op = fdr->op.op[oi];
        char name[128];
        snprintf(name, 128, "%d:(%s)", oi, op->name);
        var_op[oi] = IloIntVar(env, 0, fdr->op.op_size - 1, name);
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
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg.group_size; ++group_id){
        int op_id;
        const bor_iset_t *group = &opg.group[group_id];
        BOR_ISET_FOR_EACH(group, op_id){
            num_op_constr += fdrOpConstr(op_id, group, fdr, env, model,
                                         var_fact, var_op, err);
        }
        //BOR_INFO(err, "  Created operator constraints %d",
        //         borISetSize(&opg.group[group_id]));
    }
    BOR_INFO(err, "  Added %d operator constraints", num_op_constr);
    opGroupsFree(&opg);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function min(count-diff())");

    //std::cerr << model << std::endl;

    int *values = BOR_ALLOC_ARR(int, fdr->op.op_size);
    if (solve(model, var_op, values, err) == 0){
        int num_redundant = 0;
        for (int op_id = 0; op_id < fdr->op.op_size; ++op_id){
            int value = values[op_id];
            if (value != op_id){
                if (values[value] == value){
                    if (redundant_ops != NULL)
                        borISetAdd(redundant_ops, op_id);
                    ++num_redundant;
                }
#ifdef DEBUG_PRINT_OP_MAPPING
                BOR_INFO(err, "    :: op %d -> %d :: (%s) -> (%s)",
                         op_id, value,
                         fdr->op.op[op_id]->name,
                         fdr->op.op[value]->name);
#endif /* DEBUG_PRINT_OP_MAPPING */
            }
        }
        BOR_INFO(err, "  Found %d redundant operators", num_redundant);
    }
    BOR_FREE(values);
    model.end();
}




static void mgStripsInit(mg_strips_t *mgs, const pddl_mg_strips_t *mg_strips)
{
    mgs->strips = &mg_strips->strips;
    mgs->mgroup = &mg_strips->mg;

    mgs->fact_size = mgs->strips->fact.fact_size;
    mgs->fact_to_mgroup = BOR_CALLOC_ARR(bor_iset_t, mgs->fact_size);
    for (int mgi = 0; mgi < mgs->mgroup->mgroup_size; ++mgi){
        int fact_id;
        BOR_ISET_FOR_EACH(&mgs->mgroup->mgroup[mgi].mgroup, fact_id)
            borISetAdd(mgs->fact_to_mgroup + fact_id, mgi);
    }

    mgs->op_size = mgs->strips->op.op_size;
    mgs->op = BOR_CALLOC_ARR(mg_strips_op_t, mgs->op_size);
    for (int op_id = 0; op_id < mgs->strips->op.op_size; ++op_id){
        const pddl_strips_op_t *sop = mgs->strips->op.op[op_id];
        mg_strips_op_t *op = mgs->op + op_id;
        op->cost = sop->cost;
        int fact_id;
        BOR_ISET_FOR_EACH(&sop->pre, fact_id){
            int mgroup_id;
            BOR_ISET_FOR_EACH(&mgs->fact_to_mgroup[fact_id], mgroup_id){
                ASSERT(!pddlFDRPartStateIsSet(&op->pre, mgroup_id));
                pddlFDRPartStateSet(&op->pre, mgroup_id, fact_id);
            }
        }

        BOR_ISET_FOR_EACH(&sop->add_eff, fact_id){
            int mgroup_id;
            BOR_ISET_FOR_EACH(&mgs->fact_to_mgroup[fact_id], mgroup_id){
                ASSERT(!pddlFDRPartStateIsSet(&op->eff, mgroup_id));
                pddlFDRPartStateSet(&op->eff, mgroup_id, fact_id);
            }
        }

#ifdef PDDL_DEBUG
        BOR_ISET_FOR_EACH(&sop->del_eff, fact_id){
            int mgroup_id;
            BOR_ISET_FOR_EACH(&mgs->fact_to_mgroup[fact_id], mgroup_id)
                ASSERT(pddlFDRPartStateIsSet(&op->eff, mgroup_id));
        }
#endif /* PDDL_DEBUG */
    }

    mgs->fact_to_cvar = BOR_CALLOC_ARR(int, mgs->fact_size);
    mgs->fact_identity = BOR_CALLOC_ARR(int, mgs->fact_size);
    mgs->op_identity = BOR_CALLOC_ARR(int, mgs->op_size);
}

static void mgStripsFree(mg_strips_t *mgs)
{
    for (int fi = 0; fi < mgs->fact_size; ++fi)
        borISetFree(mgs->fact_to_mgroup + fi);
    BOR_FREE(mgs->fact_to_mgroup);

    for (int op_id = 0; op_id < mgs->op_size; ++op_id){
        pddlFDRPartStateFree(&mgs->op[op_id].pre);
        pddlFDRPartStateFree(&mgs->op[op_id].eff);
    }
    BOR_FREE(mgs->op);

    BOR_FREE(mgs->fact_to_cvar);
    BOR_FREE(mgs->fact_identity);
    BOR_FREE(mgs->op_identity);
}

static int mgStripsPartStateIsIdentity(const mg_strips_t *mgs,
                                       const pddl_fdr_part_state_t *p)
{
    for (int fi = 0; fi < p->fact_size; ++fi){
        if (!mgs->fact_identity[p->fact[fi].val])
            return 0;
    }
    return 1;
}

static void mgStripsFindIdentity(mg_strips_t *mgs, const op_groups_t *opg)
{
    int fact_id;
    BOR_ISET_FOR_EACH(&mgs->strips->init, fact_id)
        mgs->fact_identity[fact_id] = 1;
    BOR_ISET_FOR_EACH(&mgs->strips->goal, fact_id)
        mgs->fact_identity[fact_id] = 1;

    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        const bor_iset_t *group = &opg->group[group_id];
        if (borISetSize(group) != 1)
            continue;

        int op_id = borISetGet(group, 0);
        mgs->op_identity[op_id] = 1;

        const mg_strips_op_t *op = mgs->op + op_id;
        for (int fi = 0; fi < op->pre.fact_size; ++fi)
            mgs->fact_identity[op->pre.fact[fi].val] = 1;
        for (int fi = 0; fi < op->eff.fact_size; ++fi)
            mgs->fact_identity[op->eff.fact[fi].val] = 1;
    }

    for (int op_id = 0; op_id < mgs->op_size; ++op_id){
        const mg_strips_op_t *op = mgs->op + op_id;
        if (mgStripsPartStateIsIdentity(mgs, &op->pre)
                && mgStripsPartStateIsIdentity(mgs, &op->eff)){
            mgs->op_identity[op_id] = 1;
        }
    }
}

static int mgStripsNumIdentityFacts(const mg_strips_t *mgs)
{
    int cnt = 0;
    for (int fi = 0; fi < mgs->fact_size; ++fi)
        cnt += mgs->fact_identity[fi];
    return cnt;
}

static int mgStripsNumIdentityOps(const mg_strips_t *mgs)
{
    int cnt = 0;
    for (int oi = 0; oi < mgs->op_size; ++oi)
        cnt += mgs->op_identity[oi];
    return cnt;
}

static void mgStripsPrepareCVars(mg_strips_t *mgs)
{
    int vid = 0;
    for (int fi = 0; fi < mgs->fact_size; ++fi){
        if (mgs->fact_identity[fi]){
            mgs->fact_to_cvar[fi] = -1;
        }else{
            mgs->fact_to_cvar[fi] = vid++;
        }
    }
    mgs->cvar_fact_size = vid;

    mgs->non_identity_cvar_op_size = 0;
    for (int oi = 0; oi < mgs->op_size; ++oi){
        if (!mgs->op_identity[oi])
            mgs->non_identity_cvar_op_size++;
    }
}

static int mgStripsOpPreConstr(int op_id,
                               const bor_iset_t *group,
                               const mg_strips_t *mgs,
                               IloEnv &env,
                               IloModel &model,
                               IloIntVarArray &fact_var,
                               IloIntVarArray &op_var,
                               bor_err_t *err)
{
    const mg_strips_op_t *op = mgs->op + op_id;
    int other_op_id;

    int pre_size = 1;
    for (int fi = 0; fi < op->pre.fact_size; ++fi){
        if (!mgs->fact_identity[op->pre.fact[fi].val])
            ++pre_size;
    }

    IloIntTupleSet pre(env, pre_size);
    BOR_ISET_FOR_EACH(group, other_op_id){
        const mg_strips_op_t *other_op = mgs->op + other_op_id;
        if (other_op->cost > op->cost)
            continue;
        IloIntArray vals(env, pre_size);
        vals[0] = other_op_id;

        int skip = 0;
        int idx = 1;
        for (int fi = 0; fi < other_op->pre.fact_size; ++fi){
            ASSERT(other_op->pre.fact[fi].var == op->pre.fact[fi].var);
            if (mgs->fact_identity[op->pre.fact[fi].val]){
                if (op->pre.fact[fi].val != other_op->pre.fact[fi].val){
                    skip = 1;
                    break;
                }
            }else{
                vals[idx++] = other_op->pre.fact[fi].val;
            }
        }
        if (!skip){
            ASSERT(idx == pre_size);
            pre.add(vals);
        }
    }

    if (pre.getCardinality() == 0)
        return 0;

    IloIntVarArray pre_var(env, pre_size);
    pre_var[0] = op_var[op_id];
    int idx = 1;
    for (int fi = 0; fi < op->pre.fact_size; ++fi){
        if (!mgs->fact_identity[op->pre.fact[fi].val])
            pre_var[idx++] = fact_var[mgs->fact_to_cvar[op->pre.fact[fi].val]];
    }

    model.add(IloAllowedAssignments(env, pre_var, pre));
    return 1;
}

static int mgStripsOpEffConstr(int op_id,
                                const bor_iset_t *group,
                                const mg_strips_t *mgs,
                                IloEnv &env,
                                IloModel &model,
                                IloIntVarArray &fact_var,
                                IloIntVarArray &op_var,
                                bor_err_t *err)
{
    const mg_strips_op_t *op = mgs->op + op_id;
    int other_op_id;

    int eff_size = 1;
    for (int fi = 0; fi < op->eff.fact_size; ++fi){
        if (!mgs->fact_identity[op->eff.fact[fi].val])
            ++eff_size;
    }

    IloIntTupleSet eff(env, eff_size);
    BOR_ISET_FOR_EACH(group, other_op_id){
        const mg_strips_op_t *other_op = mgs->op + other_op_id;
        if (other_op->cost > op->cost)
            continue;
        IloIntArray vals(env, eff_size);
        vals[0] = other_op_id;

        int skip = 0;
        int idx = 1;
        for (int fi = 0; fi < other_op->eff.fact_size; ++fi){
            ASSERT(other_op->eff.fact[fi].var == op->eff.fact[fi].var);
            if (mgs->fact_identity[op->eff.fact[fi].val]){
                if (op->eff.fact[fi].val != other_op->eff.fact[fi].val){
                    skip = 1;
                    break;
                }
            }else{
                vals[idx++] = other_op->eff.fact[fi].val;
            }
        }
        if (!skip){
            ASSERT(idx == eff_size);
            eff.add(vals);
        }
    }

    if (eff.getCardinality() == 0)
        return 0;

    IloIntVarArray eff_var(env, eff_size);
    eff_var[0] = op_var[op_id];
    int idx = 1;
    for (int fi = 0; fi < op->eff.fact_size; ++fi){
        if (!mgs->fact_identity[op->eff.fact[fi].val])
            eff_var[idx++] = fact_var[mgs->fact_to_cvar[op->eff.fact[fi].val]];
    }

    model.add(IloAllowedAssignments(env, eff_var, eff));
    return 1;
}

static int mgStripsOpConstr(int op_id,
                            const bor_iset_t *group,
                            const mg_strips_t *mgs,
                            IloEnv &env,
                            IloModel &model,
                            IloIntVarArray &fact_var,
                            IloIntVarArray &op_var,
                            bor_err_t *err)
{
    if (mgs->op_identity[op_id]){
        model.add(op_var[op_id] == op_id);
        return 1;
    }

    int num = 0;
    num += mgStripsOpPreConstr(op_id, group, mgs, env, model,
                               fact_var, op_var, err);
    num += mgStripsOpEffConstr(op_id, group, mgs, env, model,
                               fact_var, op_var, err);
    ASSERT_RUNTIME(num == 0 || num == 2);
    return num;
}

void pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                          bor_iset_t *redundant_ops,
                                          bor_err_t *err)
{
    BOR_INFO2(err, "Endomorphism on MG-Strips ...");
    mg_strips_t mgs;
    mgStripsInit(&mgs, mg_strips);

    op_groups_t opg;
    opGroupsInitMGStrips(&opg, &mgs);
    BOR_INFO(err, "  Operators grouped into %d groups", opg.group_size);

    mgStripsFindIdentity(&mgs, &opg);
    BOR_INFO(err, "  Found %d/%d identity facts, %d/%d identity ops",
             mgStripsNumIdentityFacts(&mgs), mgs.fact_size,
             mgStripsNumIdentityOps(&mgs), mgs.op_size);
    if (mgStripsNumIdentityOps(&mgs) == mgs.op_size){
        BOR_INFO2(err, "  All operators are identity");
        BOR_INFO2(err, "  Found 0 redundant operators");
        opGroupsFree(&opg);
        mgStripsFree(&mgs);
        return;
    }

    mgStripsPrepareCVars(&mgs);
    BOR_INFO(err, "  CSP needs %d fact and %d non-identity operator variables",
             mgs.cvar_fact_size, mgs.non_identity_cvar_op_size);

    IloEnv env;
    IloModel model(env);

    // Create fact variables
    IloIntVarArray var_fact(env, mgs.cvar_fact_size);
    for (int fi = 0, vi = 0; fi < mgs.fact_size; ++fi){
        if (mgs.fact_to_cvar[fi] < 0)
            continue;
        char name[128];
        snprintf(name, 128, "%d:(%s)", fi, mgs.strips->fact.fact[fi]->name);
        var_fact[vi++] = IloIntVar(env, 0, mgs.fact_size - 1, name);
        //var_fact[vi++] = IloIntVar(env, 0, mgs.fact_size - 1);
    }

    // Create operator variables
    IloIntVarArray var_op(env, mgs.op_size);
    for (int oi = 0; oi < mgs.op_size; ++oi){
        char name[128];
        snprintf(name, 128, "%d:(%s)", oi, mgs.strips->op.op[oi]->name);
        var_op[oi] = IloIntVar(env, 0, mgs.op_size - 1, name);
        //var_op[vi++] = IloIntVar(env, 0, mgs.op_size - 1);
    }
    BOR_INFO(err, "  Created %d fact and %d operator variables",
             (int)var_fact.getSize(), (int)var_op.getSize());

    // Set operator constraints
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg.group_size; ++group_id){
        int op_id;
        const bor_iset_t *group = &opg.group[group_id];
        BOR_ISET_FOR_EACH(group, op_id){
            num_op_constr += mgStripsOpConstr(op_id, group, &mgs, env, model,
                                              var_fact, var_op, err);
        }
        //BOR_INFO(err, "  Created operator constraints %d",
        //         borISetSize(&opg.group[group_id]));
    }
    BOR_INFO(err, "  Added %d operator constraints", num_op_constr);
    opGroupsFree(&opg);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function min(count-diff())");

    //std::cerr << model << std::endl;

    int *values = BOR_ALLOC_ARR(int, mgs.op_size);
    if (solve(model, var_op, values, err) == 0){
        int num_redundant = 0;
        for (int op_id = 0; op_id < mgs.op_size; ++op_id){
            int value = values[op_id];
            ASSERT(value >= 0 && value < mgs.op_size);
            if (value != op_id){
                if (values[value] == value){
                    if (redundant_ops != NULL)
                            borISetAdd(redundant_ops, op_id);
                    ++num_redundant;
                }
#ifdef DEBUG_PRINT_OP_MAPPING
                BOR_INFO(err, "    :: op %d -> %d :: (%s) -> (%s)",
                         op_id, value,
                         mgs.strips->op.op[op_id]->name,
                         mgs.strips->op.op[value]->name);
#endif /* DEBUG_PRINT_OP_MAPPING */
            }
        }
        BOR_INFO(err, "  Found %d redundant operators", num_redundant);
    }
    BOR_FREE(values);
    model.end();
    mgStripsFree(&mgs);
}

#else /* PDDL_CPOPTIMIZER */
void pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                     bor_iset_t *redundant_ops,
                                     bor_err_t *err)
{
    BOR_FATAL2("Missing CPOPTIMIZER");
}

void pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                          bor_iset_t *redundant_ops,
                                          bor_err_t *err)
{
    BOR_FATAL2("Missing CPOPTIMIZER");
}
#endif /* PDDL_CPOPTIMIZER */
