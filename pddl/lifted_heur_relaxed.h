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
#include <pddl/strips_maker.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_lifted_heur_relaxed {
    const pddl_t *pddl;
    pddl_datalog_t *dl;
    unsigned *type_to_dlpred;
    unsigned *pred_to_dlpred;
    unsigned *obj_to_dlconst;
    unsigned *dlvar;
    int dlvar_size;
    unsigned goal_dlpred;
    pddl_bool_t collect_best_achiever_facts;

    /** Internal context for computing ff heuristic */
    void *ff_ctx;
};
typedef struct pddl_lifted_heur_relaxed pddl_lifted_heur_relaxed_t;
typedef struct pddl_lifted_heur_relaxed pddl_lifted_hmax_t;
typedef struct pddl_lifted_heur_relaxed pddl_lifted_hadd_t;
typedef struct pddl_lifted_heur_relaxed pddl_lifted_hff_max_t;
typedef struct pddl_lifted_heur_relaxed pddl_lifted_hff_add_t;

void pddlLiftedHMaxInit(pddl_lifted_hmax_t *h,
                        const pddl_t *pddl,
                        pddl_bool_t collect_best_achiever_facts,
                        pddl_err_t *err);
void pddlLiftedHMaxFree(pddl_lifted_hmax_t *h);
pddl_cost_t pddlLiftedHMax(pddl_lifted_hmax_t *h,
                           const pddl_iset_t *state,
                           const pddl_ground_atoms_t *gatoms);
void pddlLiftedHMaxBestAchieverFacts(pddl_lifted_hmax_t *h,
                                     const pddl_ground_atoms_t *gatoms,
                                     pddl_iset_t *achievers);

void pddlLiftedHAddInit(pddl_lifted_hadd_t *h,
                        const pddl_t *pddl,
                        pddl_bool_t collect_best_achiever_facts,
                        pddl_err_t *err);
void pddlLiftedHAddFree(pddl_lifted_hadd_t *h);
pddl_cost_t pddlLiftedHAdd(pddl_lifted_hadd_t *h,
                           const pddl_iset_t *state,
                           const pddl_ground_atoms_t *gatoms);
void pddlLiftedHAddBestAchieverFacts(pddl_lifted_hadd_t *h,
                                     const pddl_ground_atoms_t *gatoms,
                                     pddl_iset_t *achievers);

void pddlLiftedHFFAddInit(pddl_lifted_hadd_t *h,
                          const pddl_t *pddl,
                          pddl_err_t *err);
void pddlLiftedHFFAddFree(pddl_lifted_hff_add_t *h);
pddl_cost_t pddlLiftedHFFAdd(pddl_lifted_hff_add_t *h,
                             const pddl_iset_t *state,
                             const pddl_ground_atoms_t *gatoms);

void pddlLiftedHFFMaxInit(pddl_lifted_hadd_t *h,
                          const pddl_t *pddl,
                          pddl_err_t *err);
void pddlLiftedHFFMaxFree(pddl_lifted_hff_add_t *h);
pddl_cost_t pddlLiftedHFFMax(pddl_lifted_hff_add_t *h,
                             const pddl_iset_t *state,
                             const pddl_ground_atoms_t *gatoms);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_LIFTED_HEUR_RELAXED_H__ */
