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


static pddl_lp_status_t cplexErr(CPXENVptr *env,
                                 CPXLPptr *prob,
                                 int status,
                                 pddl_lp_solution_t *sol,
                                 const char *s,
                                 pddl_err_t *err)
{
    if (status != 0 && env != NULL){
        char errmsg[1024];
        CPXgeterrorstring(*env, status, errmsg);
        ERR(err, "CPLEX: %s: %s", s, errmsg);
    }else{
        ERR(err, "CPLEX: %s", s);
    }

    sol->solved_optimally = pddl_false;
    sol->solved_suboptimally = pddl_false;
    sol->unsolvable = pddl_false;
    sol->not_solved = pddl_false;
    sol->error = pddl_true;
    sol->timed_out = pddl_false;

    if (prob != NULL && *prob != NULL)
        CPXfreeprob(*env, prob);
    if (env != NULL && *env != NULL)
        CPXcloseCPLEX(env);
    return PDDL_LP_STATUS_ERR;
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

pddl_lp_status_t pddlLPSolveCPLEX(const pddl_lp_t *lp,
                                  pddl_lp_solution_t *sol,
                                  pddl_err_t *err)
{
    int st;
    CPXENVptr env;
    CPXLPptr prob;

    _pddlLPSolutionInit(sol, lp);

    env = CPXopenCPLEX(&st);
    if (env == NULL)
        return cplexErr(&env, NULL, 0, sol, "Could not open CPLEX environment", err);

    // Set number of processing threads
    int num_threads = PDDL_MAX(1, lp->cfg.num_threads);
    st = CPXsetintparam(env, CPX_PARAM_THREADS, num_threads);
    if (st != 0)
        return cplexErr(&env, NULL, st, sol, "Could not set number of threads", err);

    CPXsetintparam(env, CPXPARAM_ScreenOutput, CPX_OFF);

    if (lp->cfg.time_limit > 0.f){
        st = CPXsetdblparam(env, CPXPARAM_TimeLimit, lp->cfg.time_limit);
        if (st != 0)
            return cplexErr(&env, NULL, st, sol, "Could not set number of threads", err);
    }

    prob = CPXcreateprob(env, &st, "");
    if (prob == NULL)
        return cplexErr(&env, NULL, 0, sol, "Could not create CPLEX problem", err);

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
        return cplexErr(&env, &prob, st, sol, "Could not create columns", err);

    st = CPXaddrows(env, prob, 0, P.num_row, P.num_nz, P.row_rhs,
                    P.row_sense, P.row_beg, P.row_ind, P.row_val, NULL, NULL);
    if (st != 0)
        return cplexErr(&env, &prob, st, sol, "Could not create columns", err);

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
            return cplexErr(&env, &prob, st, sol, "Failed to optimize MIP", err);
        CPXcallbacksetfunc(env, prob, 0, NULL, NULL);

    }else{
        CPXsetlpcallbackfunc(env, callbackLP, &log);
        if ((st = CPXlpopt(env, prob)) != 0)
            return cplexErr(&env, &prob, st, sol, "Failed to optimize LP", err);
    }

    st = CPXgetstat(env, prob);
    if (st == CPX_STAT_OPTIMAL
            || st == CPX_STAT_OPTIMAL_INFEAS
            || st == CPXMIP_OPTIMAL
            || st == CPXMIP_OPTIMAL_TOL){
        sol->solved = pddl_true;
        sol->solved_optimally = pddl_true;

    }else if (st == CPXMIP_TIME_LIM_FEAS){
        sol->solved = pddl_true;
        sol->solved_suboptimally = pddl_true;
        sol->timed_out = pddl_true;

    }else if (st == CPX_STAT_INFEASIBLE
                || st == CPX_STAT_INForUNBD
                || st == CPXMIP_INFEASIBLE
                || st == CPXMIP_INForUNBD){
        sol->unsolvable = pddl_true;

    }else if (st == CPX_STAT_ABORT_DETTIME_LIM
                || st == CPX_STAT_ABORT_DUAL_OBJ_LIM
                || st == CPX_STAT_ABORT_IT_LIM
                || st == CPX_STAT_ABORT_OBJ_LIM
                || st == CPX_STAT_ABORT_PRIM_OBJ_LIM
                || st == CPX_STAT_ABORT_USER
                || st == CPX_STAT_UNBOUNDED
                || st == CPXMIP_ABORT_INFEAS
                || st == CPXMIP_DETTIME_LIM_FEAS
                || st == CPXMIP_DETTIME_LIM_INFEAS){
        sol->not_solved = pddl_true;

    }else if (st == CPX_STAT_ABORT_TIME_LIM
                || st == CPXMIP_TIME_LIM_INFEAS){
        sol->not_solved = pddl_true;
        sol->timed_out = pddl_true;

    }else{
        char msg[1024];
        msg[1023] = '\x0';
        snprintf(msg, 1024, "Unrecognized solution status %d", st);
        return cplexErr(&env, &prob, 0, sol, msg, err);
    }

    if (sol->solved){
        st = CPXsolution(env, prob, NULL, &sol->obj_val, sol->var_val,
                         NULL, NULL, NULL);
        if (st != 0)
            return cplexErr(&env, &prob, st, sol, "Cannot retrieve solution", err);
    }
    CPXfreeprob(env, &prob);
    CPXcloseCPLEX(&env);

    return _pddlLPSolutionToStatus(sol);
}

#else /* PDDL_CPLEX */
const char * const pddl_cplex_version = NULL;

pddl_lp_status_t pddlLPSolveCPLEX(const pddl_lp_t *lp,
                                  pddl_lp_solution_t *sol,
                                  pddl_err_t *err)
{
    PANIC("Missing CPLEX solver");
    return PDDL_LP_STATUS_ERR;
}
#endif /* PDDL_CPLEX */
