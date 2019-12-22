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

#ifndef __PDDL_FDR_H__
#define __PDDL_FDR_H__

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

int pddlFDRInitFromStrips(pddl_fdr_t *fdr,
                          const pddl_strips_t *strips,
                          const pddl_mgroups_t *mg,
                          const pddl_mutex_pairs_t *mutex,
                          unsigned fdr_var_flags,
                          bor_err_t *err);
void pddlFDRInitCopy(pddl_fdr_t *fdr, const pddl_fdr_t *fdr_in);
void pddlFDRFree(pddl_fdr_t *fdr);

/**
 * Delete the specified facts and operators.
 */
void pddlFDRReduce(pddl_fdr_t *fdr,
                   const bor_iset_t *del_vars,
                   const bor_iset_t *del_facts,
                   const bor_iset_t *del_ops);

/**
 * Initialize FDR as the Transition Normal Form of fdr_in.
 * If mg and mutex is non-NULL disambiguation is used.
 * If prevail_to_eff is true, then also prevails are copied to the
 * effects, i.e., the true TNF is constructed, but operators are not well
 * formed.
 * Fact and operator IDs are preserved from fdr_in.
 */
void pddlFDRInitTransitionNormalForm(pddl_fdr_t *fdr,
                                     const pddl_fdr_t *fdr_in,
                                     const pddl_mgroups_t *mg,
                                     const pddl_mutex_pairs_t *mutex,
                                     int prevail_to_eff,
                                     bor_err_t *err);

void pddlFDRPrintFD(const pddl_fdr_t *fdr,
                    const pddl_mgroups_t *mgs,
                    FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_FDR_H__ */
