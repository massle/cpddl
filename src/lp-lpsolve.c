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
#include "_lp.h"

#ifdef PDDL_LPSOLVE
# include <lpsolve/lp_lib.h>

struct _lp_t {
    pddl_lp_t cls;
    lprec *lp;
};
typedef struct _lp_t lp_t;

#define LP(l) pddl_container_of((l), lp_t, cls)

static int lpSense(char sense)
{
    if (sense == 'L'){
        return LE;
    }else if (sense == 'G'){
        return GE;
    }else if (sense == 'E'){
        return EQ;
    }else{
        fprintf(stderr, "LP Error: Unkown sense: %c\n", sense);
        return EQ;
    }
}

static pddl_lp_t *new(const pddl_lp_config_t *cfg, pddl_err_t *err)
{
    lp_t *lp;

    lp = ALLOC(lp_t);
    lp->cls.cls = &pddl_lp_lpsolve;
    lp->cls.err = err;
    lp->cls.cfg = *cfg;
    lp->lp = make_lp(cfg->rows, cfg->cols);
    if (cfg->maximize){
        set_maxim(lp->lp);
    }else{
        set_minim(lp->lp);
    }

    return &lp->cls;
}

static void del(pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    delete_lp(lp->lp);
    FREE(lp);
}

static void setObj(pddl_lp_t *_lp, int i, double coef)
{
    lp_t *lp = LP(_lp);
    set_obj(lp->lp, i + 1, coef);
}

static void setVarRange(pddl_lp_t *_lp, int i, double lb, double ub)
{
    lp_t *lp = LP(_lp);
    set_lowbo(lp->lp, i + 1, lb);
    set_upbo(lp->lp, i + 1, ub);
}

static void setVarFree(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    set_unbounded(lp->lp, i + 1);
}

static void setVarInt(pddl_lp_t *_lp, int i)
{
    lp_t *lp = LP(_lp);
    set_int(lp->lp, i + 1, 1);
}

static void setVarBinary(pddl_lp_t *_lp, int i)
{
    setVarRange(_lp, i, 0, 1);
    setVarInt(_lp, i);
}

static void setCoef(pddl_lp_t *_lp, int row, int col, double coef)
{
    lp_t *lp = LP(_lp);
    set_mat(lp->lp, row + 1, col + 1, coef);
}

static void setRHS(pddl_lp_t *_lp, int row, double rhs, char sense)
{
    lp_t *lp = LP(_lp);
    set_rh(lp->lp, row + 1, rhs);
    set_constr_type(lp->lp, row + 1, lpSense(sense));
}

static void addRows(pddl_lp_t *_lp, int cnt, const double *rhs, const char *sense)
{
    lp_t *lp = LP(_lp);
    int i, vsen = EQ;
    double vrhs = 0.;

    for (i = 0; i < cnt; ++i){
        if (rhs)
            vrhs = rhs[i];
        if (sense)
            vsen = lpSense(sense[i]);

        add_constraintex(lp->lp, 0, NULL, NULL, vsen, vrhs);
    }
}

static void delRows(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    int i;

    for (i = begin; i <= end; ++i)
        del_constraint(lp->lp, begin + 1);
}

static int numRows(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return get_Nrows(lp->lp);
}

static void addCols(pddl_lp_t *_lp, int cnt)
{
    lp_t *lp = LP(_lp);
    int i;

    double *col = CALLOC_ARR(double, get_Nrows(lp->lp) + 1);
    for (i = 0; i < cnt; ++i)
        add_column(lp->lp, col);
    if (col != NULL)
        FREE(col);
}

static void delCols(pddl_lp_t *_lp, int begin, int end)
{
    lp_t *lp = LP(_lp);
    int i, size;

    size = end - begin + 1;
    size = PDDL_MIN(size, get_Ncolumns(lp->lp));
    for (i = 0; i < size; ++i){
        del_column(lp->lp, begin);
    }
}

static int numCols(const pddl_lp_t *_lp)
{
    lp_t *lp = LP(_lp);
    return get_Ncolumns(lp->lp);
}

static int lpSolve(pddl_lp_t *_lp, double *val, double *obj)
{
    lp_t *lp = LP(_lp);
    int ret;

    set_verbose(lp->lp, NEUTRAL);
    ret = solve(lp->lp);
    if (ret == OPTIMAL || ret == SUBOPTIMAL){
        if (val != NULL)
            *val = get_objective(lp->lp);
        if (obj != NULL)
            get_variables(lp->lp, obj);

        return 0;

    }else if (ret == NUMFAILURE){
        // FIXME: Sometimes "numerical failure" is encountered and running
        // it once more helps, but I'm not sure why!
        ret = solve(lp->lp);
        if (ret == OPTIMAL || ret == SUBOPTIMAL){
            if (val != NULL)
                *val = get_objective(lp->lp);
            if (obj != NULL)
                get_variables(lp->lp, obj);

            return 0;
        }
    }

    if (obj != NULL){
        int cols = get_Ncolumns(lp->lp);
        ZEROIZE_ARR(obj, cols);
    }
    if (val != NULL)
        *val = 0.;
    return -1;
}

static void lpWrite(pddl_lp_t *_lp, const char *fn)
{
    lp_t *lp = LP(_lp);
    write_lp(lp->lp, (char *)fn);
}

#define TOSTR1(x) #x
#define TOSTR(x) TOSTR1(x)
pddl_lp_cls_t pddl_lp_lpsolve = {
    PDDL_LP_LPSOLVE,
    "lpsolve",
    TOSTR(MAJORVERSION.MINORVERSION),
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
#else /* PDDL_LPSOLVE */
pddl_lp_cls_t pddl_lp_lpsolve = { 0 };
#endif /* PDDL_LPSOLVE */
