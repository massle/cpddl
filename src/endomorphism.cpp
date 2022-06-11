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
#include "pddl/cp.h"

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

#include "pddl/hfunc.h"
#include "pddl/sort.h"
#include "pddl/set.h"
#include "pddl/time_limit.h"
#include "pddl/pddl_struct.h"
#include "internal.h"

#if CPX_VERSION_VERSION < 12 || CPX_VERSION_RELEASE < 9
# define NO_LOGGER
#endif

#ifndef NO_LOGGER
class Logger : public IloCP::Callback {
    pddl_err_t *err;

  public:
    Logger(pddl_err_t *err) : err(err){}
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
            PDDL_INFO(err, "    cpoptimizer: mem: %ldMB, solutions: %d",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions));
            */

        }else if (reason == Solution){
            LOG(err, "cpoptimizer: mem: %ldMB, solutions: %d",
                     (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                     (int)cp.getInfo(IloCP::NumberOfSolutions));

        }else if (reason == Proof){
            LOG(err, "cpoptimizer: mem: %ldMB, solutions: %d, proof",
                (long)cp.getInfo(IloCP::MemoryUsage) / (1024L * 1024L),
                (int)cp.getInfo(IloCP::NumberOfSolutions));

        }else if (reason == ObjBound){
            LOG(err, "cpoptimizer: mem: %ldMB, solutions: %d, new bound: %.2f",
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
    pddl_iset_t *fact_to_mgroup;
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
    pddl_iset_t pre;
    pddl_iset_t eff;
    pddl_htable_key_t key;
    pddl_list_t htable;
};
typedef struct pre_eff_vars pre_eff_vars_t;

struct op_groups {
    pddl_iset_t *group;
    int group_size;
    int group_alloc;
    pddl_htable_t *htable;
};
typedef struct op_groups op_groups_t;

static pddl_htable_key_t preEffComputeHash(const pre_eff_vars_t *v)
{
    uint64_t key;
    key = pddlFastHash_32(v->pre.s, v->pre.size, 13);
    key <<= 32;
    key |= (uint64_t)pddlFastHash_32(v->eff.s, v->eff.size, 13);
    return key;
}

static pddl_htable_key_t preEffHash(const pddl_list_t *l, void *_)
{
    const pre_eff_vars_t *v = PDDL_LIST_ENTRY(l, pre_eff_vars_t, htable);
    return v->key;
}

static int preEffEq(const pddl_list_t *l1, const pddl_list_t *l2, void *_)
{
    const pre_eff_vars_t *v1 = PDDL_LIST_ENTRY(l1, pre_eff_vars_t, htable);
    const pre_eff_vars_t *v2 = PDDL_LIST_ENTRY(l2, pre_eff_vars_t, htable);
    return pddlISetEq(&v1->pre, &v2->pre) && pddlISetEq(&v1->eff, &v2->eff);
}

static void assignOpToGroup(op_groups_t *opgs,
                            int op_id,
                            const pddl_fdr_part_state_t *pre,
                            const pddl_fdr_part_state_t *eff)
{
    pre_eff_vars_t *pev = ALLOC(pre_eff_vars_t);
    bzero(pev, sizeof(*pev));
    for (int fi = 0; fi < pre->fact_size; ++fi)
        pddlISetAdd(&pev->pre, pre->fact[fi].var);
    for (int fi = 0; fi < eff->fact_size; ++fi)
        pddlISetAdd(&pev->eff, eff->fact[fi].var);
    pev->key = preEffComputeHash(pev);
    pddlListInit(&pev->htable);

    pddl_list_t *found;
    if ((found = pddlHTableInsertUnique(opgs->htable, &pev->htable)) == NULL){
        if (opgs->group_size == opgs->group_alloc){
            if (opgs->group_alloc == 0)
                opgs->group_alloc = 2;
            opgs->group_alloc *= 2;
            opgs->group = REALLOC_ARR(opgs->group, pddl_iset_t,
                                      opgs->group_alloc);
        }
        int group_id = opgs->group_size++;
        pddl_iset_t *g = opgs->group + group_id;
        pddlISetInit(g);
        pev->group_id = group_id;
        pddlISetAdd(g, op_id);

    }else{
        pev = PDDL_LIST_ENTRY(found, pre_eff_vars_t, htable);
        pddlISetAdd(opgs->group + pev->group_id, op_id);
    }
}

static void opGroupsInitFDR(op_groups_t *opg, const pddl_fdr_t *fdr)
{
    bzero(opg, sizeof(*opg));
    opg->htable = pddlHTableNew(preEffHash, preEffEq, NULL);
    for (int oi = 0; oi < fdr->op.op_size; ++oi){
        const pddl_fdr_op_t *op = fdr->op.op[oi];
        assignOpToGroup(opg, op->id, &op->pre, &op->eff);
    }
}

static void opGroupsInitMGStrips(op_groups_t *opg, const mg_strips_t *mgs)
{
    bzero(opg, sizeof(*opg));
    opg->htable = pddlHTableNew(preEffHash, preEffEq, NULL);
    for (int oi = 0; oi < mgs->op_size; ++oi){
        const mg_strips_op_t *op = mgs->op + oi;
        assignOpToGroup(opg, oi, &op->pre, &op->eff);
    }
}

static void opGroupsFree(op_groups_t *opg)
{
    for (int i = 0; i < opg->group_size; ++i)
        pddlISetFree(opg->group + i);
    if (opg->group != NULL)
        FREE(opg->group);

    pddl_list_t list;
    pddlListInit(&list);
    pddlHTableGather(opg->htable, &list);
    while (!pddlListEmpty(&list)){
        pddl_list_t *item = pddlListNext(&list);
        pddlListDel(item);
        pre_eff_vars_t *v = PDDL_LIST_ENTRY(item, pre_eff_vars_t, htable);
        pddlISetFree(&v->pre);
        pddlISetFree(&v->eff);
        FREE(v);
    }
    pddlHTableDel(opg->htable);
}

static int extractSolution(IloCP &cp,
                           IloIntVarArray &var_op,
                           pddl_iset_t *redundant_op,
                           int *map,
                           pddl_err_t *err)
{
    std::vector<char> mapped_to(var_op.getSize(), 0);

    PDDL_ISET(redundant);
    for (int i = 0; i < var_op.getSize(); ++i)
        mapped_to[cp.getValue(var_op[i])] = 1;

    for (int i = 0; i < var_op.getSize(); ++i){
        if (!mapped_to[i]){
            if (map != NULL)
                map[i] = cp.getValue(var_op[i]);

            pddlISetAdd(&redundant, i);
#ifdef DEBUG_PRINT_OP_MAPPING
            LOG(err, "    :: op %d -> %d", i, cp.getValue(var_op[i]));
#endif /* DEBUG_PRINT_OP_MAPPING */

        }else if (map != NULL){
            // Set identity everywhere else to get rid of symmetries
            map[i] = i;
        }
    }
    int num_redundant = pddlISetSize(&redundant);

    if (redundant_op != NULL
            && pddlISetSize(&redundant) > pddlISetSize(redundant_op)){
        pddlISetEmpty(redundant_op);
        pddlISetUnion(redundant_op, &redundant);
    }
    pddlISetFree(&redundant);
    return num_redundant;
}

static int solve(IloModel &model,
                 IloIntVarArray &var_op,
                 const pddl_endomorphism_config_t *cfg,
                 float max_search_time,
                 pddl_iset_t *redundant_op,
                 int *map,
                 const char *name,
                 pddl_err_t *err)
{
    int ret = 1;
    IloCP cp(model);
    //cp.dumpModel("model.cpo");
#ifndef NO_LOGGER
    Logger *logger = new Logger(err);
    cp.addCallback(logger);
#endif /* NO_LOGGER */
    cp.setParameter(IloCP::LogVerbosity, IloCP::Quiet);
    cp.setParameter(IloCP::Workers, cfg->num_threads);
    cp.setParameter(IloCP::TimeLimit, max_search_time);

    LOG2(err, "Solving model ...");
    cp.startNewSearch();
    int num = -1;
    int max_num = -1;
    while (cp.next()){
        num = extractSolution(cp, var_op, redundant_op, map, err);
        max_num = PDDL_MAX(max_num, num);
        LOG(err, "Found a solution with %d redundant %s", num, name);
    }

    switch (cp.getInfo(IloCP::FailStatus)){
        case IloCP::SearchHasNotFailed:
        case IloCP::SearchHasFailedNormally:
            if (num < 0){
                LOG2(err, "Provably No Solution");
            }else{
                LOG2(err, "Optimal Solution Found");
                ret = 0;
            }
            break;
        case IloCP::SearchStoppedByLimit:
            LOG2(err, "Terminated by a time or fail limit");
            break;
        case IloCP::SearchStoppedByLabel:
            LOG2(err, "Terminated -- stopped-by-label");
            break;
        case IloCP::SearchStoppedByExit:
            LOG2(err, "Terminated by exitSearch()");
            break;
        case IloCP::SearchStoppedByAbort:
            LOG2(err, "Aborted");
            break;
        case IloCP::UnknownFailureStatus:
            LOG2(err, "Unknown failure");
            break;
    }

    if (num >= 0){
        LOG(err, "Found %{num_redundant}d redundant %s", max_num, name);
        ret = 0;
    }else{
        LOG2(err, "Solution not found");
        ret = -1;
    }

    cp.endSearch();
    cp.end();
#ifndef NO_LOGGER
    delete logger;
#endif /* NO_LOGGER */

    return ret;
}

static int fdrOpConstr(const pddl_fdr_t *fdr,
                       const pddl_endomorphism_config_t *cfg,
                       const pddl_fdr_op_t *op,
                       const pddl_iset_t *group,
                       pddl_cp_t *cp,
                       int op_var_offset,
                       pddl_err_t *err)
{
    int pre_size = op->pre.fact_size + 1;
    int eff_size = op->eff.fact_size + 1;
    int *pre_vals = ALLOC_ARR(int, pddlISetSize(group) * pre_size);
    int pre_vals_size = 0;
    int *eff_vals = ALLOC_ARR(int, pddlISetSize(group) * eff_size);
    int eff_vals_size = 0;

    int other_op_id;
    PDDL_ISET_FOR_EACH(group, other_op_id){
        const pddl_fdr_op_t *other_op = fdr->op.op[other_op_id];
        // TODO: ignore costs
        if (!cfg->ignore_costs && other_op->cost > op->cost)
            continue;
        int idx = pre_vals_size * pre_size;
        pre_vals[idx++] = other_op->id;
        for (int fi = 0; fi < other_op->pre.fact_size; ++fi)
            pre_vals[idx++] = other_op->pre.fact[fi].val;
        ++pre_vals_size;

        idx = eff_vals_size * eff_size;
        eff_vals[idx++] = other_op->id;
        for (int fi = 0; fi < other_op->eff.fact_size; ++fi)
            eff_vals[idx++] = other_op->eff.fact[fi].val;
        ++eff_vals_size;
    }

    int *pre_var = ALLOC_ARR(int, pre_size);
    pre_var[0] = op->id + op_var_offset;
    for (int fi = 0; fi < op->pre.fact_size; ++fi){
        int pvar = op->pre.fact[fi].var;
        int pval = op->pre.fact[fi].val;
        pre_var[fi + 1] = fdr->var.var[pvar].val[pval].global_id;
    }

    int *eff_var = ALLOC_ARR(int, eff_size);
    eff_var[0] = op->id + op_var_offset;
    for (int fi = 0; fi < op->eff.fact_size; ++fi){
        int pvar = op->eff.fact[fi].var;
        int pval = op->eff.fact[fi].val;
        eff_var[fi + 1] = fdr->var.var[pvar].val[pval].global_id;
    }

    pddlCPAddConstrIVarAllowed(cp, pre_size, pre_var, pre_vals_size, pre_vals);
    pddlCPAddConstrIVarAllowed(cp, eff_size, eff_var, eff_vals_size, eff_vals);
    FREE(pre_var);
    FREE(eff_var);
    FREE(pre_vals);
    FREE(eff_vals);
    return 2;
}

static int fdrSetModel(const pddl_fdr_t *fdr,
                       const pddl_endomorphism_config_t *cfg,
                       const op_groups_t *opg,
                       pddl_cp_t *cp,
                       pddl_time_limit_t *time_limit,
                       pddl_err_t *err)
{
    // Create fact variables
    for (int fi = 0; fi < fdr->var.global_id_size; ++fi){
        const pddl_fdr_val_t *val = fdr->var.global_id_to_val[fi];
        const pddl_fdr_var_t *var = fdr->var.var + val->var_id;
        char name[128];
        snprintf(name, 128, "%d:(%s)", fi, val->name);
        int id = pddlCPAddIVar(cp, 0, var->val_size - 1, name);
        ASSERT_RUNTIME(id == fi);
    }
    LOG(err, "Created %{num_fact_vars}d fact variables",
        fdr->var.global_id_size);
    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Create operator variables
    int op_var_offset = fdr->var.global_id_size;
    for (int oi = 0; oi < fdr->op.op_size; ++oi){
        const pddl_fdr_op_t *op = fdr->op.op[oi];
        char name[128];
        snprintf(name, 128, "%d:(%s)", oi, op->name);
        int id = pddlCPAddIVar(cp, 0, fdr->op.op_size - 1, name);
        ASSERT_RUNTIME(op->id + op_var_offset == id);
    }
    LOG(err, "Created %{num_op_vars}d operator variables", fdr->op.op_size);
    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Set init constraint
    for (int vi = 0; vi < fdr->var.var_size; ++vi){
        int fact_id = fdr->var.var[vi].val[fdr->init[vi]].global_id;
        pddlCPAddConstrIVarEq(cp, fact_id, fdr->init[vi]);
    }

    // Set goal constraint
    for (int fi = 0; fi < fdr->goal.fact_size; ++fi){
        int var = fdr->goal.fact[fi].var;
        int val = fdr->goal.fact[fi].val;
        int fact_id = fdr->var.var[var].val[val].global_id;
        pddlCPAddConstrIVarEq(cp, fact_id, val);
    }
    LOG2(err, "Added init and goal constraints");
    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Set operator constraints
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;

        int op_id;
        const pddl_iset_t *group = &opg->group[group_id];
        PDDL_ISET_FOR_EACH(group, op_id){
            if (pddlTimeLimitCheck(time_limit) != 0)
                return -1;

            const pddl_fdr_op_t *op = fdr->op.op[op_id];
            num_op_constr += fdrOpConstr(fdr, cfg, op, group, cp,
                                         op_var_offset, err);
        }
        //PDDL_INFO(err, "  Created operator constraints %d",
        //         pddlISetSize(&opg.group[group_id]));
    }
    LOG(err, "Added %{num_op_constrs}d operator constraints", num_op_constr);

    PDDL_ISET(op_vars);
    for (int oi = 0; oi < fdr->op.op_size; ++oi)
        pddlISetAdd(&op_vars, oi + op_var_offset);
    pddlCPSetObjectiveMinCountDiff(cp, &op_vars);
    pddlISetFree(&op_vars);
    LOG2(err, "  Added objective function min(count-diff())");
    return 0;
}

static int fdrOpPreConstr(const pddl_fdr_op_t *op,
                          const pddl_iset_t *group,
                          const pddl_fdr_t *fdr,
                          IloEnv &env,
                          IloModel &model,
                          IloIntVarArray &fact_var,
                          IloIntVarArray &op_var,
                          pddl_err_t *err)
{
    int other_op_id;

    int pre_size = op->pre.fact_size + 1;
    IloIntTupleSet pre(env, pre_size);
    PDDL_ISET_FOR_EACH(group, other_op_id){
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
                          const pddl_iset_t *group,
                          const pddl_fdr_t *fdr,
                          IloEnv &env,
                          IloModel &model,
                          IloIntVarArray &fact_var,
                          IloIntVarArray &op_var,
                          pddl_err_t *err)
{
    int other_op_id;

    int eff_size = op->eff.fact_size + 1;
    IloIntTupleSet eff(env, eff_size);
    PDDL_ISET_FOR_EACH(group, other_op_id){
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
                       const pddl_iset_t *group,
                       const pddl_fdr_t *fdr,
                       IloEnv &env,
                       IloModel &model,
                       IloIntVarArray &fact_var,
                       IloIntVarArray &op_var,
                       pddl_err_t *err)
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
                        pddl_iset_t *redundant_ops,
                        pddl_time_limit_t *time_limit,
                        pddl_err_t *err)
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
    LOG(err, "Created %{num_fact_vars}d fact and %{num_op_vars}d operator variables",
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
    LOG2(err, "Added init and goal constraints");

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Set operator constraints
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;

        int op_id;
        const pddl_iset_t *group = &opg->group[group_id];
        PDDL_ISET_FOR_EACH(group, op_id){
            if (pddlTimeLimitCheck(time_limit) != 0)
                return -1;

            num_op_constr += fdrOpConstr(op_id, group, fdr, env, model,
                                         var_fact, var_op, err);
        }
        //PDDL_INFO(err, "  Created operator constraints %d",
        //         pddlISetSize(&opg.group[group_id]));
    }
    LOG(err, "Added %{num_op_constrs}d operator constraints", num_op_constr);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    LOG2(err, "  Added objective function min(count-diff())");

    //std::cerr << model << std::endl;

    float max_search_time = pddlTimeLimitRemain(time_limit);
    max_search_time = PDDL_MIN(max_search_time, cfg->max_search_time);
    return solve(model, var_op, cfg, max_search_time, redundant_ops, NULL,
                 "operators", err);
}

static int fdrRedundantOps(const pddl_fdr_t *fdr,
                           const pddl_endomorphism_config_t *cfg,
                           pddl_iset_t *redundant_ops,
                           pddl_err_t *err)
{
    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    if (cfg->max_time > 0.)
        pddlTimeLimitSet(&time_limit, cfg->max_time);

    int ret = 0;
    LOG(err, "Endomorphism on FDR (facts: %d, ops: %d) ...",
        fdr->var.global_id_size, fdr->op.op_size);
    op_groups_t opg;
    opGroupsInitFDR(&opg, fdr);
    LOG(err, "Operators grouped into %d groups", opg.group_size);

    IloEnv env;
    IloModel model(env);

    try {
        int rinf = fdrInference(fdr, cfg, &opg, env, model, redundant_ops,
                                &time_limit, err);
        if (rinf < 0){
            LOG2(err, "Time limit reached");
            LOG2(err, "Terminating inference of endomorphism");
            LOG2(err, "Terminated by a time limit");
            ret = -1;
        }else{
            ret = rinf;
        }
    } catch(IloMemoryException &e){
        LOG2(err, "Not Enough Memory");
        LOG2(err, "Terminating inference of endomorphism");
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
    mgs->fact_to_mgroup = CALLOC_ARR(pddl_iset_t, mgs->fact_size);
    for (int mgi = 0; mgi < mgs->mgroup->mgroup_size; ++mgi){
        int fact_id;
        PDDL_ISET_FOR_EACH(&mgs->mgroup->mgroup[mgi].mgroup, fact_id)
            pddlISetAdd(mgs->fact_to_mgroup + fact_id, mgi);
    }

    mgs->op_size = mgs->strips->op.op_size;
    mgs->op = CALLOC_ARR(mg_strips_op_t, mgs->op_size);
    for (int op_id = 0; op_id < mgs->strips->op.op_size; ++op_id){
        const pddl_strips_op_t *sop = mgs->strips->op.op[op_id];
        mg_strips_op_t *op = mgs->op + op_id;
        op->cost = sop->cost;
        int fact_id;
        PDDL_ISET_FOR_EACH(&sop->pre, fact_id){
            int mgroup_id;
            PDDL_ISET_FOR_EACH(&mgs->fact_to_mgroup[fact_id], mgroup_id){
                ASSERT(!pddlFDRPartStateIsSet(&op->pre, mgroup_id));
                pddlFDRPartStateSet(&op->pre, mgroup_id, fact_id);
            }
        }

        PDDL_ISET_FOR_EACH(&sop->add_eff, fact_id){
            int mgroup_id;
            PDDL_ISET_FOR_EACH(&mgs->fact_to_mgroup[fact_id], mgroup_id){
                ASSERT(!pddlFDRPartStateIsSet(&op->eff, mgroup_id));
                pddlFDRPartStateSet(&op->eff, mgroup_id, fact_id);
            }
        }

#ifdef PDDL_DEBUG
        PDDL_ISET_FOR_EACH(&sop->del_eff, fact_id){
            int mgroup_id;
            PDDL_ISET_FOR_EACH(&mgs->fact_to_mgroup[fact_id], mgroup_id)
                ASSERT(pddlFDRPartStateIsSet(&op->eff, mgroup_id));
        }
#endif /* PDDL_DEBUG */
    }

    mgs->fact_to_cvar = CALLOC_ARR(int, mgs->fact_size);
    mgs->fact_identity = CALLOC_ARR(int, mgs->fact_size);
    mgs->op_identity = CALLOC_ARR(int, mgs->op_size);
}

static void mgStripsFree(mg_strips_t *mgs)
{
    for (int fi = 0; fi < mgs->fact_size; ++fi)
        pddlISetFree(mgs->fact_to_mgroup + fi);
    FREE(mgs->fact_to_mgroup);

    for (int op_id = 0; op_id < mgs->op_size; ++op_id){
        pddlFDRPartStateFree(&mgs->op[op_id].pre);
        pddlFDRPartStateFree(&mgs->op[op_id].eff);
    }
    FREE(mgs->op);

    FREE(mgs->fact_to_cvar);
    FREE(mgs->fact_identity);
    FREE(mgs->op_identity);
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
    PDDL_ISET_FOR_EACH(&mgs->strips->init, fact_id)
        mgs->fact_identity[fact_id] = 1;
    PDDL_ISET_FOR_EACH(&mgs->strips->goal, fact_id)
        mgs->fact_identity[fact_id] = 1;

    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        const pddl_iset_t *group = &opg->group[group_id];
        if (pddlISetSize(group) != 1)
            continue;

        int op_id = pddlISetGet(group, 0);
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
                               const pddl_iset_t *group,
                               const mg_strips_t *mgs,
                               IloEnv &env,
                               IloModel &model,
                               IloIntVarArray &fact_var,
                               IloIntVarArray &op_var,
                               pddl_err_t *err)
{
    const mg_strips_op_t *op = mgs->op + op_id;
    int other_op_id;

    int pre_size = 1;
    for (int fi = 0; fi < op->pre.fact_size; ++fi){
        if (!mgs->fact_identity[op->pre.fact[fi].val])
            ++pre_size;
    }

    IloIntTupleSet pre(env, pre_size);
    PDDL_ISET_FOR_EACH(group, other_op_id){
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
                               const pddl_iset_t *group,
                               const mg_strips_t *mgs,
                               IloEnv &env,
                               IloModel &model,
                               IloIntVarArray &fact_var,
                               IloIntVarArray &op_var,
                               pddl_err_t *err)
{
    const mg_strips_op_t *op = mgs->op + op_id;
    int other_op_id;

    int eff_size = 1;
    for (int fi = 0; fi < op->eff.fact_size; ++fi){
        if (!mgs->fact_identity[op->eff.fact[fi].val])
            ++eff_size;
    }

    IloIntTupleSet eff(env, eff_size);
    PDDL_ISET_FOR_EACH(group, other_op_id){
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
                            const pddl_iset_t *group,
                            const mg_strips_t *mgs,
                            IloEnv &env,
                            IloModel &model,
                            IloIntVarArray &fact_var,
                            IloIntVarArray &op_var,
                            pddl_err_t *err)
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
                             pddl_iset_t *redundant_ops,
                             pddl_time_limit_t *time_limit,
                             pddl_err_t *err)
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
    LOG(err, "Created %d fact and %d operator variables",
        (int)var_fact.getSize(), (int)var_op.getSize());

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    // Set operator constraints
    int num_op_constr = 0;
    for (int group_id = 0; group_id < opg->group_size; ++group_id){
        if (pddlTimeLimitCheck(time_limit) != 0)
            return -1;

        int op_id;
        const pddl_iset_t *group = &opg->group[group_id];
        PDDL_ISET_FOR_EACH(group, op_id){
            num_op_constr += mgStripsOpConstr(op_id, group, mgs, env, model,
                                              var_fact, var_op, err);
        }
        //PDDL_INFO(err, "  Created operator constraints %d",
        //         pddlISetSize(&opg.group[group_id]));
    }
    LOG(err, "Added %d operator constraints", num_op_constr);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    LOG2(err, "Added objective function min(count-diff())");

    //std::cerr << model << std::endl;

    float max_search_time = pddlTimeLimitRemain(time_limit);
    max_search_time = PDDL_MIN(max_search_time, cfg->max_search_time);
    return solve(model, var_op, cfg, max_search_time, redundant_ops, NULL,
                 "operators", err);
}

static int mgStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                const pddl_endomorphism_config_t *cfg,
                                pddl_iset_t *redundant_ops,
                                pddl_err_t *err)
{
    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    if (cfg->max_time > 0.)
        pddlTimeLimitSet(&time_limit, cfg->max_time);

    int ret = 0;
    LOG(err, "Endomorphism on MG-Strips (facts: %d, ops: %d)...",
        mg_strips->strips.fact.fact_size,
        mg_strips->strips.op.op_size);
    mg_strips_t mgs;
    mgStripsInit(&mgs, mg_strips);

    op_groups_t opg;
    opGroupsInitMGStrips(&opg, &mgs);
    LOG(err, "Operators grouped into %d groups", opg.group_size);

    mgStripsFindIdentity(&mgs, &opg);
    LOG(err, "Found %d/%d identity facts, %d/%d identity ops",
              mgStripsNumIdentityFacts(&mgs), mgs.fact_size,
              mgStripsNumIdentityOps(&mgs), mgs.op_size);
    if (mgStripsNumIdentityOps(&mgs) == mgs.op_size){
        LOG2(err, "All operators are identity");
        LOG2(err, "Found 0 redundant operators");
        opGroupsFree(&opg);
        mgStripsFree(&mgs);
        return 0;
    }

    mgStripsPrepareCVars(&mgs);
    LOG(err, "CSP needs %d fact and %d non-identity operator variables",
              mgs.cvar_fact_size, mgs.non_identity_cvar_op_size);

    IloEnv env;
    IloModel model(env);

    try {
        int rinf = mgStripsInference(mg_strips, cfg, &mgs, &opg, env, model,
                                     redundant_ops, &time_limit, err);
        if (rinf < 0){
            LOG2(err, "Time limit reached");
            LOG2(err, "Terminating inference of endomorphism");
            LOG2(err, "Terminated by a time limit");
            ret = -1;
        }else{
            ret = rinf;
        }
    } catch(IloMemoryException &e){
        LOG2(err, "Not Enough Memory");
        LOG2(err, "Terminating inference of endomorphism");
        ret = -1;
    }

    env.end();
    opGroupsFree(&opg);
    mgStripsFree(&mgs);
    return ret;
}



struct label_groups {
    pddl_iset_t init_loop;
    pddl_iset_t loop;
    pddl_iset_t init_to;
    pddl_iset_t init_from;
    pddl_iset_t goal_to;
    pddl_iset_t goal_from;
    pddl_iset_t goal;
};
typedef struct label_groups label_groups_t;

struct ts_presolve {
    std::vector<std::vector<bool>> op_allow;
    std::vector<bool> op_identity;
    std::vector<std::vector<std::vector<bool>>> state_allow;
};
typedef struct ts_presolve ts_presolve_t;

static bool cmpISetSize(const pddl_iset_t *a, const pddl_iset_t *b)
{
    return pddlISetSize(a) < pddlISetSize(b);
}

static void presolveTRStateAllow(pddl_iset_t *state_allow,
                                 const ts_presolve_t *presolve,
                                 const pddl_trans_systems_t *tss,
                                 int tsi,
                                 int from,
                                 int label,
                                 int to)
{
    const pddl_trans_system_t *ts = tss->ts[tsi];
    int olabel, ofrom, oto;
    int from_is_goal = pddlISetIn(from, &ts->goal_states);
    int to_is_goal = pddlISetIn(to, &ts->goal_states);
    int label_cost = tss->label.label[label].cost;
    const std::vector<bool> &op_allow = presolve->op_allow[label];
    PDDL_ISET(from_allow);
    PDDL_ISET(to_allow);
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
        if (from_is_goal && !pddlISetIn(ofrom, &ts->goal_states))
            continue;
        if (to_is_goal && !pddlISetIn(oto, &ts->goal_states))
            continue;
        if (!pddlISetIn(ofrom, state_allow + from))
            continue;
        if (!pddlISetIn(oto, state_allow + to))
            continue;
        pddlISetAdd(&from_allow, ofrom);
        pddlISetAdd(&to_allow, oto);
    }
    pddlISetIntersect(state_allow + from, &from_allow);
    pddlISetIntersect(state_allow + to, &to_allow);
    pddlISetFree(&from_allow);
    pddlISetFree(&to_allow);
}

static void stateAllowFree(pddl_iset_t *state_allow, int num_states)
{
    for (int si = 0; si < num_states; ++si)
        pddlISetFree(state_allow + si);
    FREE(state_allow);
}

static int presolveStateAllow(ts_presolve_t *presolve,
                              const pddl_trans_systems_t *tss,
                              int tsi,
                              pddl_time_limit_t *time_limit)
{
    const pddl_trans_system_t *ts = tss->ts[tsi];

    pddl_iset_t *state_allow = CALLOC_ARR(pddl_iset_t, ts->num_states);
    for (int si = 0; si < ts->num_states; ++si){
        if (pddlTimeLimitCheck(time_limit) != 0){
            stateAllowFree(state_allow, ts->num_states);
            return -1;
        }

        if (si == ts->init_state){
            pddlISetAdd(state_allow + si, si);

        }else if (pddlISetIn(si, &ts->goal_states)){
            int si2;
            PDDL_ISET_FOR_EACH(&ts->goal_states, si2)
                pddlISetAdd(state_allow + si, si2);

        }else{
            for (int si2 = 0; si2 < ts->num_states; ++si2)
                pddlISetAdd(state_allow + si, si2);
        }
    }

    int label_id, from, to;
    PDDL_LABELED_TRANSITIONS_SET_FOR_EACH(&ts->trans, from, label_id, to){
        presolveTRStateAllow(state_allow, presolve,
                             tss, tsi, from, label_id, to);
    }
    for (int si = 0; si < ts->num_states; ++si){
        int state;
        PDDL_ISET_FOR_EACH(state_allow + si, state)
            presolve->state_allow[tsi][si][state] = true;
    }
    stateAllowFree(state_allow, ts->num_states);
    return 0;
}

static void labelGroupsFree(const pddl_trans_systems_t *tss,
                            label_groups_t *label_group)
{
    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        pddlISetFree(&label_group[tsi].init_loop);
        pddlISetFree(&label_group[tsi].loop);
        pddlISetFree(&label_group[tsi].init_to);
        pddlISetFree(&label_group[tsi].init_from);
        pddlISetFree(&label_group[tsi].goal_to);
        pddlISetFree(&label_group[tsi].goal_from);
        pddlISetFree(&label_group[tsi].goal);
    }
    FREE(label_group);
}

static int tsPresolve(ts_presolve_t *presolve,
                      const pddl_trans_systems_t *tss,
                      pddl_time_limit_t *time_limit,
                      pddl_err_t *err)
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
    label_group = CALLOC_ARR(label_groups_t, tss->ts_size);

    std::vector<std::vector<pddl_iset_t *>> relevant(tss->label.label_size);
    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        if (pddlTimeLimitCheck(time_limit) != 0){
            labelGroupsFree(tss, label_group);
            return -1;
        }
        const pddl_trans_system_t *ts = tss->ts[tsi];
        int consider_goal = (pddlISetSize(&ts->goal_states) != ts->num_states);
        const pddl_label_set_t *labels;
        int from, to;
        PDDL_LABELED_TRANSITIONS_SET_FOR_EACH_LABEL_SET(&ts->trans,
                                                        from, labels, to){
            if (from == to){
                if (from == ts->init_state)
                    pddlISetUnion(&label_group[tsi].init_loop, &labels->label);
                pddlISetUnion(&label_group[tsi].loop, &labels->label);
            }

            int from_init = (from == ts->init_state);
            int to_init = (to == ts->init_state);
            if (from_init)
                pddlISetUnion(&label_group[tsi].init_from, &labels->label);
            if (to_init)
                pddlISetUnion(&label_group[tsi].init_to, &labels->label);

            if (consider_goal){
                int from_goal = pddlISetIn(from, &ts->goal_states);
                int to_goal = pddlISetIn(to, &ts->goal_states);
                if (from_goal)
                    pddlISetUnion(&label_group[tsi].goal_from, &labels->label);
                if (to_goal)
                    pddlISetUnion(&label_group[tsi].goal_to, &labels->label);
                if (from_goal && to_goal)
                    pddlISetUnion(&label_group[tsi].goal, &labels->label);
            }
        }
        int op;
        PDDL_ISET_FOR_EACH(&label_group[tsi].init_loop, op)
            relevant[op].push_back(&label_group[tsi].init_loop);
        PDDL_ISET_FOR_EACH(&label_group[tsi].loop, op)
            relevant[op].push_back(&label_group[tsi].loop);
        PDDL_ISET_FOR_EACH(&label_group[tsi].init_to, op)
            relevant[op].push_back(&label_group[tsi].init_to);
        PDDL_ISET_FOR_EACH(&label_group[tsi].init_from, op)
            relevant[op].push_back(&label_group[tsi].init_from);
        PDDL_ISET_FOR_EACH(&label_group[tsi].goal_to, op)
            relevant[op].push_back(&label_group[tsi].goal_to);
        PDDL_ISET_FOR_EACH(&label_group[tsi].goal_from, op)
            relevant[op].push_back(&label_group[tsi].goal_from);
        PDDL_ISET_FOR_EACH(&label_group[tsi].goal, op)
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

        PDDL_ISET(allowed);
        pddlISetUnion(&allowed, relevant[op_id][0]);
        for (size_t i = 1; i < relevant[op_id].size(); ++i)
            pddlISetIntersect(&allowed, relevant[op_id][i]);

        PDDL_ISET(allowed2);
        int op_cost = tss->label.label[op_id].cost;
        int other_op;
        PDDL_ISET_FOR_EACH(&allowed, other_op){
            if (tss->label.label[other_op].cost <= op_cost)
                pddlISetAdd(&allowed2, other_op);
        }
        pddlISetFree(&allowed);

        if (pddlISetSize(&allowed2) <= 1){
            presolve->op_identity[op_id] = true;
            presolve->op_allow[op_id].resize(tss->label.label_size, false);
            presolve->op_allow[op_id][op_id] = true;
            //fprintf(stderr, "O %d identity\n", op_id);
        }else{
            presolve->op_allow[op_id].resize(tss->label.label_size, false);
            PDDL_ISET_FOR_EACH(&allowed2, other_op)
                presolve->op_allow[op_id][other_op] = true;
            /*
            fprintf(stderr, "O %d: ", op_id);
            pddlISetPrint(&allowed2, stderr);
            fprintf(stderr, "\n");
            */
        }
        pddlISetFree(&allowed2);
    }

    labelGroupsFree(tss, label_group);
    LOG2(err, "presolve restriction of operator domains done");

    for (int tsi = 0; tsi < tss->ts_size; ++tsi){
        if (presolveStateAllow(presolve, tss, tsi, time_limit) != 0)
            return -1;
    }
    LOG2(err, "presolve restriction of state domains done");

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
                            pddl_err_t *err)
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
        LOG(err, "Could not find mapping for (%d)->%d->(%d)",
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
                         pddl_err_t *err)
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
    PDDL_ISET_FOR_EACH(&ts->goal_states, goal_state)
        goal_val.add(goal_state);
    PDDL_ISET_FOR_EACH(&ts->goal_states, goal_state){
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
                       pddl_iset_t *redundant_ops,
                       pddl_time_limit_t *time_limit,
                       pddl_err_t *err)
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
    LOG(err, "Created %d state and %d operator variables",
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
    LOG(err, "Set %d operator-identity bounds", num_ident);

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
        LOG(err, "Added %d constraints for TS %d with %d states",
            num, tsi, tss->ts[tsi]->num_states);
        num_constrs += num;
    }
    LOG(err, "Added %d constraints overall", num_constrs);

    IloObjective obj = IloMinimize(env, IloCountDifferent(var_op));
    model.add(obj);
    LOG2(err, "Added objective function");

    //std::cerr << model << std::endl;

    if (pddlTimeLimitCheck(time_limit) != 0)
        return -1;

    float max_search_time = pddlTimeLimitRemain(time_limit);
    max_search_time = PDDL_MIN(max_search_time, cfg->max_search_time);
    return solve(model, var_op, cfg, max_search_time, redundant_ops, NULL,
                 "operators", err);
}

static int transSystemRedundantOps(const pddl_trans_systems_t *tss,
                                   const pddl_endomorphism_config_t *cfg,
                                   pddl_iset_t *redundant_ops,
                                   pddl_err_t *err)
{
    pddl_time_limit_t time_limit;
    pddlTimeLimitInit(&time_limit);
    if (cfg->max_time > 0.)
        pddlTimeLimitSet(&time_limit, cfg->max_time);

    int ret = 0;
    LOG(err, "Endomorphism on factored TS (num-ts: %d, num-labels: %d) ...",
        tss->ts_size, tss->label.label_size);

    ts_presolve_t presolve;
    LOG2(err, "Running presolve...");
    if (tsPresolve(&presolve, tss, &time_limit, err) != 0){
        LOG2(err, "Time limit reached");
        LOG2(err, "Terminating presolve phase");
        LOG2(err, "Terminating inference of endomorphism");
        LOG2(err, "Terminated by a time limit");
        return -1;
    }

    int num_identity = 0;
    for (size_t i = 0; i < presolve.op_identity.size(); ++i)
        num_identity += int(presolve.op_identity[i]);
    LOG(err, "Presolve found %d identity operators", num_identity);

    if (num_identity == tss->label.label_size){
        LOG2(err, "All operators are identity");
        LOG2(err, "Found 0 redundant operators");
        return 0;
    }

    IloEnv env;
    IloModel model(env);

    try {
        int rinf = tsInference(tss, cfg, &presolve, env, model, redundant_ops,
                               &time_limit, err);
        if (rinf < 0){
            LOG2(err, "Time limit reached");
            LOG2(err, "Terminating inference of endomorphism");
            LOG2(err, "Terminated by a time limit");
            ret = -1;
        }else{
            ret = rinf;
        }
    }catch (IloMemoryException &e){
        LOG2(err, "Not Enough Memory");
        LOG2(err, "Terminating inference of endomorphism");
        ret = -1;
    }

    env.end();
    return ret;
}

static int runInSubprocess(const pddl_fdr_t *fdr,
                           const pddl_mg_strips_t *mg_strips,
                           const pddl_trans_systems_t *tss,
                           const pddl_endomorphism_config_t *cfg,
                           pddl_iset_t *redundant_ops,
                           pddl_err_t *err)
{
    LOG2(err, "Endomorphism in a subprocess ...");
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
        LOG(err, "Could not allocate shared memory of size %ld using mmap: %s",
            (long)shared_size, strerror(errno));
        return -1;
    }
    bzero(shared, shared_size);
    int *shared_ret = (int *)shared;
    char *shared_ops = (char *)(shared_ret + 1);
    *shared_ret = -1;
    LOG(err, "  Allocated %ld bytes of shared memory", (long)shared_size);

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
            PDDL_ISET_FOR_EACH(redundant_ops, op)
                shared_ops[op] = 1;
        }
        exit(ret);

    }else{
        wait(NULL);
        int ret = *shared_ret;
        if (ret == 0){
            if (redundant_ops != NULL){
                for (int op_id = 0; op_id < op_size; ++op_id){
                    if (shared_ops[op_id])
                        pddlISetAdd(redundant_ops, op_id);
                }
            }
        }
        munmap(shared, shared_size);
        if (redundant_ops != NULL){
            LOG(err, "Endomorphism in a subprocess: ret: %d, redundant ops: %d",
                ret, pddlISetSize(redundant_ops));
        }else{
            LOG(err, "Endomorphism in a subprocess: ret: %d", ret);
        }
        LOG2(err, "Endomorphism in a subprocess DONE");
        return ret;
    }
}

int pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                    const pddl_endomorphism_config_t *cfg,
                                    pddl_iset_t *redundant_ops,
                                    pddl_err_t *err)
{
    if (cfg->ignore_costs)
        PDDL_ERR_RET2(err, -1, "ignore_cost option is not supported");
    if (cfg->run_in_subprocess)
        return runInSubprocess(fdr, NULL, NULL, cfg, redundant_ops, err);
    return fdrRedundantOps(fdr, cfg, redundant_ops, err);
}

int pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                         const pddl_endomorphism_config_t *cfg,
                                         pddl_iset_t *redundant_ops,
                                         pddl_err_t *err)
{
    if (cfg->ignore_costs)
        PDDL_ERR_RET2(err, -1, "ignore_cost option is not supported");
    if (cfg->run_in_subprocess)
        return runInSubprocess(NULL, mg_strips, NULL, cfg, redundant_ops, err);
    return mgStripsRedundantOps(mg_strips, cfg, redundant_ops, err);
}

int pddlEndomorphismTransSystemRedundantOps(const pddl_trans_systems_t *tss,
                                            const pddl_endomorphism_config_t *cfg,
                                            pddl_iset_t *redundant_ops,
                                            pddl_err_t *err)
{
    if (cfg->ignore_costs)
        PDDL_ERR_RET2(err, -1, "ignore_cost option is not supported");
    if (cfg->run_in_subprocess)
        return runInSubprocess(NULL, NULL, tss, cfg, redundant_ops, err);
    return transSystemRedundantOps(tss, cfg, redundant_ops, err);
}


#else /* PDDL_CPOPTIMIZER */
int pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                    const pddl_endomorphism_config_t *cfg,
                                    pddl_iset_t *redundant_ops,
                                    pddl_err_t *err)
{
    PDDL_FATAL2("Missing CPOPTIMIZER");
    return -1;
}

int pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                         const pddl_endomorphism_config_t *cfg,
                                         pddl_iset_t *redundant_ops,
                                         pddl_err_t *err)
{
    PDDL_FATAL2("Missing CPOPTIMIZER");
    return -1;
}

int pddlEndomorphismTransSystemRedundantOps(const pddl_trans_systems_t *tss,
                                            const pddl_endomorphism_config_t *cfg,
                                            pddl_iset_t *redundant_ops,
                                            pddl_err_t *err)
{
    PDDL_FATAL2("Missing CPOPTIMIZER");
    return -1;
}

#endif /* PDDL_CPOPTIMIZER */
