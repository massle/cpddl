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

//#define PDDL_LP_MIN_BOUND -1E20
#define PDDL_LP_MIN_BOUND -CPX_INFBOUND
//#define PDDL_LP_MAX_BOUND 1E20
#define PDDL_LP_MAX_BOUND CPX_INFBOUND

enum pddl_lp_col_type {
    PDDL_LP_COL_TYPE_REAL,
    PDDL_LP_COL_TYPE_INT,
    PDDL_LP_COL_TYPE_BINARY,
};
typedef enum pddl_lp_col_type pddl_lp_col_type_t;

struct pddl_lp_col {
    double obj;
    pddl_lp_col_type_t type;
    double lb;
    double ub;
};
typedef struct pddl_lp_col pddl_lp_col_t;

struct pddl_lp_coef {
    int col;
    double coef;
};
typedef struct pddl_lp_coef pddl_lp_coef_t;

struct pddl_lp_row {
    pddl_lp_coef_t *coef;
    int coef_size;
    int coef_alloc;
    double lb;
    double ub;
    char sense;
};
typedef struct pddl_lp_row pddl_lp_row_t;

struct _lp_t {
    pddl_lp_t cls;
    pddl_lp_col_t *col;
    int col_size;
    int col_alloc;
    pddl_lp_row_t *row;
    int row_size;
    int row_alloc;
};
typedef struct _lp_t lp_t;

#define LP(l) pddl_container_of((l), lp_t, cls)

static void addCols(pddl_lp_t *_lp, int cnt);
static void addRow(lp_t *lp, const double rhs, const char sense);

static void freeRow(pddl_lp_row_t *row)
{
    if (row->coef != NULL)
        FREE(row->coef);
}


static pddl_lp_t *new(const pddl_lp_config_t *cfg, pddl_err_t *err)
{
    lp_t *lp = ZALLOC(lp_t);
    lp->cls.cls = &pddl_lp_cplex;
    lp->cls.err = err;
    lp->cls.cfg = *cfg;

    if (cfg->cols > 0)
        addCols(&lp->cls, cfg->cols);
    if (cfg->rows > 0){
        for (int i = 0; i < cfg->rows; ++i)
            addRow(lp, 0., 'L');
    }
    return &lp->cls;
}

static void del(pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    if (lp->col != NULL)
        FREE(lp->col);
    for (int ri = 0; ri < lp->row_size; ++ri)
        freeRow(lp->row + ri);
    if (lp->row != NULL)
        FREE(lp->row);
    FREE(lp);
}

static void setObj(pddl_lp_t *_lp, int i, double coef)
{
    lp_t *lp = LP(_lp);
    PANIC_IF(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    lp->col[i].obj = coef;
}

static void setVarRange(pddl_lp_t *_lp, int i, double lb, double ub)
{
    lp_t *lp = LP(_lp);
    PANIC_IF(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    if (lb <= PDDL_LP_MIN_BOUND)
        lb = PDDL_LP_MIN_BOUND;
    if (ub >= PDDL_LP_MAX_BOUND)
        ub = PDDL_LP_MAX_BOUND;
    lp->col[i].lb = lb;
    lp->col[i].ub = ub;
}

static void setVarFree(pddl_lp_t *_lp, int i)
{
    setVarRange(_lp, i, PDDL_LP_MIN_BOUND, PDDL_LP_MAX_BOUND);
}

static void setVarInt(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    PANIC_IF(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    lp->col[i].type = PDDL_LP_COL_TYPE_INT;
}

static void setVarBinary(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    PANIC_IF(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    lp->col[i].type = PDDL_LP_COL_TYPE_BINARY;
}

static void setCoef(pddl_lp_t *_lp, int row, int col, double coef)
{
    lp_t *lp = LP(_lp);
    PANIC_IF(row < 0 || row >= lp->row_size, "Row %d out of range", row);
    PANIC_IF(col < 0 || col >= lp->col_size, "Column %d out of range", col);
    pddl_lp_row_t *r = lp->row + row;
    if (r->coef_size == r->coef_alloc){
        if (r->coef_alloc == 0)
            r->coef_alloc = 8;
        r->coef_alloc *= 2;
        r->coef = REALLOC_ARR(r->coef, pddl_lp_coef_t, r->coef_alloc);
    }

    if (r->coef_size == 0 || r->coef[r->coef_size - 1].col < col){
        pddl_lp_coef_t *c = r->coef + r->coef_size++;
        c->col = col;
        c->coef = coef;

    }else{
        int idx;
        for (idx = r->coef_size - 1; idx >= 0; --idx){
            if (r->coef[idx].col <= col)
                break;
        }

        if (idx < 0 || r->coef[idx].col < col){
            for (int i = r->coef_size - 1; i > idx; --i)
                r->coef[i + 1] = r->coef[i];
            r->coef[idx + 1].col = col;
            r->coef[idx + 1].coef = coef;
            ++r->coef_size;

        }else{ // r->coef[idx].col == col
            r->coef[idx].coef = coef;
        }
    }

    // TODO: If coef == 0. delete coef
}

static void setRHS(pddl_lp_t *_lp, int row, double rhs, char sense)
{
    lp_t *lp = LP(_lp);
    PANIC_IF(row < 0 || row >= lp->row_size, "Row %d out of range", row);
    if (sense == 'L'){
        lp->row[row].lb = PDDL_LP_MIN_BOUND;
        lp->row[row].ub = rhs;

    }else if (sense == 'G'){
        lp->row[row].lb = rhs;
        lp->row[row].ub = PDDL_LP_MAX_BOUND;

    }else if (sense == 'E'){
        lp->row[row].lb = rhs;
        lp->row[row].ub = rhs;

    }else{
        PANIC_IF(1, "Unkown sense '%c'", sense);
    }
    lp->row[row].sense = sense;
}

static void addRow(lp_t *lp, const double rhs, const char sense)
{
    if (lp->row_size == lp->row_alloc){
        if (lp->row_alloc == 0)
            lp->row_alloc = 16;
        lp->row_alloc *= 2;
        lp->row = REALLOC_ARR(lp->row, pddl_lp_row_t, lp->row_alloc);
    }
    pddl_lp_row_t *row = lp->row + lp->row_size++;
    ZEROIZE(row);
    setRHS(&lp->cls, lp->row_size - 1, rhs, sense);
}

static void addRows(pddl_lp_t *_lp, int cnt, const double *rhs, const char *sense)
{
    lp_t *lp = LP(_lp);
    for (int i = 0; i < cnt; ++i)
        addRow(lp, rhs[i], sense[i]);
}

static void delRows(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    for (int i = begin; i < end + 1; ++i)
        freeRow(lp->row + i);
    int ins = begin;
    for (int i = end + 1; i < lp->row_size; ++i)
        lp->row[ins++] = lp->row[i];
    lp->row_size = ins;
}

static int numRows(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return lp->row_size;
}

static void addCol(lp_t *lp)
{
    if (lp->col_size == lp->col_alloc){
        if (lp->col_alloc == 0)
            lp->col_alloc = 16;
        lp->col_alloc *= 2;
        lp->col = REALLOC_ARR(lp->col, pddl_lp_col_t, lp->col_alloc);
    }
    pddl_lp_col_t *col = lp->col + lp->col_size++;
    ZEROIZE(col);
    col->obj = 0.;
    col->type = PDDL_LP_COL_TYPE_REAL;
    col->lb = PDDL_LP_MIN_BOUND;
    col->ub = PDDL_LP_MAX_BOUND;
}

static void addCols(pddl_lp_t *_lp, int cnt)
{
    lp_t *lp = LP(_lp);
    for (int i = 0; i < cnt; ++i)
        addCol(lp);
}

static void delCols(pddl_lp_t *_lp, int begin, int end)
{
    //lp_t *lp = LP(_lp);
    // TODO
    PANIC_IF(1, "Deleting columns not implemented yet.");
}

static int numCols(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return lp->col_size;
}

static void cplexErr(CPXENVptr env, int status, const char *s)
{
    char errmsg[1024];
    CPXgeterrorstring(env, status, errmsg);
    PANIC("Error: CPLEX: %s: %s", s, errmsg);
}

#if 0
struct _lp_t {
    pddl_lp_t cls;
    CPXENVptr env;
    CPXLPptr lp;
    int mip;
    pddl_timer_t log_timer;
};
typedef struct _lp_t lp_t;

#define LP(l) pddl_container_of((l), lp_t, cls)

static void cplexErr(lp_t *lp, int status, const char *s)
{
    char errmsg[1024];
    CPXgeterrorstring(lp->env, status, errmsg);
    PANIC("Error: CPLEX: %s: %s", s, errmsg);
}

static int callback(CPXCALLBACKCONTEXTptr ctx, CPXLONG ctxtid, void *_lp)
{
    lp_t *lp = _lp;

    pddlTimerStop(&lp->log_timer);
    if (pddlTimerElapsedInSF(&lp->log_timer) < 1.)
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

    CTX_NO_TIME(lp->cls.err, "cplex progress");
    LOG(lp->cls.err, "best solution: %.2f, best bound: %.2f, feasible: %d",
        best_sol, best_bound, feasible);
    CTXEND(lp->cls.err);
    pddlTimerStart(&lp->log_timer);
    return 0;
}

static int callbackLP(CPXCENVptr env,
                      void *cbdata,
                      int wherefrom,
                      void *_lp)
{
    lp_t *lp = _lp;

    pddlTimerStop(&lp->log_timer);
    if (pddlTimerElapsedInSF(&lp->log_timer) < 1.)
        return 0;

    double primal = 0.;
    CPXgetcallbackinfo(env, cbdata, wherefrom,
                       CPX_CALLBACK_INFO_PRIMAL_OBJ, &primal);
    double dual = 0.;
    CPXgetcallbackinfo(env, cbdata, wherefrom,
                       CPX_CALLBACK_INFO_DUAL_OBJ, &dual);

    CTX_NO_TIME(lp->cls.err, "cplex progress");
    LOG(lp->cls.err, "primal: %.4f, dual: %.4f", primal, dual);
    CTXEND(lp->cls.err);
    pddlTimerStart(&lp->log_timer);
    return 0;
}

static pddl_lp_t *new(const pddl_lp_config_t *cfg, pddl_err_t *err)
{
    lp_t *lp;
    int st;

    lp = ALLOC(lp_t);
    lp->cls.cls = &pddl_lp_cplex;
    lp->cls.err = err;
    lp->cls.cfg = *cfg;
    lp->mip = 0;

    // Initialize CPLEX structures
    lp->env = CPXopenCPLEX(&st);
    if (lp->env == NULL)
        cplexErr(lp, st, "Could not open CPLEX environment");

    // Set number of processing threads
    int num_threads = PDDL_MAX(1, cfg->num_threads);
    st = CPXsetintparam(lp->env, CPX_PARAM_THREADS, num_threads);
    if (st != 0)
        cplexErr(lp, st, "Could not set number of threads");

    CPXsetintparam(lp->env, CPXPARAM_ScreenOutput, CPX_OFF);

    if (cfg->time_limit > 0.f){
        st = CPXsetdblparam(lp->env, CPXPARAM_TimeLimit, cfg->time_limit);
        if (st != 0)
            cplexErr(lp, st, "Could not set number of threads");
    }

    lp->lp = CPXcreateprob(lp->env, &st, "");
    if (lp->lp == NULL)
        cplexErr(lp, st, "Could not create CPLEX problem");

    if (cfg->maximize){
        CPXchgobjsen(lp->env, lp->lp, CPX_MAX);
    }else{
        CPXchgobjsen(lp->env, lp->lp, CPX_MIN);
    }

    st = CPXnewcols(lp->env, lp->lp, cfg->cols, NULL, NULL, NULL, NULL, NULL);
    if (st != 0)
        cplexErr(lp, st, "Could not initialize variables");

    st = CPXnewrows(lp->env, lp->lp, cfg->rows, NULL, NULL, NULL, NULL);
    if (st != 0)
        cplexErr(lp, st, "Could not initialize constraints");

    if (cfg->tune_int_operator_potential){
        CPXsetintparam(lp->env, CPXPARAM_Preprocessing_Relax, CPX_ON);
        CPXsetintparam(lp->env, CPXPARAM_Preprocessing_Dual, 1);
        //CPXsetintparam(lp->env, CPXPARAM_Preprocessing_CoeffReduce, 2);
        //CPXsetintparam(lp->env, CPXPARAM_Preprocessing_Dependency, 3);
    }

    return &lp->cls;
}

static void del(pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    if (lp->lp)
        CPXfreeprob(lp->env, &lp->lp);
    if (lp->env)
        CPXcloseCPLEX(&lp->env);
    FREE(lp);
}

static void setObj(pddl_lp_t *_lp, int i, double coef)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXchgcoef(lp->env, lp->lp, -1, i, coef);
    if (st != 0)
        cplexErr(lp, st, "Could not set objective coeficient.");
}

static void setVarRange(pddl_lp_t *_lp, int i, double lb, double ub)
{
    lp_t *lp = LP(_lp);
    if (lb <= -1E20)
        lb = -CPX_INFBOUND;
    if (ub >= 1E20)
        ub = CPX_INFBOUND;
    static const char lu[2] = { 'L', 'U' };
    double bd[2] = { lb, ub };
    int ind[2];
    int st;

    ind[0] = ind[1] = i;
    st = CPXchgbds(lp->env, lp->lp, 2, ind, lu, bd);
    if (st != 0)
        cplexErr(lp, st, "Could not set variable as free.");
}

static void setVarFree(pddl_lp_t *_lp, int i)
{
    setVarRange(_lp, i, -CPX_INFBOUND, CPX_INFBOUND);
}

static void setVarInt(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    static char type = CPX_INTEGER;
    int st;

    st = CPXchgctype(lp->env, lp->lp, 1, &i, &type);
    if (st != 0)
        cplexErr(lp, st, "Could not set variable as integer.");
    lp->mip = 1;
}

static void setVarBinary(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    static char type = CPX_BINARY;
    int st;

    st = CPXchgctype(lp->env, lp->lp, 1, &i, &type);
    if (st != 0)
        cplexErr(lp, st, "Could not set variable as binary.");
    lp->mip = 1;
}

static void setCoef(pddl_lp_t *_lp, int row, int col, double coef)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXchgcoef(lp->env, lp->lp, row, col, coef);
    if (st != 0)
        cplexErr(lp, st, "Could not set constraint coeficient.");
}

static void setRHS(pddl_lp_t *_lp, int row, double rhs, char sense)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXchgcoef(lp->env, lp->lp, row, -1, rhs);
    if (st != 0)
        cplexErr(lp, st, "Could not set right-hand-side.");

    st = CPXchgsense(lp->env, lp->lp, 1, &row, &sense);
    if (st != 0)
        cplexErr(lp, st, "Could not set right-hand-side sense.");
}

static void addRows(pddl_lp_t *_lp, int cnt, const double *rhs, const char *sense)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXnewrows(lp->env, lp->lp, cnt, rhs, sense, NULL, NULL);
    if (st != 0)
        cplexErr(lp, st, "Could not add new rows.");
}

static void delRows(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXdelrows(lp->env, lp->lp, begin, end);
    if (st != 0)
        cplexErr(lp, st, "Could not delete rows.");
}

static int numRows(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return CPXgetnumrows(lp->env, lp->lp);
}

static void addCols(pddl_lp_t *_lp, int cnt)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXnewcols(lp->env, lp->lp, cnt, NULL, NULL, NULL, NULL, NULL);
    if (st != 0)
        cplexErr(lp, st, "Could not add new columns.");
}

static void delCols(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXdelcols(lp->env, lp->lp, begin, end);
    if (st != 0)
        cplexErr(lp, st, "Could not delete columns.");
}

static int numCols(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return CPXgetnumcols(lp->env, lp->lp);
}

static int solve(pddl_lp_t *_lp, double *val, double *obj)
{
    lp_t *lp = LP(_lp);
    int st;

    pddlTimerStart(&lp->log_timer);
    if (lp->mip){
        CPXcallbacksetfunc(lp->env, lp->lp,
                           CPX_CALLBACKCONTEXT_GLOBAL_PROGRESS
                                | CPX_CALLBACKCONTEXT_LOCAL_PROGRESS
                                | CPX_CALLBACKCONTEXT_RELAXATION
                                | CPX_CALLBACKCONTEXT_CANDIDATE,
                           callback, lp);
        if ((st = CPXmipopt(lp->env, lp->lp)) != 0)
            cplexErr(lp, st, "Failed to optimize LP");
        CPXcallbacksetfunc(lp->env, lp->lp, 0, NULL, NULL);

    }else{
        CPXsetlpcallbackfunc(lp->env, callbackLP, lp);
        if ((st = CPXlpopt(lp->env, lp->lp)) != 0)
            cplexErr(lp, st, "Failed to optimize LP");
    }

    st = CPXgetstat(lp->env, lp->lp);
    if (st == CPX_STAT_OPTIMAL
            || st == CPX_STAT_OPTIMAL_INFEAS
            || st == CPXMIP_OPTIMAL
            || st == CPXMIP_OPTIMAL_TOL
            || st == CPXMIP_TIME_LIM_FEAS){
        st = CPXsolution(lp->env, lp->lp, NULL, val, obj, NULL, NULL, NULL);
        if (st != 0)
            cplexErr(lp, st, "Cannot retrieve solution");
    }else{
        if (obj != NULL){
            int cols = CPXgetnumcols(lp->env, lp->lp);
            ZEROIZE_ARR(obj, cols);
        }
        if (val != NULL)
            *val = 0.;
        return -1;
    }
    return 0;
}

static void cpxWrite(pddl_lp_t *_lp, const char *fn)
{
    lp_t *lp = LP(_lp);
    int st;

    st = CPXwriteprob(lp->env, lp->lp, fn, "LP");
    if (st != 0)
        cplexErr(lp, st, "Failed to optimize ILP");
}
#endif

static int solve(pddl_lp_t *_lp, double *val, double *obj)
{
    lp_t *lp = LP(_lp);
    int st;
    CPXENVptr env;
    CPXLPptr prob;

    env = CPXopenCPLEX(&st);
    if (env == NULL)
        cplexErr(env, st, "Could not open CPLEX environment");

    // Set number of processing threads
    int num_threads = PDDL_MAX(1, _lp->cfg.num_threads);
    st = CPXsetintparam(env, CPX_PARAM_THREADS, num_threads);
    if (st != 0)
        cplexErr(env, st, "Could not set number of threads");

    CPXsetintparam(env, CPXPARAM_ScreenOutput, CPX_OFF);

    if (_lp->cfg.time_limit > 0.f){
        st = CPXsetdblparam(env, CPXPARAM_TimeLimit, _lp->cfg.time_limit);
        if (st != 0)
            cplexErr(env, st, "Could not set number of threads");
    }

    prob = CPXcreateprob(env, &st, "");
    if (prob == NULL)
        cplexErr(env, st, "Could not create CPLEX problem");

    if (_lp->cfg.maximize){
        CPXchgobjsen(env, prob, CPX_MAX);
    }else{
        CPXchgobjsen(env, prob, CPX_MIN);
    }

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
        if (lp->row[i].sense == 'L'){
            rhs[i] = lp->row[i].ub;
            ASSERT(rhs[i] < PDDL_LP_MAX_BOUND);
        }else if (lp->row[i].sense == 'G'){
            rhs[i] = lp->row[i].lb;
            ASSERT(rhs[i] > PDDL_LP_MIN_BOUND);
        }else if (lp->row[i].sense == 'E'){
            rhs[i] = lp->row[i].lb;
            ASSERT(rhs[i] > PDDL_LP_MIN_BOUND);
        }
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

    if (_lp->cfg.tune_int_operator_potential){
        CPXsetintparam(env, CPXPARAM_Preprocessing_Relax, CPX_ON);
        CPXsetintparam(env, CPXPARAM_Preprocessing_Dual, 1);
        //CPXsetintparam(env, CPXPARAM_Preprocessing_CoeffReduce, 2);
        //CPXsetintparam(env, CPXPARAM_Preprocessing_Dependency, 3);
    }

    //pddlTimerStart(&lp->log_timer);
    if (mip){
        /*
        CPXcallbacksetfunc(env, prob,
                           CPX_CALLBACKCONTEXT_GLOBAL_PROGRESS
                                | CPX_CALLBACKCONTEXT_LOCAL_PROGRESS
                                | CPX_CALLBACKCONTEXT_RELAXATION
                                | CPX_CALLBACKCONTEXT_CANDIDATE,
                           callback, lp);
        */
        if ((st = CPXmipopt(env, prob)) != 0)
            cplexErr(env, st, "Failed to optimize MIP");
        //CPXcallbacksetfunc(lp->env, lp->lp, 0, NULL, NULL);

    }else{
        //CPXsetlpcallbackfunc(lp->env, callbackLP, lp);
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

static void cpxWrite(pddl_lp_t *_lp, const char *fn)
{
}

#define TOSTR1(x) #x
#define TOSTR(x) TOSTR1(x)
pddl_lp_cls_t pddl_lp_cplex = {
    PDDL_LP_CPLEX,
    "cplex",
    pddl_cplex_version,
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
    solve,
    cpxWrite,
};
#else /* PDDL_CPLEX */
const char * const pddl_cplex_version = NULL;
pddl_lp_cls_t pddl_lp_cplex = { 0 };
#endif /* PDDL_CPLEX */
