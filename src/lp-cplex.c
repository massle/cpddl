/***
 * Copyright (c)2017 Daniel Fiser <danfis@danfis.cz>
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

#include "internal.h"
#include "pddl/lp.h"
#include "pddl/libs_info.h"
#include "_lp.h"

#ifdef PDDL_CPLEX
# include <ilcplex/cplex.h>
const char * const pddl_cplex_version =
    PDDL_TOSTR(CPX_VERSION_VERSION.CPX_VERSION_RELEASE.CPX_VERSION_MODIFICATION.CPX_VERSION_FIX);


static void cplexErr(CPXENVptr env, int status, const char *s)
{
    // TODO: Use pddl_err_t
    char errmsg[1024];
    CPXgeterrorstring(env, status, errmsg);
    PANIC("Error: CPLEX: %s: %s", s, errmsg);
}

struct log {
    pddl_err_t *err;
    pddl_timer_t timer;
};
typedef struct log log_t;

static int callback(CPXCALLBACKCONTEXTptr ctx, CPXLONG ctxtid, void *_log)
{
    log_t *log = _log;

    pddlTimerStop(&log->timer);
    if (pddlTimerElapsedInSF(&log->timer) < 1.)
        return 0;

    double best_sol = 0.;
    CPXcallbackgetinfodbl(ctx, CPXCALLBACKINFO_BEST_SOL, &best_sol);
    if (best_sol < -1E10 || best_sol > 1E10)
        best_sol = NAN;

    double best_bound = 0.;
    CPXcallbackgetinfodbl(ctx, CPXCALLBACKINFO_BEST_BND, &best_bound);
    if (best_bound < -1E10 || best_bound > 1E10)
        best_bound = NAN;

    int feasible = 0;
    CPXcallbackgetinfoint(ctx, CPXCALLBACKINFO_FEASIBLE, &feasible);

    CTX_NO_TIME(log->err, "cplex progress");
    LOG(log->err, "best solution: %.2f, best bound: %.2f, feasible: %d",
        best_sol, best_bound, feasible);
    CTXEND(log->err);
    pddlTimerStart(&log->timer);
    return 0;
}

static int callbackLP(CPXCENVptr env,
                      void *cbdata,
                      int wherefrom,
                      void *_log)
{
    log_t *log = _log;

    pddlTimerStop(&log->timer);
    if (pddlTimerElapsedInSF(&log->timer) < 1.)
        return 0;

    double primal = 0.;
    CPXgetcallbackinfo(env, cbdata, wherefrom,
                       CPX_CALLBACK_INFO_PRIMAL_OBJ, &primal);
    double dual = 0.;
    CPXgetcallbackinfo(env, cbdata, wherefrom,
                       CPX_CALLBACK_INFO_DUAL_OBJ, &dual);

    CTX_NO_TIME(log->err, "cplex progress");
    LOG(log->err, "primal: %.4f, dual: %.4f", primal, dual);
    CTXEND(log->err);
    pddlTimerStart(&log->timer);
    return 0;
}

int pddlLPSolveCPLEX(pddl_lp_t *lp, double *val, double *obj, pddl_err_t *err)
{
    int st;
    CPXENVptr env;
    CPXLPptr prob;

    env = CPXopenCPLEX(&st);
    if (env == NULL)
        cplexErr(env, st, "Could not open CPLEX environment");

    // Set number of processing threads
    int num_threads = PDDL_MAX(1, lp->cfg.num_threads);
    st = CPXsetintparam(env, CPX_PARAM_THREADS, num_threads);
    if (st != 0)
        cplexErr(env, st, "Could not set number of threads");

    CPXsetintparam(env, CPXPARAM_ScreenOutput, CPX_OFF);

    if (lp->cfg.time_limit > 0.f){
        st = CPXsetdblparam(env, CPXPARAM_TimeLimit, lp->cfg.time_limit);
        if (st != 0)
            cplexErr(env, st, "Could not set number of threads");
    }

    prob = CPXcreateprob(env, &st, "");
    if (prob == NULL)
        cplexErr(env, st, "Could not create CPLEX problem");

    if (lp->cfg.maximize){
        CPXchgobjsen(env, prob, CPX_MAX);
    }else{
        CPXchgobjsen(env, prob, CPX_MIN);
    }

    // TODO: Refactor with highs and gurobi
    pddl_bool_t mip = pddl_false;
    double *cobj = ALLOC_ARR(double, lp->col_size);
    double *clb = ALLOC_ARR(double, lp->col_size);
    double *cub = ALLOC_ARR(double, lp->col_size);
    char *ctype = ALLOC_ARR(char, lp->col_size);
    for (int ci = 0; ci < lp->col_size; ++ci){
        cobj[ci] = lp->col[ci].obj;
        clb[ci] = lp->col[ci].lb;
        cub[ci] = lp->col[ci].ub;
        switch (lp->col[ci].type){
            case PDDL_LP_COL_TYPE_REAL:
                ctype[ci] = CPX_CONTINUOUS;
                break;
            case PDDL_LP_COL_TYPE_INT:
                ctype[ci] = CPX_INTEGER;
                mip = pddl_true;
                break;
            case PDDL_LP_COL_TYPE_BINARY:
                ctype[ci] = CPX_BINARY;
                mip = pddl_true;
                break;
        }
    }
    st = CPXnewcols(env, prob, lp->col_size, cobj, clb, cub,
                    (mip ? ctype : NULL), NULL);
    if (st != 0)
        cplexErr(env, st, "Could not create columns");
    FREE(cobj);
    FREE(clb);
    FREE(cub);
    FREE(ctype);

    int num_row = lp->row_size;
    int num_nz = 0;
    for (int ri = 0; ri < lp->row_size; ++ri)
        num_nz += lp->row[ri].coef_size;

    double *rhs = ALLOC_ARR(double, num_row);
    char *sense = ALLOC_ARR(char, num_row);
    for (int i = 0; i < lp->row_size; ++i){
        sense[i] = lp->row[i].sense;
        rhs[i] = lp->row[i].rhs;
    }

    int *bag = ALLOC_ARR(int, num_row);
    int *ind = ALLOC_ARR(int, num_nz);
    double *rval = ALLOC_ARR(double, num_nz);
    int ins = 0;
    for (int ri = 0; ri < num_row; ++ri){
        bag[ri] = ins;
        for (int ci = 0; ci < lp->row[ri].coef_size; ++ci){
            ind[ins] = lp->row[ri].coef[ci].col;
            rval[ins] = lp->row[ri].coef[ci].coef;
            ++ins;
        }
    }
    ASSERT(ins == num_nz);
    st = CPXaddrows(env, prob, 0, num_row, num_nz, rhs, sense, bag, ind,
                    rval, NULL, NULL);
    if (st != 0)
        cplexErr(env, st, "Could not create columns");

    FREE(rhs);
    FREE(sense);
    FREE(bag);
    FREE(ind);
    FREE(rval);

    if (lp->cfg.tune_int_operator_potential){
        CPXsetintparam(env, CPXPARAM_Preprocessing_Relax, CPX_ON);
        CPXsetintparam(env, CPXPARAM_Preprocessing_Dual, 1);
        //CPXsetintparam(env, CPXPARAM_Preprocessing_CoeffReduce, 2);
        //CPXsetintparam(env, CPXPARAM_Preprocessing_Dependency, 3);
    }

    log_t log;
    log.err = err;
    pddlTimerStart(&log.timer);
    if (mip){
        CPXcallbacksetfunc(env, prob,
                           CPX_CALLBACKCONTEXT_GLOBAL_PROGRESS
                                | CPX_CALLBACKCONTEXT_LOCAL_PROGRESS
                                | CPX_CALLBACKCONTEXT_RELAXATION
                                | CPX_CALLBACKCONTEXT_CANDIDATE,
                           callback, &log);
        if ((st = CPXmipopt(env, prob)) != 0)
            cplexErr(env, st, "Failed to optimize MIP");
        CPXcallbacksetfunc(env, prob, 0, NULL, NULL);

    }else{
        CPXsetlpcallbackfunc(env, callbackLP, &log);
        if ((st = CPXlpopt(env, prob)) != 0)
            cplexErr(env, st, "Failed to optimize LP");
    }

    st = CPXgetstat(env, prob);
    if (st == CPX_STAT_OPTIMAL
            || st == CPX_STAT_OPTIMAL_INFEAS
            || st == CPXMIP_OPTIMAL
            || st == CPXMIP_OPTIMAL_TOL
            || st == CPXMIP_TIME_LIM_FEAS){
        st = CPXsolution(env, prob, NULL, val, obj, NULL, NULL, NULL);
        if (st != 0)
            cplexErr(env, st, "Cannot retrieve solution");
    }else{
        if (obj != NULL){
            int cols = CPXgetnumcols(env, prob);
            ZEROIZE_ARR(obj, cols);
        }
        if (val != NULL)
            *val = 0.;
        CPXfreeprob(env, &prob);
        CPXcloseCPLEX(&env);
        return -1;
    }
    CPXfreeprob(env, &prob);
    CPXcloseCPLEX(&env);
    return 0;
}

#else /* PDDL_CPLEX */
const char * const pddl_cplex_version = NULL;

int pddlLPSolveCPLEX(pddl_lp_t *lp, double *val, double *obj, pddl_err_t *err)
{
    PANIC("Missing CPLEX solver");
    return -1;
}
#endif /* PDDL_CPLEX */
