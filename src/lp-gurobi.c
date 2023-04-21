/***
 * cpddl
 * --------
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

#ifdef PDDL_GUROBI
# include <gurobi_c.h>
#include "_lp_compressed_row_problem.h"
const char * const pddl_gurobi_version =
    PDDL_TOSTR(GRB_VERSION_MAJOR.GRB_VERSION_MINOR.GRB_VERSION_TECHNICAL);

static pddl_lp_status_t grbErr(GRBenv *env, GRBmodel *model,
                               pddl_lp_solution_t *sol, const char *s,
                               pddl_err_t *err)
{
    if (env != NULL){
        ERR(err, "Gurobi: %s: %s", s, GRBgeterrormsg(env));
    }else{
        ERR(err, "Gurobi: %s", s);
    }
    if (model != NULL)
        GRBfreemodel(model);
    if (env != NULL)
        GRBfreeenv(env);

    sol->solved_optimally = pddl_false;
    sol->solved_suboptimally = pddl_false;
    sol->unsolvable = pddl_false;
    sol->not_solved = pddl_false;
    sol->error = pddl_true;
    sol->timed_out = pddl_false;
    return PDDL_LP_STATUS_ERR;
}

static int cb(GRBmodel *model, void *cbdata, int where, void *ud)
{
    pddl_err_t *err = ud;
    if (where == GRB_CB_MESSAGE){
        char *msg;
        if (GRBcbget(cbdata, where, GRB_CB_MSG_STRING, (void *)&msg) == 0){
            char out[128];
            int msglen = strlen(msg);
            msglen = PDDL_MIN(128, msglen - 1);
            memcpy(out, msg, msglen * sizeof(char));
            out[msglen] = '\x0';
            LOG(err, "log: %s", out);
        }
    }
    return 0;
}


pddl_lp_status_t pddlLPSolveGurobi(const pddl_lp_t *lp,
                                   pddl_lp_solution_t *sol,
                                   pddl_err_t *err)
{
    CTX_NO_TIME(err, "LP-Gurobi");
    LOG(err, "version: %s", pddl_gurobi_version);
    LOG(err, "problem: cols: %d, rows: %d, maximize: %b, time_limit: %.2f,"
        " tune-int-op-pot: %b",
        lp->col_size, lp->row_size, lp->cfg.maximize, lp->cfg.time_limit,
        lp->cfg.tune_int_operator_potential);
    GRBenv *env = NULL;
    GRBmodel *model = NULL;
    int ret;

    _pddlLPSolutionInit(sol, lp);

    if ((ret = GRBemptyenv(&env)) != 0){
        char msg[1024];
        snprintf(msg, 1024, "Could not create environment (error code: %d)", ret);
        msg[1023] = '\x0';
        CTXEND(err);
        return grbErr(NULL, NULL, sol, msg, err);
    }
    if (GRBsetintparam(env, "OutputFlag", 0) != 0){
        CTXEND(err);
        return grbErr(env, NULL, sol, "Could not set OutputFlag", err);
    }

    if ((ret = GRBstartenv(env)) != 0){
        if (ret == GRB_ERROR_NO_LICENSE)
            WARN(err, "It seems license file wasn't found. Don't forget to"
                  " set GRB_LICENSE_FILE environment variable.");
        CTXEND(err);
        return grbErr(env, NULL, sol, "Could not start Gurobi environment", err);
    }

    pddl_lp_compressed_row_problem_t P;
    compressedRowProblemInit(&P, lp, 
                             GRB_CONTINUOUS,
                             GRB_INTEGER,
                             GRB_BINARY,
                             pddl_false,
                             -GRB_INFINITY,
                             GRB_INFINITY);
    LOG(err, "problem: non-zero coefficients: %d", P.num_nz);

    if (GRBnewmodel(env, &model, NULL, P.num_col,
                    P.col_obj, P.col_lb, P.col_ub, P.col_type, NULL) != 0){
        CTXEND(err);
        return grbErr(env, NULL, sol, "Could create a model", err);
    }

    if (GRBaddconstrs(model, P.num_row, P.num_nz, P.row_beg, P.row_ind,
                      P.row_val, P.row_sense, P.row_rhs, NULL) != 0){
        CTXEND(err);
        return grbErr(env, model, sol, "Could add constraints", err);
    }
    GRBupdatemodel(model);
    compressedRowProblemFree(&P);

    GRBsetcallbackfunc(model, cb, err);

    int num_threads = PDDL_MAX(1, lp->cfg.num_threads);
    if (GRBsetintparam(GRBgetenv(model), "Threads", num_threads) != 0){
        CTXEND(err);
        return grbErr(env, model, sol, "Could set number of threads", err);
    }

    if (lp->cfg.time_limit > 0.){
        if (GRBsetdblparam(GRBgetenv(model), "TimeLimit", lp->cfg.time_limit) != 0){
            CTXEND(err);
            return grbErr(env, model, sol, "Could set time limit", err);
        }
    }

    int minmax = GRB_MINIMIZE;
    if (lp->cfg.maximize)
        minmax = GRB_MAXIMIZE;
    if (GRBsetintattr(model, GRB_INT_ATTR_MODELSENSE, minmax) != 0){
        CTXEND(err);
        return grbErr(env, model, sol, "Could set minimization/maximization", err);
    }

    if (GRBoptimize(model) != 0){
        CTXEND(err);
        return grbErr(env, model, sol, "Could not optimize model", err);
    }

    int st;
    if (GRBgetintattr(model, "Status", &st) != 0){
        CTXEND(err);
        return grbErr(env, model, sol, "Could not obtain solution status", err);
    }

    if (st == GRB_OPTIMAL){
        sol->solved = pddl_true;
        sol->solved_optimally = pddl_true;

    }else if (st == GRB_SUBOPTIMAL){
        sol->solved = pddl_true;
        sol->solved_suboptimally = pddl_true;

    }else if (st == GRB_INFEASIBLE
                || st == GRB_INF_OR_UNBD
                || st == GRB_UNBOUNDED){
        sol->unsolvable = pddl_true;

    }else if (st == GRB_TIME_LIMIT
                || st == GRB_CUTOFF
                || st == GRB_ITERATION_LIMIT
                || st == GRB_NODE_LIMIT
                || st == GRB_SOLUTION_LIMIT
                || st == GRB_INTERRUPTED
                || st == GRB_NUMERIC
                || st == GRB_USER_OBJ_LIMIT
                || st == GRB_WORK_LIMIT){
        int val;
        if (GRBgetintattr(model, "SolCount", &val) != 0){
            CTXEND(err);
            return grbErr(env, model, sol, "Could not obtain number of solutions", err);
        }

        if (val <= 0){
            sol->not_solved = pddl_true;
        }else{
            sol->solved = pddl_true;
            sol->solved_suboptimally = pddl_true;
        }
        if (st == GRB_TIME_LIMIT){
            LOG(err, "Time limit reached: solutions: %d", val);
            sol->timed_out = pddl_true;
        }

    }else{
        char msg[1024];
        snprintf(msg, 1024, "Unrecognized solution status %d", st);
        msg[1023] = '\x0';
        CTXEND(err);
        return grbErr(env, model, sol, msg, err);
    }

    if (sol->solved){
        if (GRBgetdblattr(model, "ObjVal", &sol->obj_val) != 0){
            CTXEND(err);
            return grbErr(env, model, sol, "Could not obtain objective value", err);
        }

        if (sol->var_val != NULL){
            int num_cols;
            if (GRBgetintattr(model, "NumVars", &num_cols) != 0){
                CTXEND(err);
                return grbErr(env, model, sol, "Could not obtain number of columns", err);
            }
            PANIC_IF(num_cols != lp->col_size, "Invalid number of columns.");
            for (int i = 0; i < lp->col_size; ++i){
                if (GRBgetdblattrelement(model, "X", i, sol->var_val + i) != 0){
                    CTXEND(err);
                    return grbErr(env, model, sol, "Could not obtain variable value", err);
                }
            }
        }
    }

    GRBfreemodel(model);
    GRBfreeenv(env);
    CTXEND(err);
    return _pddlLPSolutionToStatus(sol);
}

#else /* PDDL_GUROBI */
const char * const pddl_gurobi_version = NULL;

pddl_lp_status_t pddlLPSolveGurobi(const pddl_lp_t *lp,
                                   pddl_lp_solution_t *sol,
                                   pddl_err_t *err)
{
    PANIC("Missing Gurobi solver");
    return PDDL_LP_STATUS_ERR;
}
#endif /* PDDL_GUROBI */
