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
#include "_lp.h"
#include "internal.h"

#ifdef PDDL_GUROBI
# include <gurobi_c.h>

#define MAX_COEFS 1000
struct _lp_t {
    pddl_lp_t cls;
    GRBenv *env;
    GRBmodel *model;
    int *coef_row;
    int *coef_col;
    double *coef_coef;
    int coef_size;
};
typedef struct _lp_t lp_t;

#define LP(l) pddl_container_of((l), lp_t, cls)

static char lpSense(char sense)
{
    if (sense == 'L'){
        return GRB_LESS_EQUAL;
    }else if (sense == 'G'){
        return GRB_GREATER_EQUAL;
    }else if (sense == 'E'){
        return GRB_EQUAL;
    }else{
        fprintf(stderr, "Gurobi Error: Unkown sense: %c\n", sense);
        return GRB_EQUAL;
    }
}

static void grbError(lp_t *lp)
{
    fprintf(stderr, "LP Gurobi Error: %s\n", GRBgeterrormsg(lp->env));
    exit(-1);
}

static pddl_lp_t *new(const pddl_lp_config_t *cfg, pddl_err_t *err)
{
    int ret;

    lp_t *lp = ALLOC(lp_t);
    lp->cls.cls = &pddl_lp_gurobi;
    lp->cls.err = err;
    lp->cls.cfg = *cfg;
    if ((ret = GRBloadenv(&lp->env, NULL)) != 0){
        FATAL("LP Gurobi Error: Could not create environment"
              " (error-code: %d)!", ret);
    }
    if (GRBnewmodel(lp->env, &lp->model, NULL, cfg->cols,
                NULL, NULL, NULL, NULL, NULL) != 0){
        grbError(lp);
    }

    if (GRBsetintparam(lp->env, "OutputFlag", 1) != 0)
        grbError(lp);

    int num_threads = PDDL_MAX(1, cfg->num_threads);
    if (GRBsetintparam(lp->env, "Threads", num_threads) != 0)
        grbError(lp);

    if (cfg->time_limit > 0.){
        if (GRBsetdblparam(lp->env, "TimeLimit", cfg->time_limit) != 0)
            grbError(lp);
    }

    if (cfg->rows > 0){
        if (GRBaddconstrs(lp->model, cfg->rows, 0,
                    NULL, NULL, NULL, NULL, NULL, NULL) != 0){
            grbError(lp);
        }
    }

    int sense = GRB_MINIMIZE;
    if (cfg->maximize)
        sense = GRB_MAXIMIZE;
    if (GRBsetintattr(lp->model, GRB_INT_ATTR_MODELSENSE, sense) != 0)
        grbError(lp);

    GRBupdatemodel(lp->model);

    lp->coef_size = 0;
    lp->coef_row = ALLOC_ARR(int, MAX_COEFS);
    lp->coef_col = ALLOC_ARR(int, MAX_COEFS);
    lp->coef_coef = ALLOC_ARR(double, MAX_COEFS);
    return &lp->cls;
}

static void del(pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    GRBfreeenv(lp->env);
    FREE(lp->coef_row);
    FREE(lp->coef_col);
    FREE(lp->coef_coef);
    FREE(lp);
}

static void setObj(pddl_lp_t *_lp, int i, double coef)
{
    lp_t *lp = LP(_lp);
    if (GRBsetdblattrelement(lp->model, "Obj", i, coef) != 0)
        grbError(lp);
}

static void setVarRange(pddl_lp_t *_lp, int i, double lb, double ub)
{
    lp_t *lp = LP(_lp);
    if (GRBsetdblattrelement(lp->model, "LB", i, lb) != 0)
        grbError(lp);
    if (GRBsetdblattrelement(lp->model, "UB", i, ub) != 0)
        grbError(lp);
}

static void setVarFree(pddl_lp_t *_lp, int i)
{
    setVarRange(_lp, i, -1e21, 1e21);
}

static void setVarInt(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    if (GRBsetcharattrelement(lp->model, "VType", i, 'I') != 0)
        grbError(lp);
}

static void setVarBinary(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    if (GRBsetcharattrelement(lp->model, "VType", i, 'B') != 0)
        grbError(lp);
}

static void setCoef(pddl_lp_t *_lp, int row, int col, double coef)
{
    lp_t *lp = LP(_lp);
    if (lp->coef_size == MAX_COEFS){
        GRBupdatemodel(lp->model);
        lp->coef_size = 0;
    }
    lp->coef_row[lp->coef_size] = row;
    lp->coef_col[lp->coef_size] = col;
    lp->coef_coef[lp->coef_size] = coef;
    if (GRBchgcoeffs(lp->model, 1,
                     &lp->coef_row[lp->coef_size],
                     &lp->coef_col[lp->coef_size],
                     &lp->coef_coef[lp->coef_size]) != 0){
        grbError(lp);
    }
    ++lp->coef_size;
}

static void setRHS(pddl_lp_t *_lp, int row, double rhs, char sense)
{
    lp_t *lp = LP(_lp);
    if (GRBsetcharattrelement(lp->model, "Sense", row, lpSense(sense)) != 0)
        grbError(lp);
    if (GRBsetdblattrelement(lp->model, "RHS", row, rhs) != 0)
        grbError(lp);
}

static void addRows(pddl_lp_t *_lp, int cnt, const double *rhs, const char *sense)
{
    lp_t *lp = LP(_lp);
    int i;
    char *gsense;

    gsense = ALLOC_ARR(char, cnt);
    for (i = 0; i < cnt; ++i)
        gsense[i] = lpSense(sense[i]);

    if (GRBaddconstrs(lp->model, cnt, 0, NULL, NULL, NULL,
                gsense, (double *)rhs, NULL) != 0){
        FREE(gsense);
        grbError(lp);
    }
    GRBupdatemodel(lp->model);
    FREE(gsense);
}

static void delRows(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    int i, j, *ind;

    ind = ALLOC_ARR(int, end - begin + 1);
    for (j = 0, i = begin; i <= end; ++i, ++j)
        ind[j] = i;

    if (GRBdelconstrs(lp->model, end - begin + 1, ind) != 0){
        FREE(ind);
        grbError(lp);
    }
    FREE(ind);
}

static int numRows(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    int rows;
    if (GRBgetintattr(lp->model, "NumConstrs", &rows) != 0)
        grbError(lp);
    return rows;
}

static void addCols(pddl_lp_t *_lp, int cnt)
{
    lp_t *lp = LP(_lp);

    if (GRBaddvars(lp->model, cnt, 0, NULL, NULL, NULL,
                NULL, NULL, NULL, NULL, NULL) != 0){
        grbError(lp);
    }
    GRBupdatemodel(lp->model);
}

static void delCols(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    int i, j, *ind;

    ind = ALLOC_ARR(int, end - begin + 1);
    for (j = 0, i = begin; i <= end; ++i, ++j)
        ind[j] = i;

    if (GRBdelvars(lp->model, end - begin + 1, ind) != 0){
        FREE(ind);
        grbError(lp);
    }
    GRBupdatemodel(lp->model);
    FREE(ind);
}

static int numCols(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    int cols;
    if (GRBgetintattr(lp->model, "NumVars", &cols) != 0)
        grbError(lp);
    return cols;
}

static int lpSolve(pddl_lp_t *_lp, double *val, double *obj)
{
    lp_t *lp = LP(_lp);
    int st, i, cols;

    if (GRBoptimize(lp->model) != 0)
        grbError(lp);
    if (GRBgetintattr(lp->model, "Status", &st) != 0)
        grbError(lp);

    if (st == GRB_OPTIMAL){
        if (val != NULL){
            if (GRBgetdblattr(lp->model, "ObjVal", val) != 0)
                grbError(lp);
        }
        if (obj != NULL){
            if (GRBgetintattr(lp->model, "NumVars", &cols) != 0)
                grbError(lp);
            for (i = 0; i < cols; ++i){
                if (GRBgetdblattrelement(lp->model, "X", i, obj + i) != 0)
                    grbError(lp);
            }
        }
        return 0;
    }else{
        if (obj != NULL){
            if (GRBgetintattr(lp->model, "NumVars", &cols) != 0)
                grbError(lp);
            bzero(obj, sizeof(double) * cols);
        }
        if (val != NULL)
            *val = 0.;
        return -1;
    }
}

static void lpWrite(pddl_lp_t *_lp, const char *fn)
{
    lp_t *lp = LP(_lp);
    if (GRBwrite(lp->model, fn) != 0)
        grbError(lp);
}

#define TOSTR1(x) #x
#define TOSTR(x) TOSTR1(x)
pddl_lp_cls_t pddl_lp_gurobi = {
    PDDL_LP_GUROBI,
    "gurobi",
    TOSTR(GRB_VERSION_MAJOR.GRB_VERSION_MINOR.GRB_VERSION_TECHNICAL),
    new,
    del,
    setObj,
    setVarRange,
    setVarFree,
    setVarInt,
    setVarBinary,
    setCoef,
    setRHS,
    addRows,
    delRows,
    numRows,
    addCols,
    delCols,
    numCols,
    lpSolve,
    lpWrite,
};
#else /* PDDL_GUROBI */
pddl_lp_cls_t pddl_lp_gurobi;
#endif /* PDDL_GUROBI */
