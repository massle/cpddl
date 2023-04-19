/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/lp.h"
#include "pddl/libs_info.h"
#include "_lp.h"

#ifdef PDDL_HIGHS
#include <interfaces/highs_c_api.h>

const char * const pddl_highs_version =
    PDDL_TOSTR(HIGHS_VERSION_MAJOR.HIGHS_VERSION_MINOR.HIGHS_VERSION_PATCH);

#define TOLERANCE 1E-5
#define MIP_TOLERANCE 1E-5
#define MIN_BOUND -1E20
#define MAX_BOUND 1E20


static void *createModel(pddl_lp_t *lp)
{
    int num_col = lp->col_size;
    int num_row = lp->row_size;
    int num_nz = 0;
    for (int ri = 0; ri < lp->row_size; ++ri)
        num_nz += lp->row[ri].coef_size;

    int sense = kHighsObjSenseMinimize;
    if (lp->cfg.maximize)
        sense = kHighsObjSenseMaximize;

    double offset = 0.;
    double *col_cost = ALLOC_ARR(double, num_col);
    double *col_lower = ALLOC_ARR(double, num_col);
    double *col_upper = ALLOC_ARR(double, num_col);
    int is_mip = 0;
    for (int i = 0; i < lp->col_size; ++i){
        col_cost[i] = lp->col[i].obj;
        col_lower[i] = lp->col[i].lb;
        col_upper[i] = lp->col[i].ub;
        if (lp->col[i].type == PDDL_LP_COL_TYPE_BINARY){
            if (col_lower[i] < 0.){
                col_lower[i] = 0.;
            }else if (col_lower[i] > 1.){
                col_lower[i] = 1.;
            }

            if (col_upper[i] > 1.){
                col_upper[i] = 1.;
            }else if (col_upper[i] < 0.){
                col_upper[i] = 0.;
            }
        }
        if (lp->col[i].type == PDDL_LP_COL_TYPE_BINARY
                || lp->col[i].type == PDDL_LP_COL_TYPE_INT){
            is_mip = 1;
        }
    }

    double *row_lower = ALLOC_ARR(double, num_row);
    double *row_upper = ALLOC_ARR(double, num_row);
    for (int i = 0; i < lp->row_size; ++i){
        switch (lp->row[i].sense){
            case 'L':
                row_lower[i] = MIN_BOUND;
                row_upper[i] = lp->row[i].rhs;
                break;
            case 'G':
                row_lower[i] = lp->row[i].rhs;
                row_upper[i] = MAX_BOUND;
                break;
            case 'E':
                row_lower[i] = lp->row[i].rhs;
                row_upper[i] = lp->row[i].rhs;
                break;
            default:
                PANIC("Unkown row sense '%c'", lp->row[i].sense);
                break;
        }
    }

    int a_format = kHighsMatrixFormatRowwise;
    HighsInt *a_start = ALLOC_ARR(HighsInt, num_row);
    HighsInt *a_index = ALLOC_ARR(HighsInt, num_nz);
    double *a_value = ALLOC_ARR(double, num_nz);

    int ins = 0;
    for (int ri = 0; ri < num_row; ++ri){
        a_start[ri] = ins;
        for (int ci = 0; ci < lp->row[ri].coef_size; ++ci){
            a_index[ins] = lp->row[ri].coef[ci].col;
            a_value[ins] = lp->row[ri].coef[ci].coef;
            ++ins;
        }
    }

    HighsInt *integrality = NULL;
    if (is_mip){
        integrality = ALLOC_ARR(HighsInt, num_col);
        for (int i = 0; i < num_col; ++i){
            switch (lp->col[i].type){
                case PDDL_LP_COL_TYPE_REAL:
                    integrality[i] = kHighsVarTypeContinuous;
                    break;
                case PDDL_LP_COL_TYPE_INT:
                    integrality[i] = kHighsVarTypeInteger;
                    break;
                case PDDL_LP_COL_TYPE_BINARY:
                    integrality[i] = kHighsVarTypeInteger;
                    break;
            }
        }
    }

    void *model = Highs_create();
    PANIC_IF(model == NULL, "Could not create a HiGHS model.");

    int num_threads = PDDL_MAX(lp->cfg.num_threads, 1);
    Highs_setIntOptionValue(model, "threads", num_threads);
    if (lp->cfg.time_limit > 0.)
        Highs_setDoubleOptionValue(model, "time_limit", lp->cfg.time_limit);
    Highs_setBoolOptionValue(model, "output_flag", 0);
    //Highs_setIntOptionValue(model, "log_dev_level", 2);
    //Highs_setIntOptionValue(model, "highs_debug_level", 2);
    Highs_setDoubleOptionValue(model, "primal_feasibility_tolerance", TOLERANCE);
    Highs_setDoubleOptionValue(model, "dual_feasibility_tolerance", TOLERANCE);
    Highs_setDoubleOptionValue(model, "mip_feasibility_tolerance", MIP_TOLERANCE);

    HighsInt st = 0;
    if (is_mip){
        st = Highs_passMip(model, num_col, num_row, num_nz, a_format,
                           sense, offset, col_cost, col_lower, col_upper,
                           row_lower, row_upper, a_start, a_index, a_value,
                           integrality);
    }else{
        st = Highs_passLp(model, num_col, num_row, num_nz, a_format,
                          sense, offset, col_cost, col_lower, col_upper,
                          row_lower, row_upper, a_start, a_index, a_value);
    }

    FREE(col_cost);
    FREE(col_lower);
    FREE(col_upper);
    FREE(row_lower);
    FREE(row_upper);
    FREE(a_start);
    FREE(a_index);
    FREE(a_value);
    if (integrality != NULL)
        FREE(integrality);

    if (st == kHighsStatusError){
        // TODO: Not sure how to recover from this...
        return NULL;

    }else if (st == kHighsStatusWarning){
        return NULL;
    }

    //Highs_writeModel(model, "model.lp");
    return model;
}

int pddlLPSolveHiGHS(pddl_lp_t *lp, double *val, double *obj, pddl_err_t *err)
{
    int ret = 0;

    void *model = createModel(lp);
    if (model == NULL){
        LOG(err, "Something went wrong with the creation of model!");
        return -1;
    }

    HighsInt st = Highs_run(model);
    if (st == kHighsStatusError){
        LOG(err, "Something went wrong during solving the model!");
        Highs_destroy(model);
        return -1;
    }else if (st == kHighsStatusWarning){
        // TODO
    }

    HighsInt modelst = Highs_getModelStatus(model);
    if (modelst == kHighsModelStatusNotset){
        LOG(err, "Model status not set");
        ret = -1;

    }else if (modelst == kHighsModelStatusLoadError){
        LOG(err, "Model load error!");
        ret = -1;

    }else if (modelst == kHighsModelStatusModelError){
        LOG(err, "Model error!");
        ret = -1;

    }else if (modelst == kHighsModelStatusPresolveError){
        LOG(err, "Presolve error!");
        ret = -1;

    }else if (modelst == kHighsModelStatusSolveError){
        LOG(err, "Solve error!");
        ret = -1;

    }else if (modelst == kHighsModelStatusPostsolveError){
        LOG(err, "Postsolve error!");
        ret = -1;

    }else if (modelst == kHighsModelStatusModelEmpty){
        LOG(err, "Model is empty!");
        ret = -1;

    }else if (modelst == kHighsModelStatusOptimal){
        //LOG(err, "Model has optimal solution.");
        ret = 0;

    }else if (modelst == kHighsModelStatusInfeasible){
        LOG(err, "Solution is infeasible.");
        ret = -1;

    }else if (modelst == kHighsModelStatusUnboundedOrInfeasible){
        LOG(err, "Solution is unbounded or infeasible.");
        ret = -1;

    }else if (modelst == kHighsModelStatusUnbounded){
        LOG(err, "Solution is unbounded.");
        ret = -1;

    }else if (modelst == kHighsModelStatusObjectiveBound){
        LOG(err, "Bound on objective reached.");
        ret = -1;

    }else if (modelst == kHighsModelStatusObjectiveTarget){
        LOG(err, "Target for objective reached.");
        ret = -1;

    }else if (modelst == kHighsModelStatusTimeLimit){
        //LOG(err, "Time limit.");
        //ret = -1;
        ret = 0;

    }else if (modelst == kHighsModelStatusIterationLimit){
        //LOG(err, "Iteration limit.");
        //ret = -1;
        ret = 0;

    }else if (modelst == kHighsModelStatusUnknown){
        LOG(err, "Unkown solution status");
        ret = -1;

    }else{
        LOG(err, "Unkown solution status: %d", (int)modelst);
    }

    HighsInt solst;
    Highs_getIntInfoValue(model, "primal_solution_status", &solst);
    if (solst == kHighsSolutionStatusFeasible){
        if (val != NULL)
            *val = Highs_getObjectiveValue(model);
        if (obj != NULL)
            Highs_getSolution(model, obj, NULL, NULL, NULL);
        ret = 0;

    }else{
        ret = -1;
    }

    Highs_destroy(model);

    return ret;
}


#else /* PDDL_HIGHS */
const char * const pddl_highs_version = NULL;

int pddlLPSolveHiGHS(pddl_lp_t *lp, double *val, double *obj, pddl_err_t *err)
{
    PANIC("Missing HiGHS solver");
    return -1;
}
#endif /* PDDL_HIGHS */
