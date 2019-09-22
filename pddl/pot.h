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

#ifndef __PDDL_POT_H__
#define __PDDL_POT_H__

#include <boruvka/htable.h>
#include <boruvka/segmarr.h>
#include <pddl/fdr.h>
#include <pddl/strips.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_pot_constr {
    bor_iset_t plus;
    bor_iset_t minus;
    int rhs;
};
typedef struct pddl_pot_constr pddl_pot_constr_t;

struct pddl_pot {
    int var_size; /*!< Number of LP variables */
    double *obj; /*!< Objective function coeficients */
    pddl_pot_constr_t *constr_op; /*!< Operator constraints */
    int constr_op_size;
    int constr_op_alloc;
    pddl_pot_constr_t constr_goal; /*!< Goal constraint */
    pddl_pot_constr_t *constr_maxpot; /*!< Maxpot constraints */
    int constr_maxpot_size;
    int constr_maxpot_alloc;

    bor_segmarr_t *maxpot;
    int maxpot_size;
    bor_htable_t *maxpot_htable; /*!< Set of LP variables grouped into maxpot */

    int *fdr_var_offset; /*!< LP variable ID = .fdr_var_offset[var] + val */
};
typedef struct pddl_pot pddl_pot_t;

void pddlPotInitFDR(pddl_pot_t *pot, const pddl_fdr_t *fdr);
void pddlPotFree(pddl_pot_t *pot);

void pddlPotSetObjFDRState(pddl_pot_t *pot,
                           const pddl_fdr_vars_t *vars,
                           const int *state);
void pddlPotSetObjFDRAllSyntacticStates(pddl_pot_t *pot,
                                        const pddl_fdr_vars_t *vars);


int pddlPotSolve(const pddl_pot_t *pot, double *w, int var_size, int use_ilp);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_POT_H__ */
