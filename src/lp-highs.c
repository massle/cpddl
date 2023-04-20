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
#define COMPRESSED_INT HighsInt
#define COMPRESSED_COL_TYPE HighsInt
#include "_lp_compressed_row_problem.h"

const char * const pddl_highs_version =
    PDDL_TOSTR(HIGHS_VERSION_MAJOR.HIGHS_VERSION_MINOR.HIGHS_VERSION_PATCH);

#define TOLERANCE 1E-5
#define MIP_TOLERANCE 1E-5
#define MIN_BOUND -1E20
#define MAX_BOUND 1E20


static void *createModel(pddl_lp_t *lp)
{
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

    pddl_lp_compressed_row_problem_t P;
    compressedRowProblemInit(&P, lp, 
                             kHighsVarTypeContinuous,
                             kHighsVarTypeInteger,
                             kHighsVarTypeInteger,
                             pddl_true,
                             MIN_BOUND,
                             MAX_BOUND);

    int sense = kHighsObjSenseMinimize;
    if (lp->cfg.maximize)
        sense = kHighsObjSenseMaximize;

    int a_format = kHighsMatrixFormatRowwise;
    double offset = 0.;

    HighsInt st = 0;
    if (P.is_mip){
        st = Highs_passMip(model, P.num_col, P.num_row, P.num_nz, a_format,
                           sense, offset, P.col_obj, P.col_lb, P.col_ub,
                           P.row_lb, P.row_ub, P.row_beg, P.row_ind, P.row_val,
                           P.col_type);
    }else{
        st = Highs_passLp(model, P.num_col, P.num_row, P.num_nz, a_format,
                          sense, offset, P.col_obj, P.col_lb, P.col_ub,
                          P.row_lb, P.row_ub, P.row_beg, P.row_ind, P.row_val);
    }

    compressedRowProblemFree(&P);

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
