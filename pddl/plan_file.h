/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_PLAN_FILE_H__
#define __PDDL_PLAN_FILE_H__

#include <boruvka/iarr.h>
#include <pddl/fdr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_search;

struct pddl_plan_file_fdr {
    bor_iarr_t op; /*!< Sequence of operators */
    int **state; /*!< Intermediate states */
    int state_size;
    int state_alloc;
    int cost; /*!< Cost of the plan */
};
typedef struct pddl_plan_file_fdr pddl_plan_file_fdr_t;


/**
 * Reads plan file corresponding to the given FDR planning task.
 * Returns 0 on success.
 */
int pddlPlanFileFDRInit(pddl_plan_file_fdr_t *p,
                        const pddl_fdr_t *fdr,
                        const char *filename,
                        bor_err_t *err);

/**
 * Free allocated memory.
 */
void pddlPlanFileFDRFree(pddl_plan_file_fdr_t *p);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_PLAN_FILE_H__ */
