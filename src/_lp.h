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

#ifndef __PDDL__LP_H__
#define __PDDL__LP_H__

#include "pddl/config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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
    double rhs;
    char sense; // 'L', 'G', 'E'
};
typedef struct pddl_lp_row pddl_lp_row_t;

struct pddl_lp {
    pddl_err_t *err; // TODO: Remove this one
    pddl_lp_config_t cfg;
    pddl_lp_col_t *col;
    int col_size;
    int col_alloc;
    pddl_lp_row_t *row;
    int row_size;
    int row_alloc;
};

void _pddlLPSolutionInit(pddl_lp_solution_t *sol, const pddl_lp_t *lp);
pddl_lp_status_t _pddlLPSolutionToStatus(const pddl_lp_solution_t *sol);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL__LP_H__ */
