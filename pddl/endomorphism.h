/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_ENDOMORPHISM_H__
#define __PDDL_ENDOMORPHISM_H__

#include <pddl/fdr.h>
#include <pddl/mg_strips.h>
#include <pddl/trans_system.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_endomorphism_config {
    /** Maximal overall time in seconds. (default: 1 hour) */
    float max_time;
    /** Maximal search time in seconds. (default: 1 hour) */
    float max_search_time;
    /** Maximal number of worker threads. (default: 1) */
    int num_threads;
    /** If set to true, the inference will run in a separate sub-process.
     *  (default: true) */
    int run_in_subprocess;
};
typedef struct pddl_endomorphism_config pddl_endomorphism_config_t;

#define PDDL_ENDOMORPHISM_CONFIG_INIT \
    { 3600.f, /* .max_time */ \
      3600.f, /* .max_search_time */ \
      1, /* .num_threads */ \
      1, /* .run_in_subprocess */ \
    }

int pddlEndomorphismFDRRedundantOps(const pddl_fdr_t *fdr,
                                    const pddl_endomorphism_config_t *cfg,
                                    bor_iset_t *redundant_ops,
                                    bor_err_t *err);

int pddlEndomorphismMGStripsRedundantOps(const pddl_mg_strips_t *mg_strips,
                                         const pddl_endomorphism_config_t *cfg,
                                         bor_iset_t *redundant_ops,
                                         bor_err_t *err);

int pddlEndomorphismTransSystemRedundantOps(const pddl_trans_systems_t *tss,
                                            const pddl_endomorphism_config_t *c,
                                            bor_iset_t *redundant_ops,
                                            bor_err_t *err);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_ENDOMORPHISM_H__ */
