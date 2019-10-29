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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_HPOT_H__
#define __PDDL_HPOT_H__

#include <pddl/pot.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_hpot {
    double **pot; /*!< Potentials */
    int pot_size;
    int var_size;
};
typedef struct pddl_hpot pddl_hpot_t;

#define PDDL_HPOT_OBJ_INIT 0x1
#define PDDL_HPOT_OBJ_ALL_STATES 0x2
#define PDDL_HPOT_OBJ_SAMPLES_MAX 0x3
#define PDDL_HPOT_OBJ_SAMPLES_SUM 0x4
#define PDDL_HPOT_OBJ_ALL_STATES_MUTEX 0x5

struct pddl_hpot_config {
    int disambiguation;
    int weak_disambiguation;
    int obj;
    int add_init_constr;
    double init_constr_coef;
    int num_samples;
    int samples_use_mutex;
    int all_states_mutex_size;
};
typedef struct pddl_hpot_config pddl_hpot_config_t;

#define PDDL_HPOT_CONFIG_INIT { \
        1, /* .disambiguation */ \
        0, /* .weak_disambiguation */ \
        PDDL_HPOT_OBJ_ALL_STATES, /* .obj */ \
        1, /* .add_init_constr */ \
        1., /* .init_constr_coef */ \
        1000, /* .num_samples */ \
        0, /* .samples_use_mutex */ \
        0, /* .all_states_mutex_size */ \
    }

int pddlHPotInit(pddl_hpot_t *hpot,
                 const pddl_fdr_t *fdr,
                 const pddl_hpot_config_t *cfg,
                 bor_err_t *err);

void pddlHPotFree(pddl_hpot_t *hpot);

/**
 * Returns heuristic estimate for the given FDR state.
 */
int pddlHPotFDRStateEstimate(const pddl_hpot_t *hpot,
                             const pddl_fdr_vars_t *vars,
                             const int *state);

/**
 * Same as pddlHPotFDRStateEstimate() but no rounding is used.
 */
double pddlHPotFDRStateEstimateDbl(const pddl_hpot_t *hpot,
                                   const pddl_fdr_vars_t *vars,
                                   const int *state);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_HPOT_H__ */
