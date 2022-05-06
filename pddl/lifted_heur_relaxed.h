/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_LIFTED_HEUR_RELAXED_H__
#define __PDDL_LIFTED_HEUR_RELAXED_H__

#include <pddl/datalog.h>
#include <pddl/prep_action.h>
#include <pddl/strips_maker.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_lifted_heur_relaxed {
    const pddl_t *pddl;
    pddl_prep_actions_t prep_action;
    pddl_datalog_t *dl;
    unsigned *type_to_dlpred;
    unsigned *pred_to_dlpred;
    unsigned *obj_to_dlconst;
    unsigned *dlvar;
    int dlvar_size;
    unsigned goal_dlpred;
};
typedef struct pddl_lifted_heur_relaxed pddl_lifted_heur_relaxed_t;

void pddlLiftedHeurRelaxedInit(pddl_lifted_heur_relaxed_t *h,
                               const pddl_t *pddl,
                               pddl_err_t *err);
void pddlLiftedHeurRelaxedFree(pddl_lifted_heur_relaxed_t *h);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_LIFTED_HEUR_RELAXED_H__ */
