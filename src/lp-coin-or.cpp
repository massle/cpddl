/***
 * Copyright (c)2023 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/lp.h"
#include "pddl/libs_info.h"
#include "_lp.h"

#ifndef PDDL_COIN_OR
# error "Missing Coin-Or library!"
#endif

#if defined(__clang__) && __clang_major__ >= 11
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif
#include <OsiConfig.h>
#include <ClpConfig.h>
#include <CbcConfig.h>
#include <OsiSolverInterface.hpp>
#include <OsiClpSolverInterface.hpp>
#include <CbcModel.hpp>

#define MIN_BOUND -1E15
#define MAX_BOUND 1E15

const char * const pddl_coin_or_version =
    "Osi-" OSI_VERSION ":Clp-" CLP_VERSION ":Cbc-" CBC_VERSION;

class MessageHandler : public CoinMessageHandler {
    pddl_err_t *err;
  public:
    MessageHandler(pddl_err_t *err)
        : err(err)
    {
        setLogLevel(0);
    }

    int print()
    {
        LOG(err, "%s: %s", messageBuffer(), currentSource().c_str());
        return 0;
    }
};

pddl_lp_status_t pddlLPSolveCoinOr(const pddl_lp_t *lp,
                                   pddl_lp_solution_t *sol,
                                   pddl_err_t *err)
{
    CTX_NO_TIME(err, "LP-Coin-Or");
    LOG(err, "version: %s", pddl_coin_or_version);
    LOG(err, "problem: cols: %d, rows: %d, maximize: %b, time_limit: %.2f,"
        " tune-int-op-pot: %b",
        lp->col_size, lp->row_size, lp->cfg.maximize, lp->cfg.time_limit,
        lp->cfg.tune_int_operator_potential);
    pddl_timer_t timer;
    pddlTimerStart(&timer);

    if (lp->cfg.time_limit > 0.)
        WARN(err, "coin-or is able to apply time limit only on the MIP solver.");

    _pddlLPSolutionInit(sol, lp);

    MessageHandler msg_handler(err);
    OsiClpSolverInterface solver;
    solver.passInMessageHandler(&msg_handler);

    if (lp->cfg.maximize){
        solver.setObjSense(-1);
    }else{
        solver.setObjSense(1);
    }

    pddl_bool_t is_mip = pddl_false;
    for (int ci = 0; ci < lp->col_size; ++ci){
        double lb = lp->col[ci].lb;
        double ub = lp->col[ci].ub;

        if (lb <= PDDL_LP_MIN_BOUND)
            lb = MIN_BOUND;
        if (ub >= PDDL_LP_MAX_BOUND)
            ub = MAX_BOUND;

        pddl_bool_t is_int = pddl_false;
        if (lp->col[ci].type == PDDL_LP_COL_TYPE_INT){
            is_int = pddl_true;
            is_mip = pddl_true;

        }else if (lp->col[ci].type == PDDL_LP_COL_TYPE_BINARY){
            is_int = pddl_true;
            is_mip = pddl_true;
            lb = 0.;
            ub = 1.;
        }
        solver.addCol(0, NULL, NULL, lb, ub, lp->col[ci].obj);
        if (is_int)
            solver.setInteger(ci);
    }

    for (int ri = 0; ri < lp->row_size; ++ri){
        if (lp->row[ri].coef_size == 0)
            continue;

        double lhs, rhs;
        switch (lp->row[ri].sense){
            case 'L':
                lhs = MIN_BOUND;
                rhs = lp->row[ri].rhs;
                break;
            case 'G':
                lhs = lp->row[ri].rhs;
                rhs = MAX_BOUND;
                break;
            case 'E':
                lhs = rhs = lp->row[ri].rhs;
                break;
        }

        int *cols = ALLOC_ARR(int, lp->row[ri].coef_size);
        double *coef = ALLOC_ARR(double, lp->row[ri].coef_size);
        for (int c = 0; c < lp->row[ri].coef_size; ++c){
            cols[c] = lp->row[ri].coef[c].col;
            coef[c] = lp->row[ri].coef[c].coef;
        }
        solver.addRow(lp->row[ri].coef_size, cols, coef, lhs, rhs);

        FREE(cols);
        FREE(coef);
    }

    solver.initialSolve();
    if (solver.isAbandoned()){
        sol->not_solved = pddl_true;

    }else if (solver.isProvenPrimalInfeasible()
                || solver.isProvenDualInfeasible()){
        sol->unsolvable = pddl_true;

    }else if (is_mip){
        CbcModel model(solver);
        model.passInMessageHandler(&msg_handler);
        if (lp->cfg.time_limit > 0.){
            pddlTimerStop(&timer);
            double time_limit = lp->cfg.time_limit - pddlTimerElapsedInSF(&timer);
            if (time_limit <= 0.)
                time_limit = 0.1;
            model.setMaximumSeconds(time_limit);
        }

        model.branchAndBound();
        if (model.solver()->isAbandoned()){
            sol->not_solved = pddl_true;

        }else if (model.isProvenInfeasible()){
            sol->unsolvable = pddl_true;

        }else if (model.isProvenOptimal()){
            sol->solved = pddl_true;
            sol->solved_optimally = pddl_true;

        }else if (model.isSolutionLimitReached()
                    || model.solver()->isPrimalObjectiveLimitReached()
                    || model.solver()->isDualObjectiveLimitReached()
                    || model.solver()->isIterationLimitReached()
                    || model.status() != 0
                    || model.maximumSecondsReached()){
            if (model.maximumSecondsReached())
                sol->timed_out = pddl_true;

            if (model.bestSolution() != NULL){
                sol->solved = pddl_true;
                sol->solved_suboptimally = pddl_true;

            }else{
                sol->not_solved = pddl_true;
            }
        }

        if (sol->solved){
            sol->obj_val = model.getObjValue();

            if (sol->var_val != NULL){
                const double *s = model.bestSolution();
                memcpy(sol->var_val, s, sizeof(double) * lp->col_size);
            }
        }

    }else{
        if (solver.isProvenOptimal()){
            sol->solved = pddl_true;
            sol->solved_optimally = pddl_true;

        }else if (solver.isProvenPrimalInfeasible()
                    || solver.isProvenDualInfeasible()){
            sol->unsolvable = pddl_true;

        }else if (solver.isPrimalObjectiveLimitReached()
                    || solver.isDualObjectiveLimitReached()
                    || solver.isIterationLimitReached()){
            sol->not_solved = pddl_true;
        }

        if (sol->solved){
            sol->obj_val = solver.getObjValue();

            if (sol->var_val != NULL){
                const double *s = solver.getColSolution();
                memcpy(sol->var_val, s, sizeof(double) * lp->col_size);
            }
        }
    }

    CTXEND(err);
    return _pddlLPSolutionToStatus(sol);
}
