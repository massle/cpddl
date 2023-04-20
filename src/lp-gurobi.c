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

static void grbError(GRBenv *env, pddl_lp_t *lp)
{
        // TODO: Use err
    PANIC("Gurobi Error: %s\n", GRBgeterrormsg(env));
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
            LOG(err, "gurobi: %s", out);
        }
    }
    return 0;
}


int pddlLPSolveGurobi(pddl_lp_t *lp, double *val, double *obj, pddl_err_t *err)
{
    GRBenv *env;
    GRBmodel *model;
    int ret;

    if ((ret = GRBemptyenv(&env)) != 0){
        // TODO: Use err
        PANIC("Gurobi Error: Could not create environment"
              " (error-code: %d)!", ret);
    }
    if (GRBsetintparam(env, "OutputFlag", 0) != 0)
        grbError(env, lp);

    if ((ret = GRBstartenv(env)) != 0){
        if (ret == GRB_ERROR_NO_LICENSE)
            WARN(err, "It seems license file wasn't found. Don't forget to"
                  " set GRB_LICENSE_FILE environment variable.");
        grbError(env, lp);
    }

    pddl_lp_compressed_row_problem_t P;
    compressedRowProblemInit(&P, lp, 
                             GRB_CONTINUOUS,
                             GRB_INTEGER,
                             GRB_BINARY,
                             pddl_false,
                             -GRB_INFINITY,
                             GRB_INFINITY);

    if (GRBnewmodel(env, &model, NULL, P.num_col,
                    P.col_obj, P.col_lb, P.col_ub, P.col_type, NULL) != 0){
        grbError(env, lp);
    }

    if (GRBaddconstrs(model, P.num_row, P.num_nz, P.row_beg, P.row_ind,
                      P.row_val, P.row_sense, P.row_rhs, NULL) != 0){
        grbError(env, lp);
    }
    GRBupdatemodel(model);
    compressedRowProblemFree(&P);

    GRBsetcallbackfunc(model, cb, err);

    int num_threads = PDDL_MAX(1, lp->cfg.num_threads);
    if (GRBsetintparam(GRBgetenv(model), "Threads", num_threads) != 0)
        grbError(env, lp);

    if (lp->cfg.time_limit > 0.){
        if (GRBsetdblparam(GRBgetenv(model), "TimeLimit", lp->cfg.time_limit) != 0)
            grbError(env, lp);
    }

    int minmax = GRB_MINIMIZE;
    if (lp->cfg.maximize)
        minmax = GRB_MAXIMIZE;
    if (GRBsetintattr(model, GRB_INT_ATTR_MODELSENSE, minmax) != 0)
        grbError(env, lp);

    int st, i, cols;

    if (GRBoptimize(model) != 0)
        grbError(env, lp);
    if (GRBgetintattr(model, "Status", &st) != 0)
        grbError(env, lp);

    if (st == GRB_OPTIMAL || st == GRB_TIME_LIMIT){
        if (st == GRB_TIME_LIMIT){
            int val;
            if (GRBgetintattr(model, "SolCount", &val) != 0)
                grbError(env, lp);
            LOG(err, "Time limit: solutions: %d", val);
            if (val <= 0){
                GRBfreemodel(model);
                GRBfreeenv(env);
                return -1;
            }
        }

        if (val != NULL){
            if (GRBgetdblattr(model, "ObjVal", val) != 0)
                grbError(env, lp);
        }
        if (obj != NULL){
            if (GRBgetintattr(model, "NumVars", &cols) != 0)
                grbError(env, lp);
            for (i = 0; i < cols; ++i){
                if (GRBgetdblattrelement(model, "X", i, obj + i) != 0)
                    grbError(env, lp);
            }
        }

        GRBfreemodel(model);
        GRBfreeenv(env);
        return 0;

    }else{
        if (obj != NULL){
            if (GRBgetintattr(model, "NumVars", &cols) != 0)
                grbError(env, lp);
            ZEROIZE_ARR(obj, cols);
        }
        if (val != NULL)
            *val = 0.;

        GRBfreemodel(model);
        GRBfreeenv(env);
        return -1;
    }
}

#else /* PDDL_GUROBI */
const char * const pddl_gurobi_version = NULL;

int pddlLPSolveGurobi(pddl_lp_t *lp, double *val, double *obj, pddl_err_t *err)
{
    PANIC("Missing Gurobi solver");
    return -1;
}
#endif /* PDDL_GUROBI */
