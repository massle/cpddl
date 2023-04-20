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
#include "_lp_compressed_row_problem.h"
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

    pddl_lp_compressed_row_problem_t P;
    compressedRowProblemInit(&P, lp, 
                             CPX_CONTINUOUS,
                             CPX_INTEGER,
                             CPX_BINARY,
                             pddl_false,
                             -CPX_INFBOUND,
                             CPX_INFBOUND);

    pddl_bool_t is_mip = P.is_mip;
    st = CPXnewcols(env, prob, P.num_col, P.col_obj, P.col_lb, P.col_ub,
                    (P.is_mip ? P.col_type : NULL), NULL);
    if (st != 0)
        cplexErr(env, st, "Could not create columns");

    st = CPXaddrows(env, prob, 0, P.num_row, P.num_nz, P.row_rhs,
                    P.row_sense, P.row_beg, P.row_ind, P.row_val, NULL, NULL);
    if (st != 0)
        cplexErr(env, st, "Could not create columns");

    compressedRowProblemFree(&P);

    if (lp->cfg.tune_int_operator_potential){
        CPXsetintparam(env, CPXPARAM_Preprocessing_Relax, CPX_ON);
        CPXsetintparam(env, CPXPARAM_Preprocessing_Dual, 1);
        //CPXsetintparam(env, CPXPARAM_Preprocessing_CoeffReduce, 2);
        //CPXsetintparam(env, CPXPARAM_Preprocessing_Dependency, 3);
    }

    log_t log;
    log.err = err;
    pddlTimerStart(&log.timer);
    if (is_mip){
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
