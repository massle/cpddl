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
#include <pddl/mg_strips.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_pot_constr {
    bor_iset_t plus;
    bor_iset_t minus;
    int rhs;
};
typedef struct pddl_pot_constr pddl_pot_constr_t;

struct pddl_pot_constrs {
    pddl_pot_constr_t *c;
    int size;
    int alloc;
};
typedef struct pddl_pot_constrs pddl_pot_constrs_t;

struct pddl_pot {
    int var_size; /*!< Number of LP variables */
    double *obj; /*!< Objective function coeficients */
    // TODO: Deduplicate constraints using hashtable
    pddl_pot_constrs_t constr_op; /*!< Operator constraints */
    pddl_pot_constrs_t constr_goal; /*!< Goal constraint */

    bor_segmarr_t *maxpot;
    int maxpot_size;
    bor_htable_t *maxpot_htable; /*!< Set of LP variables grouped into maxpot */

    int *fdr_var_offset; /*!< LP variable ID = .fdr_var_offset[var] + val */
};
typedef struct pddl_pot pddl_pot_t;

/**
 * Initialize potential heuristic with FDR planning task.
 */
void pddlPotInitFDR(pddl_pot_t *pot, const pddl_fdr_t *fdr);

/**
 * Initialize potential heuristic with mg-strips task and disambiguation.
 * If the task is detected to be unsolvable, -1 is returned.
 * Returns 0 on success.
 */
int pddlPotInitMGStrips(pddl_pot_t *pot,
                        const pddl_mg_strips_t *mg_strips,
                        const pddl_mutex_pairs_t *mutex);

/**
 * Free allocated memory.
 */
void pddlPotFree(pddl_pot_t *pot);

/**
 * Set objective function to the given state.
 * This will work only if {pot} was initialized with *InitFDR()
 */
void pddlPotSetObjFDRState(pddl_pot_t *pot,
                           const pddl_fdr_vars_t *vars,
                           const int *state);

/**
 * Set objective function to all syntactic states.
 * This will work only if {pot} was initialized with *InitFDR()
 */
void pddlPotSetObjFDRAllSyntacticStates(pddl_pot_t *pot,
                                        const pddl_fdr_vars_t *vars);

/**
 * Set objective function to the given state.
 * This works only if {pot} was initialized with *InitMGStrips()
 */
void pddlPotSetObjStripsState(pddl_pot_t *pot, const bor_iset_t *state);


/**
 * Solve the LP problem and returns potentials via {w}.
 * Return 0 on success, -1 if solution was not found.
 */
int pddlPotSolve(const pddl_pot_t *pot, double *w, int var_size, int use_ilp);

void pddlPotMGStripsPrintLP(const pddl_pot_t *pot,
                            const pddl_mg_strips_t *mg_strips,
                            FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_POT_H__ */
