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

#include "pddl/csp.h"
#include "internal.h"

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

struct pddl_csp {
    pddl_csp_config_t cfg;
    IloEnv env;
    IloModel model;
#ifndef NO_LOGGER
    Logger *logger;
#endif /* NO_LOGGER */

    IloIntVarArray int_var;
    IloCP *cp;

    pddl_csp(const pddl_csp_config_t *cfg)
        : cfg(*cfg), env(), model(env), int_var(env), cp(NULL)
    {
#ifndef NO_LOGGER
        logger = NULL;
#endif /* NO_LOGGER */
    }

    ~pddl_csp()
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

pddl_csp_t *pddlCSPNew(const pddl_csp_config_t *cfg, pddl_err_t *err)
{
    pddl_csp_t *csp = new pddl_csp(cfg);
    return csp;
}

void pddlCSPDel(pddl_csp_t *csp)
{
    delete csp;
}

int pddlCSPAddVarInt(pddl_csp_t *csp,
                     int min_val,
                     int max_val,
                     const char *name)
{
    int idx = csp->int_var.getSize();
    csp->int_var.add(IloIntVar(csp->env, min_val, max_val, name));
    return idx;
}

int pddlCSPAddDomainInt(pddl_csp_t *csp,
                        int var_size,
                        int val_size,
                        const int *var,
                        const int *val)
{
    IloIntVarArray cpvar(csp->env, var_size);
    for (int i = 0; i < var_size; ++i)
        cpvar[i] = csp->int_var[var[i]];

    IloIntTupleSet cpval(csp->env, var_size);
    for (int vi = 0, idx = 0; vi < val_size; ++vi, idx += var_size){
        IloIntArray cpval_tuple(csp->env, var_size);
        for (int i = 0; i < var_size; ++i)
            cpval_tuple[i] = val[idx + i];
        cpval.add(cpval_tuple);
    }
    csp->model.add(IloAllowedAssignments(csp->env, cpvar, cpval));
    return 0;
}

int pddlCSPAddEqInt(pddl_csp_t *csp, int var_id, int value)
{
    csp->model.add(csp->int_var[var_id] == value);
    return 0;
}

int pddlCSPAddObjMinCountDifferent(pddl_csp_t *csp,
                                   int var_size,
                                   const int *var)
{
    IloIntVarArray cpvar(csp->env, var_size);
    for (int i = 0; i < var_size; ++i)
        cpvar[i] = csp->int_var[var[i]];
    IloObjective obj = IloMinimize(csp->env, IloCountDifferent(cpvar));
    csp->model.add(obj);
    return 0;
}

int pddlCSPGetValInt(pddl_csp_t *csp, int var_id)
{
    if (csp->cp == NULL)
        return INT_MIN;
    return csp->cp->getValue(csp->int_var[var_id]);
}

static int pddlCSPEnd(pddl_csp_t *csp, pddl_err_t *err)
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

static int pddlCSPNext(pddl_csp_t *csp, pddl_err_t *err)
{
    int ret = PDDL_CSP_FOUND;
    LOG2(err, "Solving model ...");
    csp->cp->startNewSearch();
    if (csp->cp->next()){
        LOG2(err, "Found solution.");

    }else{
        ret = pddlCSPEnd(csp, err);
    }
    return ret;
}

int pddlCSPSolve(pddl_csp_t *csp, pddl_err_t *err)
{
    CTX(err, "csp_solve", "CSP-solve");
    if (csp->cp != NULL){
        int ret = pddlCSPNext(csp, err);
        CTXEND(err);
        return ret;
    }

    csp->cp = new IloCP(csp->model);
#ifndef NO_LOGGER
    csp->logger = new Logger(err);
    csp->cp->addCallback(csp->logger);
#endif /* NO_LOGGER */
    csp->cp->setParameter(IloCP::LogVerbosity, IloCP::Quiet);
    csp->cp->setParameter(IloCP::Workers, csp->cfg.num_threads);
    csp->cp->setParameter(IloCP::TimeLimit, csp->cfg.max_search_time);

    int ret = PDDL_CSP_FOUND;
    LOG2(err, "Solving model ...");
    csp->cp->startNewSearch();
    if (csp->cp->next()){
        LOG2(err, "Found solution.");

    }else{
        ret = pddlCSPEnd(csp, err);
    }
    CTXEND(err);
    return ret;
}

void pddlCSPDump(pddl_csp_t *csp, const char *fn)
{
    IloCP cp(csp->model);
    cp.dumpModel(fn);
}

#else /* PDDL_CPOPTIMIZER */

#define ERROR PDDL_FATAL2("csp module requires IBM Cplex CP Optimizer")

pddl_csp_t *pddlCSPNew(const pddl_csp_config_t *cfg, pddl_err_t *err)
{
    ERROR;
    return NULL;
}

void pddlCSPDel(pddl_csp_t *csp)
{
    ERROR;
}

int pddlCSPAddVarInt(pddl_csp_t *csp,
                     int min_val,
                     int max_val,
                     const char *name);
{
    ERROR;
    return -1;
}

int pddlCSPAddDomainInt(pddl_csp_t *csp,
                        int var_size,
                        int val_size,
                        const int *var,
                        const int *val)
{
    ERROR;
    return -1;
}

int pddlCSPAddEqInt(pddl_csp_t *csp, int var_id, int value)
{
    ERROR;
    return -1;
}

int pddlCSPAddObjMinCountDifferent(pddl_csp_t *csp,
                                   int var_size,
                                   const int *var)
{
    ERROR;
    return -1;
}

int pddlCSPGetValInt(pddl_csp_t *csp, int var_id)
{
    ERROR;
    return -1;
}

int pddlCSPSolve(pddl_csp_t *csp, pddl_err_t *err)
{
    ERROR;
    return -1;
}

void pddlCSPDump(pddl_csp_t *csp, const char *fn)
{
    ERROR;
}
#endif /* PDDL_CPOPTIMIZER */
