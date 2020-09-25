/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
 * AI Center, Department of Computer Science,
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

#ifndef __PDDL_SYMBOLIC_VARS_H__
#define __PDDL_SYMBOLIC_VARS_H__

#include <pddl/bdd.h>
#include <pddl/mgroup.h>

struct pddl_symbolic_fact_group {
    int id;
    bor_iset_t fact;
    bor_iset_t pre_var;
    bor_iset_t eff_var;
};
typedef struct pddl_symbolic_fact_group pddl_symbolic_fact_group_t;

struct pddl_symbolic_fact {
    int id;
    int group_id;
    int val;
    pddl_bdd_t *pre_bdd;
    pddl_bdd_t *eff_bdd;
};
typedef struct pddl_symbolic_fact pddl_symbolic_fact_t;

struct pddl_symbolic_vars {
    int group_size;
    pddl_symbolic_fact_group_t *group;
    int fact_size;
    pddl_symbolic_fact_t *fact;
    pddl_bdd_manager_t *mgr;
    pddl_bdd_t *valid_states;
    int bdd_var_size;
};
typedef struct pddl_symbolic_vars pddl_symbolic_vars_t;

void pddlSymbolicVarsInit(pddl_symbolic_vars_t *vars,
                          int fact_size,
                          const pddl_mgroups_t *mgroups);
void pddlSymbolicVarsInitBDD(pddl_bdd_manager_t *mgr,
                             pddl_symbolic_vars_t *vars);
void pddlSymbolicVarsFree(pddl_symbolic_vars_t *vars);

pddl_bdd_t *pddlSymbolicVarsCreateState(pddl_symbolic_vars_t *vars,
                                        const bor_iset_t *state);

pddl_bdd_t *pddlSymbolicVarsCreatePartialState(pddl_symbolic_vars_t *vars,
                                               const bor_iset_t *part_state);

pddl_bdd_t *pddlSymbolicVarsCreateBiimp(pddl_symbolic_vars_t *vars,
                                        int group_id);

pddl_bdd_t *pddlSymbolicVarsCreateMutexPre(pddl_symbolic_vars_t *vars,
                                           int fact1, int fact2);

pddl_bdd_t *pddlSymbolicVarsCreateExactlyOneMGroupPre(pddl_symbolic_vars_t *vars,
                                                      const bor_iset_t *mgroup);

int pddlSymbolicVarsFactFromBDDCube(const pddl_symbolic_vars_t *vars,
                                    int group_id,
                                    const char *cube);

#endif /* __PDDL_SYMBOLIC_VARS_H__ */
