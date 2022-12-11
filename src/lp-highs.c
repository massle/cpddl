/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/lp.h"
#include "_lp.h"

#ifdef PDDL_HIGHS
#include <interfaces/highs_c_api.h>

#define PDDL_LP_MIN_BOUND -1E20
#define PDDL_LP_MAX_BOUND 1E20

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
    int mip;
};
typedef struct _lp_t lp_t;

#define LP(l) pddl_container_of((l), lp_t, cls)


static pddl_lp_t *new(const pddl_lp_config_t *cfg, pddl_err_t *err)
{
    lp_t *lp = ZALLOC(lp_t);
    lp->cls.cls = &pddl_lp_highs;
    lp->cls.err = err;
    lp->cls.cfg = *cfg;
    lp->mip = 0;
    return &lp->cls;
}

static void del(pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    // TODO
    FREE(lp);
}

static void setObj(pddl_lp_t *_lp, int i, double coef)
{
    lp_t *lp = LP(_lp);
    PANIC_IF_FMT(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    lp->col[i].obj = coef;
}

static void setVarRange(pddl_lp_t *_lp, int i, double lb, double ub)
{
    lp_t *lp = LP(_lp);
    PANIC_IF_FMT(i < 0 || i >= lp->col_size, "Column %d out of range", i);
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
    PANIC_IF_FMT(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    lp->col[i].type = PDDL_LP_COL_TYPE_INT;
    lp->mip = 1;
}

static void setVarBinary(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    PANIC_IF_FMT(i < 0 || i >= lp->col_size, "Column %d out of range", i);
    lp->col[i].type = PDDL_LP_COL_TYPE_BINARY;
    lp->mip = 1;
}

static void setCoef(pddl_lp_t *_lp, int row, int col, double coef)
{
    lp_t *lp = LP(_lp);
    PANIC_IF_FMT(row < 0 || row >= lp->row_size, "Row %d out of range", row);
    PANIC_IF_FMT(col < 0 || col >= lp->col_size, "Column %d out of range", col);
    pddl_lp_row_t *r = lp->row + row;
    if (r->coef_size == r->coef_alloc){
        if (r->coef_alloc == 0)
            r->coef_alloc = 8;
        r->coef_alloc *= 2;
        r->coef = REALLOC_ARR(r->coef, pddl_lp_coef_t, r->coef_alloc);
    }
    pddl_lp_coef_t *c = r->coef + r->coef_size++;
    c->col = col;
    c->coef = coef;
}

static void setRHS(pddl_lp_t *_lp, int row, double rhs, char sense)
{
    lp_t *lp = LP(_lp);
    PANIC_IF_FMT(row < 0 || row >= lp->row_size, "Row %d out of range", row);
    if (sense == 'L'){
        lp->row[row].lb = rhs;
        lp->row[row].ub = PDDL_LP_MAX_BOUND;

    }else if (sense == 'G'){
        lp->row[row].lb = PDDL_LP_MIN_BOUND;
        lp->row[row].ub = rhs;

    }else if (sense == 'E'){
        lp->row[row].lb = rhs;
        lp->row[row].ub = rhs;

    }else{
        PANIC_IF_FMT(1, "Unkown sense '%c'", sense);
    }
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
    //lp_t *lp = LP(_lp);
    // TODO
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
}

static int numCols(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return lp->col_size;
}

static int solve(pddl_lp_t *_lp, double *val, double *obj)
{
    //lp_t *lp = LP(_lp);
    // TODO

    // setOptionValue('threads', 1)
    // setOptionValue('time_limit', 1.7)
    // setOptionValue("output_flag", false)
    return -1;
}

static void cpxWrite(pddl_lp_t *_lp, const char *fn)
{
    //lp_t *lp = LP(_lp);
    // TODO
}



#define TOSTR1(x) #x
#define TOSTR(x) TOSTR1(x)
pddl_lp_cls_t pddl_lp_highs = {
    PDDL_LP_HIGHS,
    "HiGHS",
    TOSTR(HIGHS_VERSION_MAJOR.HIGHS_VERSION_MINOR.HIGHS_VERSION_PATCH),
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
#else /* PDDL_HIGHS */
pddl_lp_cls_t pddl_lp_highs = { 0 };
#endif /* PDDL_HIGHS */
