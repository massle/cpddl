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

#ifndef __PDDL_SYMBOLIC_TASK_H__
#define __PDDL_SYMBOLIC_TASK_H__

#include <boruvka/iarr.h>
#include <pddl/strips.h>
#include <pddl/mgroup.h>
#include <pddl/mutex_pair.h>
#include <pddl/hpot.h>
#include <pddl/bdd.h>
#include <pddl/bdds.h>
#include <pddl/symbolic_vars.h>
#include <pddl/symbolic_constr.h>
#include <pddl/symbolic_trans.h>
#include <pddl/symbolic_state.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define PDDL_SYMBOLIC_CONT 0
#define PDDL_SYMBOLIC_PLAN_FOUND 1
#define PDDL_SYMBOLIC_PLAN_NOT_EXIST 2
#define PDDL_SYMBOLIC_FAIL -1

struct pddl_symbolic_task_config {
    int max_mem_in_mb;
    int cache_size;
    size_t trans_merge_max_nodes;
    float trans_merge_max_time;
    int use_constr;
    size_t constr_max_nodes;
    float constr_max_time;
    int use_op_constr;
    float goal_constr_max_time;
    int fam_groups;

    int use_pot_heur;
    int use_pot_heur_real;
    int use_pot_heur_inconsistent;
    int use_pot_heur_sum_op_cost;
    pddl_hpot_config_t pot_heur_config;
    int use_heur_fw;
    int use_heur_bw;
};
typedef struct pddl_symbolic_task_config pddl_symbolic_task_config_t;

#define PDDL_SYMBOLIC_TASK_CONFIG_INIT \
    { \
        -1, /* .max_mem_in_mb */ \
        16000000, /* .cache_size */ \
        100000ul, /* .trans_merge_max_nodes */ \
        -1.f, /* .trans_merge_max_time */ \
        0, /* .use_constr */ \
        100000ul, /* .constr_max_nodes */ \
        -1.f, /* .constr_max_time */ \
        1, /* .use_op_constr */ \
        -1., /* .goal_constr_max_time */ \
        0, /* .fam_groups */ \
        0, /* .use_pot_heur */ \
        0, /* .use_pot_heur_real */ \
        0, /* .use_pot_heur_inconsistent */ \
        0, /* .use_pot_heur_sum_op_cost */ \
        PDDL_HPOT_CONFIG_INIT, \
        1, /* .use_heur_fw */ \
        0, /* .use_heur_bw */ \
    }

struct pddl_symbolic_task {
    pddl_symbolic_task_config_t cfg; /*!< Configuration */
    pddl_fdr_t fdr;
    pddl_mg_strips_t mg_strips;
    pddl_bdd_manager_t *mgr; /*!< Cudd manager */
    pddl_symbolic_vars_t vars; /*!< TODO */
    pddl_symbolic_trans_sets_t trans; /*!< BDD transitions */
    pddl_symbolic_constr_t constr; /*!< Constraints */
    pddl_bdd_t *init; /*!< Initial state */
    pddl_bdd_t *goal; /*!< Goal states */
    pddl_cost_t heur_init;
    int goal_constr_failed; /*!< True if applying constraints on the goal
                                 failed */
};
typedef struct pddl_symbolic_task pddl_symbolic_task_t;

pddl_symbolic_task_t *pddlSymbolicTaskNew(const pddl_fdr_t *fdr,
                                          const pddl_symbolic_task_config_t *c,
                                          bor_err_t *err);

void pddlSymbolicTaskDel(pddl_symbolic_task_t *states);

/**
 * Returns true if applying constraints on the goal failed.
 */
int pddlSymbolicTaskGoalConstrFailed(const pddl_symbolic_task_t *task);

int pddlSymbolicTaskSearchFw(pddl_symbolic_task_t *ss,
                             bor_iarr_t *plan,
                             bor_err_t *err);
int pddlSymbolicTaskSearchBw(pddl_symbolic_task_t *ss,
                             bor_iarr_t *plan,
                             bor_err_t *err);
int pddlSymbolicTaskSearchFwBw(pddl_symbolic_task_t *ss,
                               bor_iarr_t *plan,
                               bor_err_t *err);

int pddlSymbolicTaskCheckApplyFw(pddl_symbolic_task_t *ss,
                                 const int *state,
                                 const int *res_state,
                                 int op_id);
int pddlSymbolicTaskCheckApplyBw(pddl_symbolic_task_t *ss,
                                 const int *state,
                                 const int *res_state,
                                 int op_id);
int pddlSymbolicTaskCheckPlan(pddl_symbolic_task_t *ss,
                              const bor_iarr_t *op,
                              int plan_size);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SYMBOLIC_TASK_H__ */
