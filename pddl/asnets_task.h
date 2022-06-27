/***
 * Copyright (c)2016 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_ASNETS_TASK_H__
#define __PDDL_ASNETS_TASK_H__

#include <pddl/pddl_struct.h>
#include <pddl/strips.h>
#include <pddl/fdr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_asnets_task_action {
    int action_id;
    pddl_cond_arr_t atom;
};
typedef struct pddl_asnets_task_action pddl_asnets_task_action_t;

struct pddl_asnets_task_relate {
    int op_id;
    int fact_id;
    int position;
};
typedef struct pddl_asnets_task_relate pddl_asnets_task_relate_t;

struct pddl_asnets_task_relatedness {
    pddl_asnets_task_relate_t *rel;
    int rel_size;
    int rel_alloc;
};
typedef struct pddl_asnets_task_relatedness pddl_asnets_task_relatedness_t;

struct pddl_asnets_task {
    pddl_t pddl;
    pddl_strips_t strips;
    pddl_fdr_t fdr;

    pddl_asnets_task_action_t *pddl_action;
    pddl_asnets_task_relatedness_t relatedness;
};
typedef struct pddl_asnets_task pddl_asnets_task_t;

int pddlASNetsTaskInit(pddl_asnets_task_t *task,
                       const char *domain_fn,
                       const char *problem_fn,
                       pddl_err_t *err);

void pddlASNetsTaskFree(pddl_asnets_task_t *task);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_ASNETS_TASK_H__ */
