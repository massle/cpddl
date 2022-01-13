/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_HOMOMORPHISM_H__
#define __PDDL_HOMOMORPHISM_H__

#include <pddl/pddl_struct.h>
#include <pddl/endomorphism.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define PDDL_HOMOMORPHISM_TYPES          1
#define PDDL_HOMOMORPHISM_RAND_OBJS      2
#define PDDL_HOMOMORPHISM_RAND_TYPE_OBJS 3
#define PDDL_HOMOMORPHISM_GAIFMAN        4
#define PDDL_HOMOMORPHISM_RPG            5
struct pddl_homomorphism_config {
    int type;
    int use_endomorphism; /*!< If true, interleave collapsing with lifted
                               endomorphisms */
    pddl_endomorphism_config_t endomorphism_cfg;
    bor_iset_t collapse_types; /*!< Set of types to collapse each to a
                                    single object */
    float rm_ratio; /*!< Ratio of objects that should be removed
                         -- for *_RAND_* types */
    uint32_t random_seed;
    int keep_goal_objs;
    int rpg_max_depth;
};
typedef struct pddl_homomorphism_config pddl_homomorphism_config_t;

#define PDDL_HOMOMORPHISM_CONFIG_INIT { \
        PDDL_HOMOMORPHISM_RAND_OBJS, /* .type */ \
        0, /* .use_endomorphism */ \
        PDDL_ENDOMORPHISM_CONFIG_INIT, /* .endomorphism_cfg */ \
        BOR_ISET_INIT, /*. collapse_types */ \
        0.5, /* .rm_ratio */ \
        6899, /* .random_seed */ \
        1, /* .keep_goal_objs */ \
        2, /* .rpg_max_depth */ \
    }

void pddlHomomorphismConfigLog(const pddl_homomorphism_config_t *cfg,
                               const char *prefix,
                               bor_err_t *err);

/**
 * Computes a homomorphism image of src according to the given config.
 */
int pddlHomomorphism(pddl_t *homo_image,
                     const pddl_t *src,
                     const pddl_homomorphism_config_t *cfg,
                     pddl_obj_id_t *obj_map,
                     bor_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_HOMOMORPHISM_H__ */
