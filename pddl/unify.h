/***
 * cpddl
 * -------
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
 * FAI Group, Saarland University
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

#ifndef __PDDL_UNIFY_H__
#define __PDDL_UNIFY_H__

#include <pddl/cond.h>
#include <pddl/param.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_unify_val {
    pddl_obj_id_t obj;
    int var;
    int var_type;
};
typedef struct pddl_unify_val pddl_unify_val_t;

struct pddl_unify {
    const pddl_types_t *type;
    const pddl_params_t *param[2];
    pddl_unify_val_t *map[2];
    int var_size;
};
typedef struct pddl_unify pddl_unify_t;


void pddlUnifyInit(pddl_unify_t *u,
                   const pddl_types_t *type,
                   const pddl_params_t *param1,
                   const pddl_params_t *param2);
void pddlUnifyInitCopy(pddl_unify_t *u, const pddl_unify_t *u2);
void pddlUnifyFree(pddl_unify_t *u);

/**
 * Unifies a1 with a2.
 * Return 0 on success, -1 if unification wasn't possible.
 */
int pddlUnify(pddl_unify_t *u,
              const pddl_cond_atom_t *a1,
              const pddl_cond_atom_t *a2);

/**
 * Updates the mapping with the equality atoms (= x y) from cond.
 * Returns 0 on success.
 */
int pddlUnifyApplyEquality(pddl_unify_t *u,
                           const pddl_params_t *param,
                           int eq_pred,
                           const pddl_cond_t *cond);

/**
 * Returns true if the inequality conditions hold.
 */
int pddlUnifyCheckInequality(const pddl_unify_t *u,
                             const pddl_params_t *param,
                             int eq_pred,
                             const pddl_cond_t *cond);

/**
 * Returns true if u(a1) != u(a2), i.e., if a1 and a2 differ under the
 * current mapping.
 */
int pddlUnifyAtomsDiffer(const pddl_unify_t *u,
                         const pddl_params_t *param1,
                         const pddl_cond_atom_t *a1,
                         const pddl_params_t *param2,
                         const pddl_cond_atom_t *a2);

/**
 * TODO
 */
pddl_cond_t *pddlUnifyToCond(const pddl_unify_t *u,
                             int eq_pred,
                             const pddl_params_t *param);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_UNIFY_H__ */
