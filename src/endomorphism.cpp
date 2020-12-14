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

#define DEBUG_PRINT_OP_MAPPING

#include "pddl/config.h"
#include "pddl/endomorphism.h"

#ifdef PDDL_CPOPTIMIZER
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#define IL_STD
#include <ilcp/cp.h>
#include <ilcplex/cpxconst.h>

#include <boruvka/alloc.h>
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include <boruvka/sort.h>
#include "pddl/set.h"
#include "pddl/time_limit.h"
#include "pddl/pddl_struct.h"
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
            /*
            BOR_INFO(err, "    cpoptimizer: mem: %ldMB, solutions: %d",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions));
            */

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
    key = borFastHash_32(v->pre.s, v->pre.size, 13);
    key <<= 32;
    key |= (uint64_t)borFastHash_32(v->eff.s, v->eff.size, 13);
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

static int extractSolution(IloCP &cp,
                           IloIntVarArray &var_op,
                           bor_iset_t *redundant_op,
                           bor_err_t *err)
{
    std::vector<char> mapped_to(var_op.getSize(), 0);

    BOR_ISET(redundant);
    for (int i = 0; i < var_op.getSize(); ++i)
        mapped_to[cp.getValue(var_op[i])] = 1;

    for (int i = 0; i < var_op.getSize(); ++i){
        if (!mapped_to[i]){
            borISetAdd(&redundant, i);
#ifdef DEBUG_PRINT_OP_MAPPING
            BOR_INFO(err, "    :: op %d -> %d", i, cp.getValue(var_op[i]));
#endif /* DEBUG_PRINT_OP_MAPPING */
        }
    }
    int num_redundant = borISetSize(&redundant);

    if (redundant_op != NULL
            && borISetSize(&redundant) > borISetSize(redundant_op)){
        borISetEmpty(redundant_op);
        borISetUnion(redundant_op, &redundant);
    }
    borISetFree(&redundant);
    return num_redundant;
}

static int solve(IloModel &model,
                 IloIntVarArray &var_op,
                 const pddl_endomorphism_config_t *cfg,
                 float max_search_time,
                 bor_iset_t *redundant_op,
                 const char *name,
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
    cp.setParameter(IloCP::Workers, cfg->num_threads);
    cp.setParameter(IloCP::TimeLimit, max_search_time);

    BOR_INFO2(err, "  Solving model ...");
    cp.startNewSearch();
    int num = -1;
    int max_num = -1;
    while (cp.next()){
        num = extractSolution(cp, var_op, redundant_op, err);
        max_num = BOR_MAX(max_num, num);
        BOR_INFO(err, "  Found a solution with %d redundant %s", num, name);
    }

    switch (cp.getInfo(IloCP::FailStatus)){
        case IloCP::SearchHasNotFailed:
        case IloCP::SearchHasFailedNormally:
            if (num < 0){
                BOR_INFO2(err, "  Provably No Solution");
            }else{
                BOR_INFO2(err, "  Optimal Solution Found");
            }
            break;
        case IloCP::SearchStoppedByLimit:
            BOR_INFO2(err, "  Terminated by a time or fail limit");
            break;
        case IloCP::SearchStoppedByLabel:
            BOR_INFO2(err, "  Terminated -- stopped-by-label");
            break;
        case IloCP::SearchStoppedByExit:
            BOR_INFO2(err, "  Terminated by exitSearch()");
            break;
        case IloCP::SearchStoppedByAbort:
            BOR_INFO2(err, "  Aborted");
            break;
        case IloCP::UnknownFailureStatus:
            BOR_INFO2(err, "  Unknown failure");
            break;
    }

    if (num >= 0){
        BOR_INFO(err, "  Found %d redundant %s", max_num, name);
        ret = 0;
    }else{
        BOR_INFO2(err, "  Solution not found");
        ret = -1;
    }

    cp.endSearch();
    cp.end();
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

static int fdrInference(const pddl_fdr_t *fdr,
                        const pddl_endomorphism_config_t *cfg,
                        const op_groups_t *opg,
                        IloEnv &env,
                        IloModel &model,
                        bor_iset_t *redundant_ops,
                        pddl_time_limit_t *time_limit,
                        bor_err_t *err)
{
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

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

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

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Set operator constraints
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;

        int op_id;
        const bor_iset_t *group = &opg->group[group_id];
        BOR_ISET_FOR_EACH(group, op_id){
            if (pddlTimeLimitCheck(time_limit) != 0)
                return -1;

            num_op_constr += fdrOpConstr(op_id, group, fdr, env, model,
                                         var_fact, var_op, err);
        }
        //BOR_INFO(err, "  Created operator constraints %d",
        //         borISetSize(&opg.group[group_id]));
    }
    BOR_INFO(err, "  Added %d operator constraints", num_op_constr);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function min(count-diff())");

    //std::cerr << model << std::endl;

    float max_search_time = pddlTimeLimitRemain(time_limit);
    max_search_time = BOR_MIN(max_search_time, cfg->max_search_time);
    solve(model, var_op, cfg, max_search_time, redundant_ops,
          "operators", err);
    return 0;
}

static int fdrRedundantOps(const pddl_fdr_t *fdr,
                           const pddl_endomorphism_config_t *cfg,
                           bor_iset_t *redundant_ops,
                           bor_err_t *err)
{
    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    if (cfg->max_time > 0.)
        pddlTimeLimitSet(&time_limit, cfg->max_time);

    int ret = 0;
    BOR_INFO(err, "Endomorphism on FDR (facts: %d, ops: %d) ...",
             fdr->var.global_id_size, fdr->op.op_size);
    op_groups_t opg;
    opGroupsInitFDR(&opg, fdr);
    BOR_INFO(err, "  Operators grouped into %d groups", opg.group_size);

    IloEnv env;
    IloModel model(env);

    try {
        int rinf = fdrInference(fdr, cfg, &opg, env, model, redundant_ops,
                                &time_limit, err);
        if (rinf < 0){
            BOR_INFO2(err, "  Time limit reached");
            BOR_INFO2(err, "  Terminating inference of endomorphism");
            BOR_INFO2(err, "  Terminated by a time limit");
            ret = -1;
        }
    } catch(IloMemoryException &e){
        BOR_INFO2(err, "  Not Enough Memory");
        BOR_INFO2(err, "  Terminating inference of endomorphism");
        ret = -1;
    }

    env.end();
    opGroupsFree(&opg);
    return ret;
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

static int mgStripsInference(const pddl_mg_strips_t *mg_strips,
                             const pddl_endomorphism_config_t *cfg,
                             const mg_strips_t *mgs,
                             const op_groups_t *opg,
                             IloEnv &env,
                             IloModel &model,
                             bor_iset_t *redundant_ops,
                             pddl_time_limit_t *time_limit,
                             bor_err_t *err)
{
    // Create fact variables
    IloIntVarArray var_fact(env, mgs->cvar_fact_size);
    for (int fi = 0, vi = 0; fi < mgs->fact_size; ++fi){
        if (mgs->fact_to_cvar[fi] < 0)
            continue;
        char name[128];
        snprintf(name, 128, "%d:(%s)", fi, mgs->strips->fact.fact[fi]->name);
        var_fact[vi++] = IloIntVar(env, 0, mgs->fact_size - 1, name);
        //var_fact[vi++] = IloIntVar(env, 0, mgs->fact_size - 1);
    }

    // Create operator variables
    IloIntVarArray var_op(env, mgs->op_size);
    for (int oi = 0; oi < mgs->op_size; ++oi){
        char name[128];
        snprintf(name, 128, "%d:(%s)", oi, mgs->strips->op.op[oi]->name);
        var_op[oi] = IloIntVar(env, 0, mgs->op_size - 1, name);
        //var_op[vi++] = IloIntVar(env, 0, mgs->op_size - 1);
    }
    BOR_INFO(err, "  Created %d fact and %d operator variables",
             (int)var_fact.getSize(), (int)var_op.getSize());

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Set operator constraints
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;

        int op_id;
        const bor_iset_t *group = &opg->group[group_id];
        BOR_ISET_FOR_EACH(group, op_id){
            num_op_constr += mgStripsOpConstr(op_id, group, mgs, env, model,
                                              var_fact, var_op, err);
        }
        //BOR_INFO(err, "  Created operator constraints %d",
        //         borISetSize(&opg.group[group_id]));
    }
    BOR_INFO(err, "  Added %d operator constraints", num_op_constr);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function min(count-diff())");

    //std::cerr << model << std::endl;

    float max_search_time = pddlTimeLimitRemain(time_limit);
    max_search_time = BOR_MIN(max_search_time, cfg->max_search_time);
    solve(model, var_op, cfg, max_search_time, redundant_ops,
          "operators", err);
    return 0;
}

static int mgStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                const pddl_endomorphism_config_t *cfg,
                                bor_iset_t *redundant_ops,
                                bor_err_t *err)
{
    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    if (cfg->max_time > 0.)
        pddlTimeLimitSet(&time_limit, cfg->max_time);

    int ret = 0;
    BOR_INFO(err, "Endomorphism on MG-Strips (facts: %d, ops: %d)...",
             mg_strips->strips.fact.fact_size,
             mg_strips->strips.op.op_size);
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
        return 0;
    }

    mgStripsPrepareCVars(&mgs);
    BOR_INFO(err, "  CSP needs %d fact and %d non-identity operator variables",
             mgs.cvar_fact_size, mgs.non_identity_cvar_op_size);

    IloEnv env;
    IloModel model(env);

    try {
        int rinf = mgStripsInference(mg_strips, cfg, &mgs, &opg, env, model,
                                     redundant_ops, &time_limit, err);
        if (rinf < 0){
            BOR_INFO2(err, "  Time limit reached");
            BOR_INFO2(err, "  Terminating inference of endomorphism");
            BOR_INFO2(err, "  Terminated by a time limit");
            ret = -1;
        }
    } catch(IloMemoryException &e){
        BOR_INFO2(err, "  Not Enough Memory");
        BOR_INFO2(err, "  Terminating inference of endomorphism");
        ret = -1;
    }

    env.end();
    opGroupsFree(&opg);
    mgStripsFree(&mgs);
    return ret;
}



struct label_groups {
    bor_iset_t init_loop;
    bor_iset_t loop;
    bor_iset_t init_to;
    bor_iset_t init_from;
    bor_iset_t goal_to;
    bor_iset_t goal_from;
    bor_iset_t goal;
};
typedef struct label_groups label_groups_t;

struct ts_presolve {
    std::vector<std::vector<bool>> op_allow;
    std::vector<bool> op_identity;
    std::vector<std::vector<std::vector<bool>>> state_allow;
};
typedef struct ts_presolve ts_presolve_t;

static bool cmpISetSize(const bor_iset_t *a, const bor_iset_t *b)
{
    return borISetSize(a) < borISetSize(b);
}

static void presolveTRStateAllow(bor_iset_t *state_allow,
                                 const ts_presolve_t *presolve,
                                 const pddl_trans_systems_t *tss,
                                 int tsi,
                                 int from,
                                 int label,
                                 int to)
{
    const pddl_trans_system_t *ts = tss->ts[tsi];
    int olabel, ofrom, oto;
    int from_is_goal = borISetIn(from, &ts->goal_states);
    int to_is_goal = borISetIn(to, &ts->goal_states);
    int label_cost = tss->label.label[label].cost;
    const std::vector<bool> &op_allow = presolve->op_allow[label];
    BOR_ISET(from_allow);
    BOR_ISET(to_allow);
    PDDL_LABELED_TRANSITIONS_SET_FOR_EACH(&ts->trans, ofrom, olabel, oto){
        if (!op_allow[olabel])
            continue;
        if (tss->label.label[olabel].cost > label_cost)
            continue;
        if (from == ts->init_state && ofrom != from)
            continue;
        if (to == ts->init_state && oto != to)
            continue;
        if (from == to && ofrom != oto)
            continue;
        if (from_is_goal && !borISetIn(ofrom, &ts->goal_states))
            continue;
        if (to_is_goal && !borISetIn(oto, &ts->goal_states))
            continue;
        if (!borISetIn(ofrom, state_allow + from))
            continue;
        if (!borISetIn(oto, state_allow + to))
            continue;
        borISetAdd(&from_allow, ofrom);
        borISetAdd(&to_allow, oto);
    }
    borISetIntersect(state_allow + from, &from_allow);
    borISetIntersect(state_allow + to, &to_allow);
    borISetFree(&from_allow);
    borISetFree(&to_allow);
}

static void stateAllowFree(bor_iset_t *state_allow, int num_states)
{
    for (int si = 0; si < num_states; ++si)
        borISetFree(state_allow + si);
    BOR_FREE(state_allow);
}

static int presolveStateAllow(ts_presolve_t *presolve,
                               const pddl_trans_systems_t *tss,
                               int tsi,
                               pddl_time_limit_t *time_limit)
{
    const pddl_trans_system_t *ts = tss->ts[tsi];

    bor_iset_t *state_allow = BOR_CALLOC_ARR(bor_iset_t, ts->num_states);
    for (int si = 0; si < ts->num_states; ++si){
        if (pddlTimeLimitCheck(time_limit) != 0){
            stateAllowFree(state_allow, ts->num_states);
            return -1;
        }

        if (si == ts->init_state){
            borISetAdd(state_allow + si, si);

        }else if (borISetIn(si, &ts->goal_states)){
            int si2;
            BOR_ISET_FOR_EACH(&ts->goal_states, si2)
                borISetAdd(state_allow + si, si2);

        }else{
            for (int si2 = 0; si2 < ts->num_states; ++si2)
                borISetAdd(state_allow + si, si2);
        }
    }

    int label_id, from, to;
    PDDL_LABELED_TRANSITIONS_SET_FOR_EACH(&ts->trans, from, label_id, to){
        presolveTRStateAllow(state_allow, presolve,
                             tss, tsi, from, label_id, to);
    }
    for (int si = 0; si < ts->num_states; ++si){
        int state;
        BOR_ISET_FOR_EACH(state_allow + si, state)
            presolve->state_allow[tsi][si][state] = true;
    }
    stateAllowFree(state_allow, ts->num_states);
    return 0;
}

static void labelGroupsFree(const pddl_trans_systems_t *tss,
                            label_groups_t *label_group)
{
    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        borISetFree(&label_group[tsi].init_loop);
        borISetFree(&label_group[tsi].loop);
        borISetFree(&label_group[tsi].init_to);
        borISetFree(&label_group[tsi].init_from);
        borISetFree(&label_group[tsi].goal_to);
        borISetFree(&label_group[tsi].goal_from);
        borISetFree(&label_group[tsi].goal);
    }
    BOR_FREE(label_group);
}

static int tsPresolve(ts_presolve_t *presolve,
                      const pddl_trans_systems_t *tss,
                      pddl_time_limit_t *time_limit,
                      bor_err_t *err)
{
    presolve->op_identity.resize(tss->label.label_size, false);
    presolve->op_allow.resize(tss->label.label_size);
    presolve->state_allow.resize(tss->ts_size);
    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;
        int num_states = tss->ts[tsi]->num_states;
        presolve->state_allow[tsi].resize(num_states);
        for (int si = 0; si < tss->ts[tsi]->num_states; ++si)
            presolve->state_allow[tsi][si].resize(num_states, false);
    }

    label_groups_t *label_group;
    label_group = BOR_CALLOC_ARR(label_groups_t, tss->ts_size);

    std::vector<std::vector<bor_iset_t *>> relevant(tss->label.label_size);
    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        if (pddlTimeLimitCheck(time_limit) != 0){
            labelGroupsFree(tss, label_group);
            return -1;
        }
        const pddl_trans_system_t *ts = tss->ts[tsi];
        int consider_goal = (borISetSize(&ts->goal_states) != ts->num_states);
        const pddl_label_set_t *labels;
        int from, to;
        PDDL_LABELED_TRANSITIONS_SET_FOR_EACH_LABEL_SET(&ts->trans,
                                                        from, labels, to){
            if (from == to){
                if (from == ts->init_state)
                    borISetUnion(&label_group[tsi].init_loop, &labels->label);
                borISetUnion(&label_group[tsi].loop, &labels->label);
            }

            int from_init = (from == ts->init_state);
            int to_init = (to == ts->init_state);
            if (from_init)
                borISetUnion(&label_group[tsi].init_from, &labels->label);
            if (to_init)
                borISetUnion(&label_group[tsi].init_to, &labels->label);

            if (consider_goal){
                int from_goal = borISetIn(from, &ts->goal_states);
                int to_goal = borISetIn(to, &ts->goal_states);
                if (from_goal)
                    borISetUnion(&label_group[tsi].goal_from, &labels->label);
                if (to_goal)
                    borISetUnion(&label_group[tsi].goal_to, &labels->label);
                if (from_goal && to_goal)
                    borISetUnion(&label_group[tsi].goal, &labels->label);
            }
        }
        int op;
        BOR_ISET_FOR_EACH(&label_group[tsi].init_loop, op)
            relevant[op].push_back(&label_group[tsi].init_loop);
        BOR_ISET_FOR_EACH(&label_group[tsi].loop, op)
            relevant[op].push_back(&label_group[tsi].loop);
        BOR_ISET_FOR_EACH(&label_group[tsi].init_to, op)
            relevant[op].push_back(&label_group[tsi].init_to);
        BOR_ISET_FOR_EACH(&label_group[tsi].init_from, op)
            relevant[op].push_back(&label_group[tsi].init_from);
        BOR_ISET_FOR_EACH(&label_group[tsi].goal_to, op)
            relevant[op].push_back(&label_group[tsi].goal_to);
        BOR_ISET_FOR_EACH(&label_group[tsi].goal_from, op)
            relevant[op].push_back(&label_group[tsi].goal_from);
        BOR_ISET_FOR_EACH(&label_group[tsi].goal, op)
            relevant[op].push_back(&label_group[tsi].goal);
    }

    for (int op_id = 0; op_id < tss->label.label_size; ++op_id){
        if (pddlTimeLimitCheck(time_limit) != 0){
            labelGroupsFree(tss, label_group);
            return -1;
        }

        if (relevant[op_id].size() == 0){
            presolve->op_allow[op_id].resize(tss->label.label_size, true);
            continue;
        }

        std::sort(relevant[op_id].begin(), relevant[op_id].end(), cmpISetSize);

        BOR_ISET(allowed);
        borISetUnion(&allowed, relevant[op_id][0]);
        for (size_t i = 1; i < relevant[op_id].size(); ++i)
            borISetIntersect(&allowed, relevant[op_id][i]);

        BOR_ISET(allowed2);
        int op_cost = tss->label.label[op_id].cost;
        int other_op;
        BOR_ISET_FOR_EACH(&allowed, other_op){
            if (tss->label.label[other_op].cost <= op_cost)
                borISetAdd(&allowed2, other_op);
        }
        borISetFree(&allowed);

        if (borISetSize(&allowed2) <= 1){
            presolve->op_identity[op_id] = true;
            presolve->op_allow[op_id].resize(tss->label.label_size, false);
            presolve->op_allow[op_id][op_id] = true;
            //fprintf(stderr, "O %d identity\n", op_id);
        }else{
            presolve->op_allow[op_id].resize(tss->label.label_size, false);
            BOR_ISET_FOR_EACH(&allowed2, other_op)
                presolve->op_allow[op_id][other_op] = true;
            /*
            fprintf(stderr, "O %d: ", op_id);
            pddlISetPrint(&allowed2, stderr);
            fprintf(stderr, "\n");
            */
        }
        borISetFree(&allowed2);
    }

    labelGroupsFree(tss, label_group);
    BOR_INFO2(err, "    presolve restriction of operator domains done");

    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        if (presolveStateAllow(presolve, tss, tsi, time_limit) != 0)
            return -1;
    }
    BOR_INFO2(err, "    presolve restriction of state domains done");

    return 0;
}

static int transConstraints(const pddl_trans_systems_t *tss,
                            int tsi,
                            const ts_presolve_t *presolve,
                            int from,
                            int label,
                            int to,
                            IloEnv &env,
                            IloModel &model,
                            IloIntVarArray &var_state,
                            int var_state_offset,
                            IloIntVarArray &var_op,
                            bor_err_t *err)
{
    const pddl_trans_system_t *ts = tss->ts[tsi];

    IloIntVarArray var(env, 3);
    var[0] = var_op[label];
    var[1] = var_state[var_state_offset + from];
    var[2] = var_state[var_state_offset + to];

    IloIntTupleSet val(env, 3);
    int olabel, ofrom, oto;
    const std::vector<bool> &op_allow = presolve->op_allow[label];
    PDDL_LABELED_TRANSITIONS_SET_FOR_EACH(&ts->trans, ofrom, olabel, oto){
        if (!op_allow[olabel]
                || !presolve->state_allow[tsi][from][ofrom]
                || !presolve->state_allow[tsi][to][oto]){
            continue;
        }
        if (from == to && ofrom != oto)
            continue;
        ASSERT(from != ts->init_state || ofrom == from);
        ASSERT(to != ts->init_state || oto == to);
        val.add(IloIntArray(env, 3, olabel, ofrom, oto));
    }
    if (val.getCardinality() == 0){
        BOR_INFO(err, "Could not find mapping for (%d)->%d->(%d)",
                 from, label, to);
        return 0;
    }

    model.add(IloAllowedAssignments(env, var, val));
    return 1;
}

static int tsConstraints(const pddl_trans_systems_t *tss,
                         const ts_presolve_t *presolve,
                         int tsi,
                         IloEnv &env,
                         IloModel &model,
                         IloIntVarArray &var_state,
                         int var_state_offset,
                         IloIntVarArray &var_op,
                         pddl_time_limit_t *time_limit,
                         bor_err_t *err)
{
    int num_constrs = 0;
    const pddl_trans_system_t *ts = tss->ts[tsi];

    // Add init constraint
    ASSERT(ts->init_state >= 0);
    model.add(var_state[var_state_offset + ts->init_state] == ts->init_state);
    num_constrs += 1;

    // Goal constraints
    int goal_state;
    IloIntArray goal_val(env);
    BOR_ISET_FOR_EACH(&ts->goal_states, goal_state)
        goal_val.add(goal_state);
    BOR_ISET_FOR_EACH(&ts->goal_states, goal_state){
        IloIntVar &var = var_state[var_state_offset + goal_state];
        model.add(IloAllowedAssignments(env, var, goal_val));
        num_constrs += 1;
    }

    // Transition constraints
    int label_id, from, to;
    PDDL_LABELED_TRANSITIONS_SET_FOR_EACH(&ts->trans, from, label_id, to){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;
        num_constrs += transConstraints(tss, tsi, presolve,
                                        from, label_id, to, env, model,
                                        var_state, var_state_offset,
                                        var_op, err);
    }
    return num_constrs;
}

static int tsInference(const pddl_trans_systems_t *tss,
                       const pddl_endomorphism_config_t *cfg,
                       const ts_presolve_t *presolve,
                       IloEnv &env,
                       IloModel &model,
                       bor_iset_t *redundant_ops,
                       pddl_time_limit_t *time_limit,
                       bor_err_t *err)
{
    // Create state variables
    std::vector<int> var_state_offset(tss->ts_size);
    int num_states = 0;
    for (int tsi = 0; tsi < tss->ts_size; ++tsi)
        num_states += tss->ts[tsi]->num_states;

    IloIntVarArray var_state(env, num_states);
    for (int tsi = 0, sid = 0; tsi < tss->ts_size; ++tsi){
        int ts_num_states = tss->ts[tsi]->num_states;
        var_state_offset[tsi] = sid;
        for (int i = 0; i < ts_num_states; ++i){
            char name[128];
            snprintf(name, 128, "%d:%d:%d", tsi, i, sid);
            var_state[sid++] = IloIntVar(env, 0, ts_num_states - 1, name);
        }
    }

    // Create operator variables
    IloIntVarArray var_op(env, tss->label.label_size);
    for (int li = 0; li < tss->label.label_size; ++li){
        char name[128];
        snprintf(name, 128, "O%d", li);
        var_op[li] = IloIntVar(env, 0, tss->label.label_size - 1, name);
        //var_op[vi++] = IloIntVar(env, 0, mgs.op_size - 1);
    }
    BOR_INFO(err, "  Created %d state and %d operator variables",
             (int)var_state.getSize(), (int)var_op.getSize());

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Operator identity constraints
    int num_ident = 0;
    for (int op_id = 0; op_id < tss->label.label_size; ++op_id){
        if (presolve->op_identity[op_id]){
            var_op[op_id].setBounds(op_id, op_id);
            ++num_ident;
        }
    }
    BOR_INFO(err, "  Set %d operator-identity bounds", num_ident);

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    int num_constrs = 0;
    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;

        int num = tsConstraints(tss, presolve, tsi, env, model,
                                var_state, var_state_offset[tsi],
                                var_op, time_limit, err);
        if (num < 0)
            return -1;
        BOR_INFO(err, "  Added %d constraints for TS %d with %d states",
                 num, tsi, tss->ts[tsi]->num_states);
        num_constrs += num;
    }
    BOR_INFO(err, "  Added %d constraints overall", num_constrs);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function");

    //std::cerr << model << std::endl;

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    float max_search_time = pddlTimeLimitRemain(time_limit);
    max_search_time = BOR_MIN(max_search_time, cfg->max_search_time);
    solve(model, var_op, cfg, max_search_time, redundant_ops,
          "operators", err);
    return 0;
}

static int transSystemRedundantOps(const pddl_trans_systems_t *tss,
                                   const pddl_endomorphism_config_t *cfg,
                                   bor_iset_t *redundant_ops,
                                   bor_err_t *err)
{
    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    if (cfg->max_time > 0.)
        pddlTimeLimitSet(&time_limit, cfg->max_time);

    int ret = 0;
    BOR_INFO(err, "Endomorphism on factored TS"
                  " (num-ts: %d, num-labels: %d) ...",
             tss->ts_size, tss->label.label_size);

    ts_presolve_t presolve;
    BOR_INFO2(err, "  Running presolve...");
    if (tsPresolve(&presolve, tss, &time_limit, err) != 0){
        BOR_INFO2(err, "  Time limit reached");
        BOR_INFO2(err, "  Terminating presolve phase");
        BOR_INFO2(err, "  Terminating inference of endomorphism");
        BOR_INFO2(err, "  Terminated by a time limit");
        return -1;
    }

    int num_identity = 0;
    for (size_t i = 0; i < presolve.op_identity.size(); ++i)
        num_identity += int(presolve.op_identity[i]);
    BOR_INFO(err, "  Presolve found %d identity operators", num_identity);

    if (num_identity == tss->label.label_size){
        BOR_INFO2(err, "  All operators are identity");
        BOR_INFO2(err, "  Found 0 redundant operators");
        return 0;
    }

    IloEnv env;
    IloModel model(env);

    try {
        int rinf = tsInference(tss, cfg, &presolve, env, model, redundant_ops,
                               &time_limit, err);
        if (rinf < 0){
            BOR_INFO2(err, "  Time limit reached");
            BOR_INFO2(err, "  Terminating inference of endomorphism");
            BOR_INFO2(err, "  Terminated by a time limit");
            ret = -1;
        }
    }catch (IloMemoryException &e){
        BOR_INFO2(err, "  Not Enough Memory");
        BOR_INFO2(err, "  Terminating inference of endomorphism");
        ret = -1;
    }

    env.end();
    return ret;
}

static int runInSubprocess(const pddl_fdr_t *fdr,
                           const pddl_mg_strips_t *mg_strips,
                           const pddl_trans_systems_t *tss,
                           const pddl_endomorphism_config_t *cfg,
                           bor_iset_t *redundant_ops,
                           bor_err_t *err)
{
    BOR_INFO2(err, "Endomorphism in a subprocess ...");
    fflush(stdout);
    fflush(stderr);
    fflush(err->warn_out);
    fflush(err->info_out);

    int op_size = 0;
    if (fdr != NULL){
        op_size = fdr->op.op_size;
    }else if (mg_strips != NULL){
        op_size = mg_strips->strips.op.op_size;
    }else if (tss != NULL){
        op_size = tss->label.label_size;
    }

    size_t shared_size = sizeof(int) + (sizeof(char) * op_size);
    void *shared = mmap(NULL, shared_size, PROT_WRITE | PROT_READ,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED){
        //perror("mmap() failed");
        BOR_INFO(err, "Could not allocate shared memory of size %ld using"
                      " mmap: %s",
                 (long)shared_size, strerror(errno));
        return -1;
    }
    bzero(shared, shared_size);
    int *shared_ret = (int *)shared;
    char *shared_ops = (char *)(shared_ret + 1);
    *shared_ret = -1;
    BOR_INFO(err, "  Allocated %ld bytes of shared memory", (long)shared_size);

    int pid = fork();
    if (pid == -1){
        perror("fork() failed");
        return -1;

    }else if (pid == 0){
        int ret = 0;
        if (fdr != NULL){
            ret = fdrRedundantOps(fdr, cfg, redundant_ops, err);
        }else if (mg_strips != NULL){
            ret = mgStripsRedundantOps(mg_strips, cfg, redundant_ops, err);
        }else if (tss != NULL){
            ret = transSystemRedundantOps(tss, cfg, redundant_ops, err);
        }

        *shared_ret = ret;
        if (ret == 0){
            int op;
            BOR_ISET_FOR_EACH(redundant_ops, op)
                shared_ops[op] = 1;
        }
        exit(ret);

    }else{
        wait(NULL);
        int ret = *shared_ret;
        if (ret == 0){
            for (int op_id = 0; op_id < op_size; ++op_id){
                if (shared_ops[op_id])
                    borISetAdd(redundant_ops, op_id);
            }
        }
        munmap(shared, shared_size);
        BOR_INFO(err, "Endomorphism in a subprocess: ret: %d,"
                      " redundant ops: %d",
                 ret, borISetSize(redundant_ops));
        BOR_INFO2(err, "Endomorphism in a subprocess DONE");
        return ret;
    }
}

int pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                    const pddl_endomorphism_config_t *cfg,
                                    bor_iset_t *redundant_ops,
                                    bor_err_t *err)
{
    if (cfg->ignore_costs)
        BOR_ERR_RET2(err, -1, "ignore_cost option is not supported");
    if (cfg->run_in_subprocess)
        return runInSubprocess(fdr, NULL, NULL, cfg, redundant_ops, err);
    return fdrRedundantOps(fdr, cfg, redundant_ops, err);
}

int pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                         const pddl_endomorphism_config_t *cfg,
                                         bor_iset_t *redundant_ops,
                                         bor_err_t *err)
{
    if (cfg->ignore_costs)
        BOR_ERR_RET2(err, -1, "ignore_cost option is not supported");
    if (cfg->run_in_subprocess)
        return runInSubprocess(NULL, mg_strips, NULL, cfg, redundant_ops, err);
    return mgStripsRedundantOps(mg_strips, cfg, redundant_ops, err);
}

int pddlEndomorphismTransSystemRedundantOps(const pddl_trans_systems_t *tss,
                                            const pddl_endomorphism_config_t *cfg,
                                            bor_iset_t *redundant_ops,
                                            bor_err_t *err)
{
    if (cfg->ignore_costs)
        BOR_ERR_RET2(err, -1, "ignore_cost option is not supported");
    if (cfg->run_in_subprocess)
        return runInSubprocess(NULL, NULL, tss, cfg, redundant_ops, err);
    return transSystemRedundantOps(tss, cfg, redundant_ops, err);
}




struct lifted_endomorphism {
    int obj_size;
    int *obj_is_fixed;
};
typedef struct lifted_endomorphism lifted_endomorphism_t;

static void setAtomTypeFixed(lifted_endomorphism_t *end,
                             const pddl_t *pddl,
                             const pddl_params_t *params,
                             const pddl_cond_atom_t *atom,
                             int parami)
{
    if (atom->arg[parami].param >= 0){
        int param = atom->arg[parami].param;
        int type_id = params->param[param].type;
        const pddl_type_t *type = pddl->type.type + type_id;
        for (int i = 0; i < type->obj.obj_size; ++i)
            end->obj_is_fixed[type->obj.obj[i]] = 1;

    }else{
        end->obj_is_fixed[atom->arg[parami].obj] = 1;
    }
}

static void setAtomTypesFixed(lifted_endomorphism_t *end,
                              const pddl_t *pddl,
                              const pddl_params_t *params,
                              const pddl_cond_atom_t *atom)
{
    for (int i = 0; i < atom->arg_size; ++i)
        setAtomTypeFixed(end, pddl, params, atom, i);
}

static int hasAtom(const pddl_cond_t *cond,
                   const pddl_cond_atom_t *atom)
{
    if (cond->type == PDDL_COND_ATOM){
        return pddlCondEq(cond, &atom->cls);
    }else{
        ASSERT_RUNTIME(cond->type == PDDL_COND_AND);
        const pddl_cond_part_t *cand = PDDL_COND_CAST(cond, part);
        bor_list_t *item;
        BOR_LIST_FOR_EACH(&cand->part, item){
            const pddl_cond_t *c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
            ASSERT_RUNTIME(c->type == PDDL_COND_ATOM);
            if (pddlCondEq(c, &atom->cls))
                return 1;
        }
    }
    return 0;
}

static int isCovered(const pddl_t *pddl,
                     const pddl_params_t *atom_param,
                     const pddl_cond_atom_t *atom,
                     const pddl_params_t *ma_param,
                     const pddl_cond_atom_t *ma)
{
    for (int argi = 0; argi < atom->arg_size; ++argi){
        if (ma->arg[argi].param >= 0 && atom->arg[argi].param >= 0){
            int a_parami = atom->arg[argi].param;
            int a_type = atom_param->param[a_parami].type;
            int m_parami = ma->arg[argi].param;
            int m_type = ma_param->param[m_parami].type;
            //if (pddlTypesAreDisjunct(&pddl->type, m_type, a_type))
            if (!pddlTypesIsParent(&pddl->type, a_type, m_type))
                return 0;

        }else if (ma->arg[argi].param >= 0){
            int a_obj = atom->arg[argi].obj;
            int m_parami = ma->arg[argi].param;
            int m_type = ma_param->param[m_parami].type;
            if (!pddlTypesObjHasType(&pddl->type, m_type, a_obj))
                return 0;

        }else if (atom->arg[argi].param >= 0){
            int m_obj = ma->arg[argi].obj;
            int a_parami = atom->arg[argi].param;
            int a_type = atom_param->param[a_parami].type;
            if (!pddlTypesObjHasType(&pddl->type, a_type, m_obj)
                    || pddlTypeNumObjs(&pddl->type, a_type) > 1){
                return 0;
            }

        }else{
            if (atom->arg[argi].obj != ma->arg[argi].obj)
                return 0;
        }
    }

    return 1;
}

static int coverAtomWithMGroup(int *counted,
                               const pddl_t *pddl,
                               const pddl_params_t *act_param,
                               const pddl_cond_atom_t *atom,
                               const pddl_lifted_mgroup_t *mgroup)
{
    int covered = 0;

    for (int ci = 0; ci < mgroup->cond.size; ++ci){
        const pddl_cond_t *mc = mgroup->cond.cond[ci];
        const pddl_cond_atom_t *ma = PDDL_COND_CAST(mc, atom);
        if (ma->pred != atom->pred)
            continue;

        if (!isCovered(pddl, act_param, atom, &mgroup->param, ma))
            continue;

        covered = 1;
        for (int argi = 0; argi < atom->arg_size; ++argi){
            if (ma->arg[argi].param >= 0 && atom->arg[argi].param >= 0){
                int a_parami = atom->arg[argi].param;
                int a_type = act_param->param[a_parami].type;
                const pddl_obj_id_t *a_obj;
                int a_obj_size;
                a_obj = pddlTypesObjsByType(&pddl->type, a_type, &a_obj_size);

                int m_parami = ma->arg[argi].param;
#ifdef PDDL_DEBUG
                int m_type = mgroup->param.param[m_parami].type;
#endif /* PDDL_DEBUG */
                int is_counted = mgroup->param.param[m_parami].is_counted_var;

                for (int i = 0; i < a_obj_size; ++i){
                    ASSERT(pddlTypesObjHasType(&pddl->type, m_type, a_obj[i]));
                    if (!is_counted){
                        counted[a_obj[i]] = -1;
                    }else if (counted[a_obj[i]] == 0){
                        counted[a_obj[i]] = 1;
                    }
                }

            }else if (ma->arg[argi].param >= 0){
                counted[atom->arg[argi].obj] = -1;

            }else if (atom->arg[argi].param >= 0){
                counted[ma->arg[argi].obj] = -1;

            }else{
                counted[atom->arg[argi].obj] = -1;
            }
        }
    }

    return covered;
}

static void coverAtomWithMGroups(lifted_endomorphism_t *end,
                                 const pddl_t *pddl,
                                 const pddl_params_t *act_param,
                                 const pddl_cond_atom_t *atom,
                                 const pddl_lifted_mgroups_t *mgroups)
{
    int *counted = BOR_CALLOC_ARR(int, pddl->obj.obj_size);
    int covered = 0;
    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        covered |= coverAtomWithMGroup(counted, pddl, act_param, atom,
                                       mgroups->mgroup + mgi);
    }

    if (!covered){
        setAtomTypesFixed(end, pddl, act_param, atom);
        BOR_FREE(counted);
        return;
    }

    for (int argi = 0; argi < atom->arg_size; ++argi){
        if (atom->arg[argi].param >= 0){
            int a_parami = atom->arg[argi].param;
            int a_type = act_param->param[a_parami].type;
            const pddl_obj_id_t *a_obj;
            int a_obj_size;
            a_obj = pddlTypesObjsByType(&pddl->type, a_type, &a_obj_size);
            for (int i = 0; i < a_obj_size; ++i){
                if (counted[a_obj[i]] < 0)
                    end->obj_is_fixed[a_obj[i]] = 1;
            }

        }else{
            if (counted[atom->arg[argi].obj] < 0)
                end->obj_is_fixed[atom->arg[argi].obj] = 1;
        }
    }

    BOR_FREE(counted);
}

static void analyzeActionAtom(
                lifted_endomorphism_t *end,
                const pddl_t *pddl,
                const pddl_params_t *act_param,
                const pddl_cond_t *act_pre,
                const pddl_cond_atom_t *eff_atom,
                const pddl_lifted_mgroups_t *lifted_mgroups,
                const pddl_endomorphism_config_t *cfg,
                bor_err_t *err)
{
    if (!eff_atom->neg)
        return;

    pddl_cond_t *pos_c = pddlCondClone(&eff_atom->cls);
    pddl_cond_atom_t *pos_a = PDDL_COND_CAST(pos_c, atom);
    pos_a->neg = 0;
    if (hasAtom(act_pre, pos_a)){
        coverAtomWithMGroups(end, pddl, act_param, eff_atom, lifted_mgroups);

    }else{
        setAtomTypesFixed(end, pddl, act_param, eff_atom);
    }
    pddlCondDel(pos_c);
}

static void fixAtomConstants(pddl_cond_atom_t *a, lifted_endomorphism_t *end)
{
    for (int i = 0; i < a->arg_size; ++i){
        if (a->arg[i].obj >= 0)
            end->obj_is_fixed[a->arg[i].obj] = 1;
    }
}

static int fixConstants(pddl_cond_t *c, void *_end)
{
    lifted_endomorphism_t *end = (lifted_endomorphism_t *)_end;
    if (c->type == PDDL_COND_ATOM){
        pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
        fixAtomConstants(a, end);

    }else if (c->type == PDDL_COND_ASSIGN){
        pddl_cond_func_op_t *a = PDDL_COND_CAST(c, func_op);
        if (a->lvalue != NULL)
            fixAtomConstants(a->lvalue, end);
        if (a->fvalue != NULL)
            fixAtomConstants(a->fvalue, end);
    }

    return 0;
}

static void liftedEndomorphismAnalyzeAction(
                lifted_endomorphism_t *end,
                const pddl_t *pddl,
                const pddl_params_t *act_param,
                const pddl_cond_t *act_pre,
                const pddl_cond_t *act_eff,
                const pddl_lifted_mgroups_t *lifted_mgroups,
                const pddl_endomorphism_config_t *cfg,
                bor_err_t *err)
{
    pddlCondTraverse((pddl_cond_t *)act_pre, NULL, fixConstants, end);
    pddlCondTraverse((pddl_cond_t *)act_eff, NULL, fixConstants, end);
    if (act_eff->type == PDDL_COND_ATOM){
        const pddl_cond_atom_t *a = PDDL_COND_CAST(act_eff, atom);
        analyzeActionAtom(end, pddl, act_param, act_pre, a,
                          lifted_mgroups, cfg, err);
        return;
    }

    ASSERT_RUNTIME(act_eff->type == PDDL_COND_AND);
    const pddl_cond_part_t *cand = PDDL_COND_CAST(act_eff, part);
    bor_list_t *item;
    BOR_LIST_FOR_EACH(&cand->part, item){
        const pddl_cond_t *c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type == PDDL_COND_ATOM){
            const pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
            analyzeActionAtom(end, pddl, act_param, act_pre, a,
                              lifted_mgroups, cfg, err);

        }else if (c->type == PDDL_COND_INCREASE){
            // We can ignore this, because it is already handled in
            // the initial state

        }else if (c->type == PDDL_COND_WHEN){
            const pddl_cond_when_t *w = PDDL_COND_CAST(c, when);
            liftedEndomorphismAnalyzeAction(end, pddl, act_param,
                                            w->pre, w->eff, lifted_mgroups,
                                            cfg, err);

        }else{
            BOR_FATAL("Unexpected atom of type %d:%s\n",
                      c->type, pddlCondTypeName(c->type));
        }
    }
}

static int _liftedEndomorphismFixGoal(pddl_cond_t *c, void *u)
{
    lifted_endomorphism_t *end = (lifted_endomorphism_t *)u;
    if (c->type == PDDL_COND_ATOM){
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        for (int i = 0; i < atom->arg_size; ++i){
            ASSERT(atom->arg[i].obj >= 0);
            end->obj_is_fixed[atom->arg[i].obj] = 1;
        }
    }
    return 0;
}

static void liftedEndomorphismInit(lifted_endomorphism_t *end,
                                   const pddl_t *pddl,
                                   const pddl_lifted_mgroups_t *lifted_mgroups,
                                   const pddl_endomorphism_config_t *cfg,
                                   bor_err_t *err)
{
    bzero(end, sizeof(*end));
    end->obj_size = pddl->obj.obj_size;
    end->obj_is_fixed = BOR_CALLOC_ARR(int, end->obj_size);

    // Set objects in the goal as fixed
    pddlCondTraverse((pddl_cond_t *)pddl->goal, NULL,
                     _liftedEndomorphismFixGoal, end);

    for (int ai = 0; ai < pddl->action.action_size; ++ai){
        const pddl_action_t *action = pddl->action.action + ai;
        BOR_INFO(err, "Analyzing action (%s) ...", action->name);
        liftedEndomorphismAnalyzeAction(end, pddl, &action->param,
                                        action->pre, action->eff,
                                        lifted_mgroups, cfg, err);
    }

#ifdef PDDL_DEBUG
    for (int i = 0; i < end->obj_size; ++i){
        BOR_INFO(err, "Obj-fixed %d:(%s): %d",
                 i, pddl->obj.obj[i].name, end->obj_is_fixed[i]);
    }
#endif /* PDDL_DEBUG */
}

static void liftedEndomorphismFree(lifted_endomorphism_t *end)
{
    if (end->obj_is_fixed != NULL)
        BOR_FREE(end->obj_is_fixed);
}

static int liftedEndomorphismNumUnfixed(const lifted_endomorphism_t *end)
{
    int num = 0;
    for (int i = 0; i < end->obj_size; ++i){
        if (!end->obj_is_fixed[i])
            ++num;
    }
    return num;
}

struct obj_tuple {
    int *tuple;
    int value;
};
typedef struct obj_tuple obj_tuple_t;

struct pred_obj_tuple {
    int pred;
    int size;
    int tuple_size;
    int tuple_alloc;
    obj_tuple_t *tuple;
};
typedef struct pred_obj_tuple pred_obj_tuple_t;

struct pred_obj_tuples {
    int pred_size;
    int func_offset;
    pred_obj_tuple_t *tuple;
};
typedef struct pred_obj_tuples pred_obj_tuples_t;

static void predObjTupleFree(pred_obj_tuple_t *tup)
{
    for (int i = 0; i < tup->tuple_size; ++i)
        BOR_FREE(tup->tuple[i].tuple);
    if (tup->tuple != NULL)
        BOR_FREE(tup->tuple);
}

static obj_tuple_t *predObjTupleAdd(pred_obj_tuple_t *tup)
{
    if (tup->tuple_size == tup->tuple_alloc){
        if (tup->tuple_alloc == 0)
            tup->tuple_alloc = 1;
        tup->tuple_alloc *= 2;
        tup->tuple = BOR_REALLOC_ARR(tup->tuple, obj_tuple_t,
                                     tup->tuple_alloc);
    }
    tup->tuple[tup->tuple_size].tuple = BOR_ALLOC_ARR(int, tup->size);
    tup->tuple[tup->tuple_size].value = 0;
    return tup->tuple + tup->tuple_size++;
}

static int cmpTuple(const void *a, const void *b, void *u)
{
    const obj_tuple_t *o1 = (const obj_tuple_t *)a;
    const obj_tuple_t *o2 = (const obj_tuple_t *)b;
    return o2->value - o1->value;
}

static void predObjTupleSort(pred_obj_tuple_t *tup)
{
    borSort(tup->tuple, tup->tuple_size, sizeof(obj_tuple_t),
            cmpTuple, NULL);
}

static void predObjTuplesInit(pred_obj_tuples_t *tup, const pddl_t *pddl)
{
    tup->pred_size = pddl->pred.pred_size + pddl->func.pred_size;
    tup->func_offset = pddl->pred.pred_size;
    tup->tuple = BOR_CALLOC_ARR(pred_obj_tuple_t, tup->pred_size);
    int i;
    for (i = 0; i < pddl->pred.pred_size; ++i){
        tup->tuple[i].pred = i;
        tup->tuple[i].size = pddl->pred.pred[i].param_size;
    }
    for (; i < tup->pred_size; ++i){
        int fi = i - pddl->pred.pred_size;
        tup->tuple[i].pred = i;
        tup->tuple[i].size = pddl->func.pred[fi].param_size;
    }
}

static void predObjTuplesFree(pred_obj_tuples_t *tup)
{
    for (int i = 0; i < tup->pred_size; ++i)
        predObjTupleFree(tup->tuple + i);
    if (tup->tuple != NULL)
        BOR_FREE(tup->tuple);
}

static int _predObjTuplesInitFromCond(pddl_cond_t *c, void *u)
{
    pred_obj_tuples_t *tup = (pred_obj_tuples_t *)u;
    if (c->type == PDDL_COND_ATOM){
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        int pred_id = atom->pred;
        pred_obj_tuple_t *tuple = tup->tuple + pred_id;
        ASSERT(atom->arg_size == tuple->size);
        obj_tuple_t *t = predObjTupleAdd(tuple);
        for (int i = 0; i < atom->arg_size; ++i){
            ASSERT(atom->arg[i].obj >= 0);
            t->tuple[i] = atom->arg[i].obj;
        }

    }else if (c->type != PDDL_COND_AND){
        const pddl_cond_func_op_t *ass = PDDL_COND_CAST(c, func_op);
        ASSERT(ass->fvalue == NULL);
        ASSERT(ass->lvalue != NULL);
        ASSERT(pddlCondAtomIsGrounded(ass->lvalue));
        int pred_id = ass->lvalue->pred + tup->func_offset;
        pred_obj_tuple_t *tuple = tup->tuple + pred_id;
        ASSERT(ass->lvalue->arg_size == tuple->size);
        obj_tuple_t *t = predObjTupleAdd(tuple);
        t->value = ass->value;
        for (int i = 0; i < ass->lvalue->arg_size; ++i){
            ASSERT(ass->lvalue->arg[i].obj >= 0);
            t->tuple[i] = ass->lvalue->arg[i].obj;
        }

    }else if (c->type != PDDL_COND_AND){
        BOR_FATAL("Unexpected atom of type %d:%s\n",
                  c->type, pddlCondTypeName(c->type));
    }
    return 0;
}

static void predObjTuplesInitFromCond(pred_obj_tuples_t *tup,
                                      const pddl_t *pddl,
                                      const pddl_cond_t *cond)
{
    predObjTuplesInit(tup, pddl);
    pddlCondTraverse((pddl_cond_t *)cond, NULL,
                     _predObjTuplesInitFromCond, tup);
}

static void liftedAddDomains(IloEnv &env,
                             IloModel &model,
                             const pddl_t *pddl,
                             const lifted_endomorphism_t *end,
                             IloIntVarArray &csp_var)
{
    ASSERT(pddl->type.type[0].parent < 0);

    // First deal with fixed objects
    for (int obj = 0; obj < pddl->obj.obj_size; ++obj){
        if (!end->obj_is_fixed[obj])
            continue;
        IloIntTupleSet single_value(env, 1);
        IloIntArray vals(env, 1);
        vals[0] = obj;
        single_value.add(vals);
        IloIntVarArray vars(env, 1);
        vars[0] = csp_var[obj];
        model.add(IloAllowedAssignments(env, vars, single_value));
    }

    // And then with unfixed ones
    for (int type = 0; type < pddl->type.type_size; ++type){
        int num_objs = pddl->type.type[type].obj.obj_size;
        IloIntTupleSet obj_values(env, 1);
        for (int i = 0; i < num_objs; ++i){
            int obj = pddl->type.type[type].obj.obj[i];
            IloIntArray vals(env, 1);
            vals[0] = obj;
            obj_values.add(vals);
        }

        for (int i = 0; i < num_objs; ++i){
            int obj = pddl->type.type[type].obj.obj[i];
            if (!end->obj_is_fixed[obj]){
                IloIntVarArray vars(env, 1);
                vars[0] = csp_var[obj];
                model.add(IloAllowedAssignments(env, vars, obj_values));
            }

        }
    }
}

static void liftedAddTupleConstr(IloEnv &env,
                                 IloModel &model,
                                 const pred_obj_tuple_t *tup,
                                 int from,
                                 int to,
                                 IloIntVarArray &csp_var)
{
    IloIntTupleSet obj_values(env, tup->size);
    for (int ti = from; ti < tup->tuple_size; ++ti){
        IloIntArray vals(env, tup->size);
        for(int i = 0; i < tup->size; ++i)
            vals[i] = tup->tuple[ti].tuple[i];
        obj_values.add(vals);
    }

    for (int ti = from; ti < to; ++ti){
        IloIntVarArray vars(env, tup->size);
        for(int i = 0; i < tup->size; ++i)
            vars[i] = csp_var[tup->tuple[ti].tuple[i]];
        model.add(IloAllowedAssignments(env, vars, obj_values));
    }
}

static void liftedAddInitConstr(IloEnv &env,
                                IloModel &model,
                                const pddl_t *pddl,
                                const lifted_endomorphism_t *end,
                                const pddl_endomorphism_config_t *cfg,
                                IloIntVarArray &csp_var)
{
    pred_obj_tuples_t tuples;
    predObjTuplesInitFromCond(&tuples, pddl, &pddl->init->cls);

    if (cfg->ignore_costs){
        for (int pred = 0; pred < tuples.pred_size; ++pred){
            pred_obj_tuple_t *tup = tuples.tuple + pred;
            for (int i = 0; i < tup->tuple_size; ++i)
                tup->tuple[i].value = 0;
        }
    }

    for (int pred = 0; pred < tuples.pred_size; ++pred){
        pred_obj_tuple_t *tup = tuples.tuple + pred;
        if (tup->tuple_size == 0 || tup->size == 0)
            continue;
        predObjTupleSort(tup);

        int from = 0;
        int to = 1;
        for (; to < tup->tuple_size; ++to){
            if (tup->tuple[from].value != tup->tuple[to].value){
                liftedAddTupleConstr(env, model, tup, from, to, csp_var);
                from = to;
            }
        }
        if (from != to)
            liftedAddTupleConstr(env, model, tup, from, to, csp_var);
    }
    predObjTuplesFree(&tuples);
}

/*
static void liftedAddGoalConstr(IloEnv &env,
                                IloModel &model,
                                const pddl_t *pddl,
                                const lifted_endomorphism_t *end,
                                IloIntVarArray &csp_var)
{
    pred_obj_tuples_t tuples;
    predObjTuplesInitFromCond(&tuples, pddl, pddl->goal);
    for (int pred = 0; pred < pddl->pred.pred_size; ++pred){
        if (tuples.tuple[pred].tuple_size == 0)
            continue;
        const pred_obj_tuple_t *tup = tuples.tuple + pred;

        for (int ti = 0; ti < tup->tuple_size; ++ti){
            IloIntTupleSet obj_values(env, tup->size);
            IloIntArray vals(env, tup->size);
            IloIntVarArray vars(env, tup->size);
            for(int i = 0; i < tup->size; ++i){
                vals[i] = tup->tuple[ti].tuple[i];
                vars[i] = csp_var[tup->tuple[ti].tuple[i]];
            }
            obj_values.add(vals);
            model.add(IloAllowedAssignments(env, vars, obj_values));
        }
    }
    predObjTuplesFree(&tuples);
}
*/

static int liftedSolve(const pddl_t *pddl,
                       const lifted_endomorphism_t *end,
                       const pddl_endomorphism_config_t *cfg,
                       float max_search_time,
                       bor_iset_t *redundant_objs,
                       bor_err_t *err)
{
    int ret = 0;
    int obj_size = pddl->obj.obj_size;
    IloEnv env;
    IloModel model(env);

    // Create variables
    IloIntVarArray csp_vars(env, obj_size);
    for (int obj = 0; obj < obj_size; ++obj){
        csp_vars[obj] = IloIntVar(env, 0, obj_size - 1,
                                  pddl->obj.obj[obj].name);
    }

    // Set domains
    liftedAddDomains(env, model, pddl, end, csp_vars);

    // Add constraints on the initial state and the goal
    liftedAddInitConstr(env, model, pddl, end, cfg, csp_vars);
    // Goal constraint is handled by .obj_is_fixed array
    //liftedAddGoalConstr(env, model, pddl, end, csp_vars);

    IloObjective obj = IloMinimize(env, IloCountDifferent(csp_vars));
    model.add(obj);
    BOR_INFO2(err, "  Added objective function min(count-diff())");

    //float max_search_time = pddlTimeLimitRemain(time_limit);
    //max_search_time = BOR_MIN(max_search_time, cfg->max_search_time);
    solve(model, csp_vars, cfg, max_search_time, redundant_objs,
          "objects", err);

    env.end();
    return ret;
}

struct select_mgroups {
    int mgroup_size;
    int *mgroup_used;
    int obj_size;
    int *obj_st;
    pddl_lifted_mgroups_t lifted_mgroups;
    int tried_all;
};
typedef struct select_mgroups select_mgroups_t;

static void selectMGroupsInit(select_mgroups_t *select,
                              const pddl_t *pddl,
                              const pddl_lifted_mgroups_t *lifted_mgroups)
{
    select->mgroup_size = lifted_mgroups->mgroup_size;
    select->mgroup_used = BOR_CALLOC_ARR(int, select->mgroup_size);
    select->obj_size = pddl->obj.obj_size;
    select->obj_st = BOR_CALLOC_ARR(int, select->obj_size);
    pddlLiftedMGroupsInit(&select->lifted_mgroups);
    select->tried_all = 0;
}

static void selectMGroupsFree(select_mgroups_t *select)
{
    BOR_FREE(select->mgroup_used);
    BOR_FREE(select->obj_st);
    pddlLiftedMGroupsFree(&select->lifted_mgroups);
}

static int selectMGroupsAdd(select_mgroups_t *select,
                            const pddl_t *pddl,
                            int mgi,
                            const pddl_lifted_mgroup_t *mgroup)
{
    int *obj_st = BOR_ALLOC_ARR(int, select->obj_size);
    memcpy(obj_st, select->obj_st, sizeof(int) * select->obj_size);

    for (int condi = 0; condi < mgroup->cond.size; ++condi){
        const pddl_cond_t *c = mgroup->cond.cond[condi];
        const pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
        for (int argi = 0; argi < a->arg_size; ++argi){
            if (a->arg[argi].obj >= 0){
                if (obj_st[a->arg[argi].obj] > 0){
                    BOR_FREE(obj_st);
                    return -1;
                }
                obj_st[a->arg[argi].obj] = -1;
            }
        }
    }

    for (int parami = 0; parami < mgroup->param.param_size; ++parami){
        const pddl_param_t *param = mgroup->param.param + parami;
        int type_id = param->type;
        int obj_size;
        const int *objs = pddlTypesObjsByType(&pddl->type, type_id, &obj_size);
        for (int obji = 0; obji < obj_size; ++obji){
            int obj = objs[obji];
            if (param->is_counted_var){
                if (obj_st[obj] < 0){
                    BOR_FREE(obj_st);
                    return -1;
                }
                obj_st[obj] = 1;
            }else{
                if (obj_st[obj] > 0){
                    BOR_FREE(obj_st);
                    return -1;
                }
                obj_st[obj] = -1;
            }
        }
    }
    pddlLiftedMGroupsAdd(&select->lifted_mgroups, mgroup);
    memcpy(select->obj_st, obj_st, sizeof(int) * select->obj_size);
    BOR_FREE(obj_st);
    return 0;
}

static int selectMGroups(select_mgroups_t *select,
                         const pddl_t *pddl,
                         const pddl_lifted_mgroups_t *lifted_mgroups,
                         const pddl_endomorphism_config_t *cfg)
{
    bzero(select->obj_st, sizeof(int) * select->obj_size);

    if (!select->tried_all){
        pddlLiftedMGroupsFree(&select->lifted_mgroups);
        pddlLiftedMGroupsInitCopy(&select->lifted_mgroups, lifted_mgroups);
        select->tried_all = 1;
        return 0;
    }

    if (!cfg->lifted_use_combinations)
        return -1;

    int num_used = 0;
    int start_i;
    do {
        for (start_i = 0;
                start_i < select->mgroup_size && select->mgroup_used[start_i];
                ++start_i);
        if (start_i == select->mgroup_size)
            return -1;

        select->mgroup_used[start_i] = 1;
        num_used = 1;
        pddlLiftedMGroupsFree(&select->lifted_mgroups);
        pddlLiftedMGroupsInit(&select->lifted_mgroups);
        if (selectMGroupsAdd(select, pddl, start_i,
                             &lifted_mgroups->mgroup[start_i]) != 0){
            start_i = -1;
        }
    } while (start_i < 0);

    for (int i = (start_i + 1) % select->mgroup_size; i != start_i;
            i = (i + 1) % select->mgroup_size){
        if (selectMGroupsAdd(select, pddl, i,
                             &lifted_mgroups->mgroup[i]) == 0){
            select->mgroup_used[i] = 1;
            num_used += 1;
        }
    }

    // We skip this because we always make sure that the variant with all
    // selected mutex groups is returned as a first choice
    if (num_used == select->mgroup_size)
        return -1;
    return 0;
}


int pddlEndomorphismLifted(const pddl_t *pddl,
                           const pddl_lifted_mgroups_t *lifted_mgroups_in,
                           const pddl_endomorphism_config_t *cfg,
                           bor_iset_t *redundant_objects,
                           bor_err_t *err)
{
    if (!pddl->normalized)
        BOR_ERR_RET2(err, -1, "PDDL needs to be normalized!");

    BOR_INFO_PREFIX_PUSH(err, "Lifted endomorphism: ");
    if (cfg->run_in_subprocess)
        BOR_INFO2(err, "run_in_subprocess is ignored");

    if (cfg->ignore_costs)
        BOR_INFO2(err, "Ignoring operator costs");

    // Filter out mutex groups without counted variables
    pddl_lifted_mgroups_t lifted_mgroups;
    pddlLiftedMGroupsInit(&lifted_mgroups);
    for (int mgi = 0; mgi < lifted_mgroups_in->mgroup_size; ++mgi){
        const pddl_lifted_mgroup_t *mg = lifted_mgroups_in->mgroup + mgi;
        if (pddlLiftedMGroupNumCountedVars(mg) > 0)
            pddlLiftedMGroupsAdd(&lifted_mgroups, mg);
    }

    // Remove atoms without counted variables
    for (int mgi = 0; mgi < lifted_mgroups.mgroup_size; ++mgi){
        pddl_lifted_mgroup_t *mg = lifted_mgroups.mgroup + mgi;
        pddlLiftedMGroupRemoveFixedAtoms(mg);
    }

    if (lifted_mgroups.mgroup_size == 0){
        BOR_INFO2(err, "No mutex groups so lifted endomorphisms cannot be"
                       " inferred");
        BOR_INFO_PREFIX_POP(err);
        pddlLiftedMGroupsFree(&lifted_mgroups);
        return 0;
    }


    select_mgroups_t select;
    selectMGroupsInit(&select, pddl, &lifted_mgroups);
    while (selectMGroups(&select, pddl, &lifted_mgroups, cfg) == 0){
        BOR_INFO_PREFIX_PUSH(err, "Selected mgroup: ");
        for (int i = 0; i < select.lifted_mgroups.mgroup_size; ++i)
            pddlLiftedMGroupLog(pddl, &select.lifted_mgroups.mgroup[i], err);
        BOR_INFO_PREFIX_POP(err);

        lifted_endomorphism_t end;
        liftedEndomorphismInit(&end, pddl, &select.lifted_mgroups, cfg, err);
        if (liftedEndomorphismNumUnfixed(&end) > 1){
            liftedSolve(pddl, &end, cfg, 1800., redundant_objects, err);
        }else{
            BOR_INFO2(err, "Not enough unfixed objects to try to find"
                           " endomorphisms");
        }
        liftedEndomorphismFree(&end);
    }
    selectMGroupsFree(&select);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    BOR_INFO_PREFIX_POP(err);
    return 0;
}

#else /* PDDL_CPOPTIMIZER */
int pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                    const pddl_endomorphism_config_t *cfg,
                                    bor_iset_t *redundant_ops,
                                    bor_err_t *err)
{
    BOR_FATAL2("Missing CPOPTIMIZER");
    return -1;
}

int pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                         const pddl_endomorphism_config_t *cfg,
                                         bor_iset_t *redundant_ops,
                                         bor_err_t *err)
{
    BOR_FATAL2("Missing CPOPTIMIZER");
    return -1;
}

int pddlEndomorphismTransSystemRedundantOps(const pddl_trans_systems_t *tss,
                                            const pddl_endomorphism_config_t *cfg,
                                            bor_iset_t *redundant_ops,
                                            bor_err_t *err)
{
    BOR_FATAL2("Missing CPOPTIMIZER");
    return -1;
}

#endif /* PDDL_CPOPTIMIZER */
