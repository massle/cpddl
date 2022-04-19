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

#include <stdlib.h>
#include <stdio.h>
#include "pddl/lp.h"
#include "_lp.h"

#define SOLVER(flags) ((flags) & 0xf0u)


int pddlLPSolverAvailable(unsigned solver)
{
    if (SOLVER(solver) == PDDL_LP_CPLEX){
#ifdef PDDL_CPLEX
        return 1;
#else /* PDDL_CPLEX */
        return 0;
#endif /* PDDL_CPLEX */
    }else if (SOLVER(solver) == PDDL_LP_GUROBI){
#ifdef PDDL_GUROBI
        return 1;
#else /* PDDL_GUROBI */
        return 0;
#endif /* PDDL_GUROBI */
    }else if (SOLVER(solver) == PDDL_LP_LPSOLVE){
#ifdef PDDL_LPSOLVE
        return 1;
#else /* PDDL_LPSOLVE */
        return 0;
#endif /* PDDL_LPSOLVE */
    }
#ifdef PDDL_CPLEX
        return 1;
#else /* PDDL_CPLEX */
#ifdef PDDL_GUROBI
        return 1;
#else /* PDDL_GUROBI */
#ifdef PDDL_LPSOLVE
        return 1;
#else /* PDDL_LPSOLVE */
        return 0;
#endif /* PDDL_LPSOLVE */
#endif /* PDDL_GUROBI */
#endif /* PDDL_CPLEX */
}

pddl_lp_t *pddlLPNew(int rows, int cols, unsigned flags, pddl_err_t *err)
{
    if (SOLVER(flags) == PDDL_LP_CPLEX){
#ifdef PDDL_CPLEX
        return pddl_lp_cplex.new(rows, cols, flags, err);
#else /* PDDL_CPLEX */
        return pddl_lp_not_available.new(rows, cols, flags, err);
#endif /* PDDL_CPLEX */
    }else if (SOLVER(flags) == PDDL_LP_GUROBI){
#ifdef PDDL_GUROBI
        return pddl_lp_gurobi.new(rows, cols, flags, err);
#else /* PDDL_GUROBI */
        return pddl_lp_not_available.new(rows, cols, flags, err);
#endif /* PDDL_GUROBI */
    }else if (SOLVER(flags) == PDDL_LP_LPSOLVE){
#ifdef PDDL_LPSOLVE
        return pddl_lp_lpsolve.new(rows, cols, flags, err);
#else /* PDDL_LPSOLVE */
        return pddl_lp_not_available.new(rows, cols, flags, err);
#endif /* PDDL_LPSOLVE */
    }

#ifdef PDDL_CPLEX
    return pddl_lp_cplex.new(rows, cols, flags, err);
#else /* PDDL_CPLEX */
#ifdef PDDL_GUROBI
    return pddl_lp_gurobi.new(rows, cols, flags, err);
#else /* PDDL_GUROBI */
#ifdef PDDL_LPSOLVE
    return pddl_lp_lpsolve.new(rows, cols, flags, err);
#else /* PDDL_LPSOLVE */
    return pddl_lp_not_available.new(rows, cols, flags, err);
#endif /* PDDL_LPSOLVE */
#endif /* PDDL_GUROBI */
#endif /* PDDL_CPLEX */
}

void pddlLPDel(pddl_lp_t *lp)
{
    lp->cls->del(lp);
}

const char *pddlLPSolverName(const pddl_lp_t *lp)
{
    return lp->cls->solver_name;
}

int pddlLPSolverID(const pddl_lp_t *lp)
{
    return lp->cls->solver_id;
}

void pddlLPSetObj(pddl_lp_t *lp, int i, double coef)
{
    lp->cls->set_obj(lp, i, coef);
}

void pddlLPSetVarRange(pddl_lp_t *lp, int i, double lb, double ub)
{
    lp->cls->set_var_range(lp, i, lb, ub);
}

void pddlLPSetVarFree(pddl_lp_t *lp, int i)
{
    lp->cls->set_var_free(lp, i);
}

void pddlLPSetVarInt(pddl_lp_t *lp, int i)
{
    lp->cls->set_var_int(lp, i);
}

void pddlLPSetVarBinary(pddl_lp_t *lp, int i)
{
    lp->cls->set_var_binary(lp, i);
}

void pddlLPSetCoef(pddl_lp_t *lp, int row, int col, double coef)
{
    lp->cls->set_coef(lp, row, col, coef);
}

void pddlLPSetRHS(pddl_lp_t *lp, int row, double rhs, char sense)
{
    lp->cls->set_rhs(lp, row, rhs, sense);
}

void pddlLPAddRows(pddl_lp_t *lp, int cnt, const double *rhs, const char *sense)
{
    lp->cls->add_rows(lp, cnt, rhs, sense);
}

void pddlLPDelRows(pddl_lp_t *lp, int begin, int end)
{
    lp->cls->del_rows(lp, begin, end);
}

int pddlLPNumRows(const pddl_lp_t *lp)
{
    return lp->cls->num_rows(lp);
}

void pddlLPAddCols(pddl_lp_t *lp, int cnt)
{
    lp->cls->add_cols(lp, cnt);
}

void pddlLPDelCols(pddl_lp_t *lp, int begin, int end)
{
    lp->cls->del_cols(lp, begin, end);
}

int pddlLPNumCols(const pddl_lp_t *lp)
{
    return lp->cls->num_cols(lp);
}

int pddlLPSolve(pddl_lp_t *lp, double *val, double *obj)
{
    return lp->cls->solve(lp, val, obj);
}

void pddlLPWrite(pddl_lp_t *lp, const char *fn)
{
    lp->cls->write(lp, fn);
}




#define noSolverExit() \
    do { \
    fprintf(stderr, "Error: The requested LP solver is not available!\n"); \
    exit(-1); \
    } while (0)
static pddl_lp_t *noNew(int rows, int cols, unsigned flags, pddl_err_t *err)
{ noSolverExit(); }
static void noDel(pddl_lp_t *lp)
{ noSolverExit(); }
static void noSetObj(pddl_lp_t *lp, int i, double coef)
{ noSolverExit(); }
static void noSetVarRange(pddl_lp_t *lp, int i, double lb, double ub)
{ noSolverExit(); }
static void noSetVarFree(pddl_lp_t *lp, int i)
{ noSolverExit(); }
static void noSetVarInt(pddl_lp_t *lp, int i)
{ noSolverExit(); }
static void noSetVarBinary(pddl_lp_t *lp, int i)
{ noSolverExit(); }
static void noSetCoef(pddl_lp_t *lp, int row, int col, double coef)
{ noSolverExit(); }
static void noSetRHS(pddl_lp_t *lp, int row, double rhs, char sense)
{ noSolverExit(); }
static void noAddRows(pddl_lp_t *lp, int cnt, const double *rhs, const char *sense)
{ noSolverExit(); }
static void noDelRows(pddl_lp_t *lp, int begin, int end)
{ noSolverExit(); }
static int noNumRows(const pddl_lp_t *lp)
{ noSolverExit(); }
static void noAddCols(pddl_lp_t *lp, int cnt)
{ noSolverExit(); }
static void noDelCols(pddl_lp_t *lp, int begin, int end)
{ noSolverExit(); }
static int noNumCols(const pddl_lp_t *lp)
{ noSolverExit(); }
static int noSolve(pddl_lp_t *lp, double *val, double *obj)
{ noSolverExit(); }
static void noWrite(pddl_lp_t *lp, const char *fn)
{ noSolverExit(); }

pddl_lp_cls_t pddl_lp_not_available = {
    0, "",
    noNew,
    noDel,
    noSetObj,
    noSetVarRange,
    noSetVarFree,
    noSetVarInt,
    noSetVarBinary,
    noSetCoef,
    noSetRHS,
    noAddRows,
    noDelRows,
    noNumRows,
    noAddCols,
    noDelCols,
    noNumCols,
    noSolve,
    noWrite,
};
