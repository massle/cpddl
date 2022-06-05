/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>
 *
 *  This file is part of cpddl.
 *
 *  Distributed under the OSI-approved BSD License (the "License");
 *  see accompanying file BDS-LICENSE for details or see
 *  <http://www.opensource.org/licenses/bsd-license.php>.
 *
 *  This software is distributed WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the License for more information.
 */

#include "_csp.h"
#include "internal.h"

#ifdef PDDL_CPOPTIMIZER
#define IL_STD
#include <ilcp/cp.h>
#include <ilcplex/cpxconst.h>

#if CPX_VERSION_VERSION < 12 || (CPX_VERSION_VERSION == 12 && CPX_VERSION_RELEASE < 9)
# define NO_LOGGER
#endif

#ifndef NO_LOGGER
class Logger : public IloCP::Callback {
    pddl_err_t *err;

  public:
    Logger(pddl_err_t *err) : err(err){}
#if CPX_VERSION_VERSION == 12 && CPX_VERSION_RELEASE == 9
    virtual void invoke(IloCP cp, Callback::Type reason)
#else
    virtual void invoke(IloCP cp, Callback::Reason reason)
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

struct pddl_csp_cp {
    pddl_csp_t csp;
    IloEnv env;
    IloModel model;
#ifndef NO_LOGGER
    Logger *logger;
#endif /* NO_LOGGER */

    IloIntVarArray int_var;
    IloCP *cp;

    pddl_csp_cp(const pddl_csp_config_t *cfg)
        : env(), model(env), int_var(env), cp(NULL)
    {
        csp.cls = &pddl_csp_cp_optimizer;
        csp.cfg = *cfg;
#ifndef NO_LOGGER
        logger = NULL;
#endif /* NO_LOGGER */
    }

    ~pddl_csp_cp()
    {
#ifndef NO_LOGGER
        if (logger != NULL)
            delete logger;
#endif /* NO_LOGGER */
        if (cp != NULL){
            cp->end();
            delete cp;
        }
        env.end();
    }
};
typedef struct pddl_csp_cp pddl_csp_cp_t;

#define CSP(C) pddl_csp_cp_t *csp = pddl_container_of(_csp, pddl_csp_cp_t, csp)

static pddl_csp_t *cpNew(const pddl_csp_config_t *cfg, pddl_err_t *err)
{
    pddl_csp_cp_t *csp = new pddl_csp_cp(cfg);
    return &csp->csp;
}

static void cpDel(pddl_csp_t *_csp)
{
    CSP(_csp);
    delete csp;
}

static int cpAddVarInt(pddl_csp_t *_csp,
                       int min_val,
                       int max_val,
                       const char *name)
{
    CSP(_csp);
    int idx = csp->int_var.getSize();
    csp->int_var.add(IloIntVar(csp->env, min_val, max_val, name));
    return idx;
}

static int cpAddDomainInt(pddl_csp_t *_csp,
                          int tuple_size,
                          int num_var_tuples,
                          int num_val_tuples,
                          const int *var,
                          const int *val)
{
    CSP(_csp);
    IloIntTupleSet cpval(csp->env, tuple_size);
    for (int vi = 0, idx = 0; vi < num_val_tuples; ++vi, idx += tuple_size){
        IloIntArray cpval_tuple(csp->env, tuple_size);
        for (int i = 0; i < tuple_size; ++i)
            cpval_tuple[i] = val[idx + i];
        cpval.add(cpval_tuple);
    }

    for (int vari = 0, idx = 0; vari < num_var_tuples; ++vari, idx += tuple_size){
        IloIntVarArray cpvar(csp->env, tuple_size);
        for (int i = 0; i < tuple_size; ++i)
            cpvar[i] = csp->int_var[var[idx + i]];
        csp->model.add(IloAllowedAssignments(csp->env, cpvar, cpval));
    }

    return 0;
}

static int cpAddEqInt(pddl_csp_t *_csp, int var_id, int value)
{
    CSP(_csp);
    csp->model.add(csp->int_var[var_id] == value);
    return 0;
}

static int cpAddObjMinCountDifferent(pddl_csp_t *_csp,
                                     int var_size,
                                     const int *var)
{
    CSP(_csp);
    IloIntVarArray cpvar(csp->env, var_size);
    for (int i = 0; i < var_size; ++i)
        cpvar[i] = csp->int_var[var[i]];
    IloObjective obj = IloMinimize(csp->env, IloCountDifferent(cpvar));
    csp->model.add(obj);
    return 0;
}

static int cpGetValInt(pddl_csp_t *_csp, int var_id)
{
    CSP(_csp);
    if (csp->cp == NULL)
        return INT_MIN;
    return csp->cp->getValue(csp->int_var[var_id]);
}

static int cpEnd(pddl_csp_cp_t *csp, pddl_err_t *err)
{
    int ret = PDDL_CSP_UNKNOWN;
    switch (csp->cp->getInfo(IloCP::FailStatus)){
        case IloCP::SearchHasNotFailed:
        case IloCP::SearchHasFailedNormally:
            LOG2(err, "Provably No Solution");
            ret = PDDL_CSP_NO_SOLUTION;
            break;
        case IloCP::SearchStoppedByLimit:
            LOG2(err, "Terminated by a time or fail limit");
            ret = PDDL_CSP_REACHED_LIMIT;
            break;
        case IloCP::SearchStoppedByLabel:
            LOG2(err, "Terminated -- stopped-by-label");
            ret = PDDL_CSP_UNKNOWN;
            break;
        case IloCP::SearchStoppedByExit:
            LOG2(err, "Terminated by exitSearch()");
            ret = PDDL_CSP_ABORTED;
            break;
        case IloCP::SearchStoppedByAbort:
            LOG2(err, "Aborted");
            ret = PDDL_CSP_ABORTED;
            break;
        case IloCP::UnknownFailureStatus:
            LOG2(err, "Unknown failure");
            ret = PDDL_CSP_UNKNOWN;
            break;
    }

    csp->cp->endSearch();
    csp->cp->end();
    delete csp->cp;
    csp->cp = NULL;
#ifndef NO_LOGGER
    delete csp->logger;
    csp->logger = NULL;
#endif /* NO_LOGGER */
    return ret;
}

static int cpNext(pddl_csp_cp_t *csp, pddl_err_t *err)
{
    int ret = PDDL_CSP_FOUND;
    LOG2(err, "Solving model ...");
    if (csp->cp->next()){
        LOG2(err, "Found solution.");

    }else{
        ret = cpEnd(csp, err);
    }
    return ret;
}

static int cpSolve(pddl_csp_t *_csp, pddl_err_t *err)
{
    CSP(_csp);
    CTX(err, "csp_solve", "CSP-solve");
    if (csp->cp != NULL){
        int ret = cpNext(csp, err);
        CTXEND(err);
        return ret;
    }

    csp->cp = new IloCP(csp->model);
#ifndef NO_LOGGER
    csp->logger = new Logger(err);
    csp->cp->addCallback(csp->logger);
#endif /* NO_LOGGER */
    csp->cp->setParameter(IloCP::LogVerbosity, IloCP::Quiet);
    csp->cp->setParameter(IloCP::Workers, csp->csp.cfg.num_threads);
    if (csp->csp.cfg.max_search_time > 0.)
        csp->cp->setParameter(IloCP::TimeLimit, csp->csp.cfg.max_search_time);

    int ret = PDDL_CSP_FOUND;
    LOG2(err, "Solving model ...");
    csp->cp->startNewSearch();
    if (csp->cp->next()){
        LOG2(err, "Found solution.");

    }else{
        ret = cpEnd(csp, err);
    }
    CTXEND(err);
    return ret;
}

static void cpDump(pddl_csp_t *_csp, const char *fn)
{
    CSP(_csp);
    IloCP cp(csp->model);
    cp.dumpModel(fn);
}

pddl_csp_cls_t pddl_csp_cp_optimizer = {
    PDDL_CSP_CP_OPTIMIZER,
    "cp-optimizer",
    cpNew,
    cpDel,
    cpAddVarInt,
    cpAddDomainInt,
    cpAddEqInt,
    cpAddObjMinCountDifferent,
    cpGetValInt,
    cpSolve,
    cpDump,
};

#else /* PDDL_CPOPTIMIZER */
pddl_csp_cls_t pddl_csp_cp_optimizer;
#endif /* PDDL_CPOPTIMIZER */
