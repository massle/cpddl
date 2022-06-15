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

#ifndef __PDDL_LP_H__
#define __PDDL_LP_H__

#include <pddl/err.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Forward declaration */
typedef struct _pddl_lp_t pddl_lp_t;

/**
 * Solvers.
 * Not all may be available (TODO)
 */
#define PDDL_LP_DEFAULT 0x0000u
#define PDDL_LP_CPLEX   0x0010u
#define PDDL_LP_GUROBI  0x0020u
#define PDDL_LP_LPSOLVE 0x0030u
#define PDDL_LP_GLPK    0x0040u

/**
 * Sets the number of parallel threads that will be invoked by a
 * parallel optimizer.
 * By default one thread is used.
 * Set num threads to -1 to switch to auto mode.
 */
#define PDDL_LP_NUM_THREADS(num) \
    ((((unsigned)(num)) & 0x3fu) << 8u)

/**
 * Sets minimization (default).
 */
#define PDDL_LP_MIN 0x0u

/**
 * Sets maximization.
 */
#define PDDL_LP_MAX 0x1u


/**
 * Tune for finding integer operator potentials.
 */
#define PDDL_LP_TUNE_INT_OPERATOR_POTENTIAL 0x1u

/**
 * Returns true if the specified solver is available.
 * For PDDL_LP_DEFAULT returns false if there is no LP solver available.
 */
int pddlLPSolverAvailable(unsigned solver);

/**
 * Set default solver.
 */
int pddlLPSetDefault(unsigned solver, pddl_err_t *err);

/**
 * Creates a new LP problem with specified number of rows and columns.
 * TODO: Use pddl_lp_config_t instead of rows, cols, flags
 */
pddl_lp_t *pddlLPNew(int rows, int cols, unsigned flags, pddl_err_t *err);

/**
 * Deletes the LP object.
 */
void pddlLPDel(pddl_lp_t *lp);

/**
 * Returns name of the current LP solver.
 */
const char *pddlLPSolverName(const pddl_lp_t *lp);

/**
 * Returns one of PDDL_LP_{CPLEX,GUROBI,LPSOLVE} constants according to the
 * current solver.
 */
int pddlLPSolverID(const pddl_lp_t *lp);

/**
 * Sets objective coeficient for i'th variable.
 */
void pddlLPSetObj(pddl_lp_t *lp, int i, double coef);

/**
 * Sets i'th variable's range.
 */
void pddlLPSetVarRange(pddl_lp_t *lp, int i, double lb, double ub);

/**
 * Sets i'th variable as free.
 */
void pddlLPSetVarFree(pddl_lp_t *lp, int i);

/**
 * Sets i'th variable as integer.
 */
void pddlLPSetVarInt(pddl_lp_t *lp, int i);

/**
 * Sets i'th variable as binary.
 */
void pddlLPSetVarBinary(pddl_lp_t *lp, int i);

/**
 * Sets coefficient for row's constraint and col's variable.
 */
void pddlLPSetCoef(pddl_lp_t *lp, int row, int col, double coef);

/**
 * Sets right hand side of the row'th constraint.
 * Also sense of the constraint must be defined:
 *      - 'L' <=
 *      - 'G' >=
 *      - 'E' =
 */
void pddlLPSetRHS(pddl_lp_t *lp, int row, double rhs, char sense);

/**
 * Adds cnt rows to the model.
 */
void pddlLPAddRows(pddl_lp_t *lp, int cnt, const double *rhs, const char *sense);

/**
 * Deletes rows with indexes between begin and end including both limits,
 * i.e., first deleted row has index {begin} the last deleted row has index
 * {end}.
 */
void pddlLPDelRows(pddl_lp_t *lp, int begin, int end);

/**
 * Returns number of rows in model.
 */
int pddlLPNumRows(const pddl_lp_t *lp);

/**
 * Adds cnt columns to the model.
 */
void pddlLPAddCols(pddl_lp_t *lp, int cnt);

/**
 * Deletes columns with indexes between begin and end including both
 * limits, i.e., first deleted column has index {begin} the last deleted
 * column has index {end}.
 */
void pddlLPDelCols(pddl_lp_t *lp, int begin, int end);

/**
 * Returns number of columns in model.
 */
int pddlLPNumCols(const pddl_lp_t *lp);

/**
 * Solves (I)LP problem.
 * Return 0 if problem was solved, -1 if the problem has no solution.
 * Objective value is returned via argument val and values of each variable
 * via argument obj if non-NULL.
 */
int pddlLPSolve(pddl_lp_t *lp, double *val, double *obj);


void pddlLPWrite(pddl_lp_t *lp, const char *fn);

/**
 * Tune parameters for the (I)LP problem based on the given PDDL_LP_TUNE_* flag.
 */
void pddlLPTune(pddl_lp_t *lp, unsigned flag);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_LP_H__ */
