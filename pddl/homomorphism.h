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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_homomorphism_config {
    bor_iset_t collapse_types; /*!< Set of types to collapse each to a
                                    single object */
};
typedef struct pddl_homomorphism_config pddl_homomorphism_config_t;

#define PDDL_HOMOMORPHISM_CONFIG_INIT { 0 }

/**
 * Computes a homomorphism image of src according to the given config.
 */
int pddlHomomorphism(pddl_t *homo_image,
                     const pddl_t *src,
                     const pddl_homomorphism_config_t *cfg,
                     bor_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_HOMOMORPHISM_H__ */
