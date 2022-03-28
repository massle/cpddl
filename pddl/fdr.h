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
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_FDR_H__
#define __PDDL_FDR_H__

#include <boruvka/iarr.h>
#include <pddl/fdr_var.h>
#include <pddl/fdr_op.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_fdr {
    pddl_fdr_vars_t var;
    pddl_fdr_ops_t op;
    int *init;
    pddl_fdr_part_state_t goal;
    int goal_is_unreachable;
    int has_cond_eff;
};
typedef struct pddl_fdr pddl_fdr_t;

#define PDDL_FDR_SET_NONE_OF_THOSE_IN_PRE 0x1

int pddlFDRInitFromStrips(pddl_fdr_t *fdr,
                          const pddl_strips_t *strips,
                          const pddl_mgroups_t *mg,
                          const pddl_mutex_pairs_t *mutex,
                          unsigned fdr_var_flags,
                          unsigned fdr_flags,
                          bor_err_t *err);
void pddlFDRInitCopy(pddl_fdr_t *fdr, const pddl_fdr_t *fdr_in);
void pddlFDRFree(pddl_fdr_t *fdr);

/**
 * Reorder variables using causal graph
 */
void pddlFDRReorderVarsCG(pddl_fdr_t *fdr);

/**
 * Delete the specified facts and operators.
 */
void pddlFDRReduce(pddl_fdr_t *fdr,
                   const pddl_iset_t *del_vars,
                   const pddl_iset_t *del_facts,
                   const pddl_iset_t *del_ops);

/**
 * Returns true if the plan is a relaxed plan of the problem.
 */
int pddlFDRIsRelaxedPlan(const pddl_fdr_t *fdr,
                         const int *fdr_state,
                         const bor_iarr_t *plan,
                         bor_err_t *err);


/** Prevail conditions are copied to effects -- this creates a true TNF,
 *  but it can produce operators that are not well-formed */
#define PDDL_FDR_TNF_PREVAIL_TO_EFF 0x1
/** Takes effect only if mutex argument is non-NULL.
 *  Use weak type of disambiguation. */
#define PDDL_FDR_TNF_WEAK_DISAMBIGUATION 0x2
/** Multiply operators instead of creating forgetting operators */
#define PDDL_FDR_TNF_MULTIPLY_OPS 0x4

/**
 * Initialize FDR as the Transition Normal Form of fdr_in.
 * If mutex is non-NULL disambiguation is used.
 * {flags} must be or'ed PDDL_FDR_TNF_* flags.
 * Fact IDs are preserved from fdr_in and operator IDs are preserved unless
 * there are unreachable operators that are removed.
 */
int pddlFDRInitTransitionNormalForm(pddl_fdr_t *fdr,
                                    const pddl_fdr_t *fdr_in,
                                    const pddl_mutex_pairs_t *mutex,
                                    unsigned flags,
                                    bor_err_t *err);

void pddlFDRPrintFD(const pddl_fdr_t *fdr,
                    const pddl_mgroups_t *mgs,
                    int use_fd_fact_names,
                    FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_FDR_H__ */
