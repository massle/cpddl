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

#ifndef __PDDL_INVERTIBILITY_H__
#define __PDDL_INVERTIBILITY_H__

#include <pddl/mg_strips.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_invertible_mgroup {
    bor_iset_t mgroup; /*!< Mutex group formed by invertible facts */
    int mgroup_id; /*!< ID of the corresponding mutex group */
};
typedef struct pddl_invertible_mgroup pddl_invertible_mgroup_t;

struct pddl_invertible_mgroups {
    pddl_invertible_mgroup_t *mgroup;
    int mgroup_size;
    int mgroup_alloc;
    bor_iset_t invertible_fact; /*!< All invertible facts */
};
typedef struct pddl_invertible_mgroups pddl_invertible_mgroups_t;

int pddlInvertibleMGroupsFind(pddl_invertible_mgroups_t *invmgs,
                              const pddl_strips_t *strips,
                              const pddl_mgroups_t *mgroups,
                              const pddl_mutex_pairs_t *mutex,
                              bor_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_INVERTIBILITY_H__ */
