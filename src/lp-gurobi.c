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

#include "pddl/lp.h"
#include "pddl/libs_info.h"
#include "_lp.h"
#include "internal.h"

#ifdef PDDL_GUROBI
# include <gurobi_c.h>
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

    // TODO
    double *cobj = ALLOC_ARR(double, lp->col_size);
    double *lb = ALLOC_ARR(double, lp->col_size);
    double *ub = ALLOC_ARR(double, lp->col_size);
    char *vtype = ALLOC_ARR(char, lp->col_size);
    for (int i = 0; i < lp->col_size; ++i){
        cobj[i] = lp->col[i].obj;
        lb[i] = lp->col[i].lb;
        ub[i] = lp->col[i].ub;
        switch (lp->col[i].type){
            case PDDL_LP_COL_TYPE_REAL:
                vtype[i] = GRB_CONTINUOUS;
                break;
            case PDDL_LP_COL_TYPE_INT:
                vtype[i] = GRB_INTEGER;
                break;
            case PDDL_LP_COL_TYPE_BINARY:
                vtype[i] = GRB_BINARY;
                break;
        }
    }

    if (GRBnewmodel(env, &model, NULL, lp->col_size,
                    cobj, lb, ub, vtype, NULL) != 0){
        FREE(cobj);
        FREE(lb);
        FREE(ub);
        FREE(vtype);
        grbError(env, lp);
    }

    GRBsetcallbackfunc(model, cb, err);

    int num_threads = PDDL_MAX(1, lp->cfg.num_threads);
    if (GRBsetintparam(GRBgetenv(model), "Threads", num_threads) != 0)
        grbError(env, lp);

    if (lp->cfg.time_limit > 0.){
        if (GRBsetdblparam(GRBgetenv(model), "TimeLimit", lp->cfg.time_limit) != 0)
            grbError(env, lp);
    }

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

    if (lp->row_size > 0){
        if (GRBaddconstrs(model, lp->row_size, num_nz,
                          bag, ind, rval, sense, rhs, NULL) != 0){
            grbError(env, lp);
        }
    }

    int minmax = GRB_MINIMIZE;
    if (lp->cfg.maximize)
        minmax = GRB_MAXIMIZE;
    if (GRBsetintattr(model, GRB_INT_ATTR_MODELSENSE, minmax) != 0)
        grbError(env, lp);

    GRBupdatemodel(model);
    FREE(cobj);
    FREE(lb);
    FREE(ub);
    FREE(vtype);
    FREE(rhs);
    FREE(sense);
    FREE(bag);
    FREE(ind);
    FREE(rval);

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
