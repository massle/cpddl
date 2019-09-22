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

#ifndef __PDDL_FDR_OP_H__
#define __PDDL_FDR_OP_H__

#include <pddl/fdr_part_state.h>
#include <pddl/mgroup.h>
#include <pddl/mutex_pair.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_fdr_op_cond_eff {
    pddl_fdr_part_state_t pre;
    pddl_fdr_part_state_t eff;
};
typedef struct pddl_fdr_op_cond_eff pddl_fdr_op_cond_eff_t;

struct pddl_fdr_op {
    char *name;
    int cost;
    pddl_fdr_part_state_t pre;
    pddl_fdr_part_state_t eff;
    pddl_fdr_op_cond_eff_t *cond_eff;
    int cond_eff_size;
    int cond_eff_alloc;

    int id;
};
typedef struct pddl_fdr_op pddl_fdr_op_t;

struct pddl_fdr_ops {
    pddl_fdr_op_t **op;
    int op_size;
    int op_alloc;
};
typedef struct pddl_fdr_ops pddl_fdr_ops_t;


/**
 * Allocate empty FDR operator
 */
pddl_fdr_op_t *pddlFDROpNewEmpty(void);

/**
 * Free allocated memory
 */
void pddlFDROpDel(pddl_fdr_op_t *op);

/**
 * Adds empty conditional effect
 */
pddl_fdr_op_cond_eff_t *pddlFDROpAddEmptyCondEff(pddl_fdr_op_t *op);


/**
 * Initialize empty set of operators.
 */
void pddlFDROpsInit(pddl_fdr_ops_t *ops);

/**
 * Free allocated memory.
 */
void pddlFDROpsFree(pddl_fdr_ops_t *ops);

/**
 * Adds the given operator to the list of operators.
 */
void pddlFDROpsAddSteal(pddl_fdr_ops_t *ops, pddl_fdr_op_t *op);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_FDR_OP_H__ */
