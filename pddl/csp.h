/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>
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

#ifndef __PDDL_CSP_H__
#define __PDDL_CSP_H__

#include <pddl/err.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define PDDL_CSP_FOUND 0
#define PDDL_CSP_FOUND_SOLUTION 0
#define PDDL_CSP_NO_SOLUTION -1
#define PDDL_CSP_REACHED_LIMIT -2
#define PDDL_CSP_ABORTED -3
#define PDDL_CSP_UNKNOWN -10

/** Forward declaration */
typedef struct pddl_csp pddl_csp_t;

struct pddl_csp_config {
    int num_threads;
    float max_search_time;
};
typedef struct pddl_csp_config pddl_csp_config_t;

#define PDDL_CSP_CONFIG_INIT \
    { \
        1, /* .num_threads */ \
        -1.f, /* .max_search_time */ \
    }


/**
 * Creates a new CSP problem.
 */
pddl_csp_t *pddlCSPNew(const pddl_csp_config_t *cfg, pddl_err_t *err);

/**
 * Deletes the CSP object.
 */
void pddlCSPDel(pddl_csp_t *csp);

/**
 * Adds integer variable and returns its ID
 */
int pddlCSPAddVarInt(pddl_csp_t *csp,
                     int min_val,
                     int max_val,
                     const char *name);

/**
 * Adds constraint restricting the domain of tuples of integer variables
 * to the given values.
 * tuple_size is the size of each variable tuple.
 * num_var_tuples is the number of variable tuples that are restricted.
 * num_val_tuples is the number of value tuples.
 * The size of var must be tuple_size * num_var_tuples.
 * The size of val must be tuple_size * num_val_tuples.
 */
int pddlCSPAddDomainInt(pddl_csp_t *csp,
                        int tuple_size,
                        int num_var_tuples,
                        int num_val_tuples,
                        const int *var,
                        const int *val);

/**
 * Adds equality constraint on the given integer variable.
 */
int pddlCSPAddEqInt(pddl_csp_t *csp, int var_id, int value);

/**
 * Adds objective that minimizes number of different values among the given
 * variables.
 */
int pddlCSPAddObjMinCountDifferent(pddl_csp_t *csp,
                                   int var_size,
                                   const int *var);

/**
 * Get value of the integer variable.
 */
int pddlCSPGetValInt(pddl_csp_t *csp, int var_id);

/**
 * Solve the given problem. Repeated call returns next solution.
 * See PDDL_CSP_* return values above.
 */
int pddlCSPSolve(pddl_csp_t *csp, pddl_err_t *err);

/**
 * Dump model into the given file.
 */
void pddlCSPDump(pddl_csp_t *csp, const char *fn);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_CSP_H__ */
