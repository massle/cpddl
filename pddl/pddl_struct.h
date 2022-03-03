/***
 * cpddl
 * -------
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_STRUCT_H__
#define __PDDL_STRUCT_H__

#include <pddl/config.h>
#include <pddl/lisp.h>
#include <pddl/require.h>
#include <pddl/type.h>
#include <pddl/obj.h>
#include <pddl/pred.h>
#include <pddl/fact.h>
#include <pddl/action.h>
#include <pddl/lifted_mgroup.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_config {
    int force_adl; /*!< Force ADL to requirements */
    int normalize; /*!< Normalize the task right after parsing */
    int remove_empty_types; /*!< Remove types without any objects */
    int compile_away_cond_eff; /*!< Compile away conditional effects */
    int enforce_unit_cost; /*!< Enforce the task to have unit-cost actions */
};
typedef struct pddl_config pddl_config_t;

#define PDDL_CONFIG_INIT_EMPTY { 0 }
#define PDDL_CONFIG_INIT \
    { 1, /* .force_adl */ \
      1, /* .normalize */ \
      1, /* .remove_empty_types */ \
      0, /* .compile_away_cond_eff */ \
      0, /* .enforce_unit_cost */ \
    }

void pddlConfigLog(const pddl_config_t *cfg, const char *prefix, bor_err_t *err);

struct pddl {
    pddl_config_t cfg;
    pddl_lisp_t *domain_lisp;
    pddl_lisp_t *problem_lisp;
    char *domain_name;
    char *problem_name;
    unsigned require;
    pddl_types_t type;
    pddl_objs_t obj;
    pddl_preds_t pred;
    pddl_preds_t func;
    pddl_cond_part_t *init;
    pddl_cond_t *goal;
    pddl_actions_t action;
    int metric;
    int normalized;
};

/**
 * Initialize pddl structure from the domain/problem PDDL files.
 */
int pddlInit(pddl_t *pddl, const char *domain_fn, const char *problem_fn,
             const pddl_config_t *cfg, bor_err_t *err);

/**
 * Creates a copy of the pddl structure.
 */
void pddlInitCopy(pddl_t *dst, const pddl_t *src);

/**
 * Frees allocated memory.
 */
void pddlFree(pddl_t *pddl);

pddl_t *pddlNew(const char *domain_fn, const char *problem_fn,
                const pddl_config_t *cfg, bor_err_t *err);
void pddlDel(pddl_t *pddl);

/**
 * Normalize pddl, i.e., make preconditions and effects CNF
 */
void pddlNormalize(pddl_t *pddl);

/**
 * Generate pddl without conditional effects.
 */
void pddlCompileAwayCondEff(pddl_t *pddl);

/**
 * Generate pddl without conditional effects unless the conditional effects
 * that have only static predicates in its preconditions.
 * This is enough for grounding.
 */
void pddlCompileAwayNonStaticCondEff(pddl_t *pddl);

/**
 * Returns maximal number of parameters of all predicates and functions.
 */
int pddlPredFuncMaxParamSize(const pddl_t *pddl);

/**
 * Checks pddl_*_size_t types agains the parsed pddl.
 * If any of these types is too small the program exists with error
 * message.
 */
void pddlCheckSizeTypes(const pddl_t *pddl);

/**
 * Adds one new type per object if necessary.
 */
void pddlAddObjectTypes(pddl_t *pddl);

/**
 * Remove specified objects from the planning task.
 */
void pddlRemoveObjs(pddl_t *pddl, const bor_iset_t *rm_objs, bor_err_t *err);

/**
 * Same as pddlRemoveObjs() except a remap array must be provided.
 */
void pddlRemoveObjsGetRemap(pddl_t *pddl,
                            const bor_iset_t *rm_obj,
                            pddl_obj_id_t *remap,
                            bor_err_t *err);

/**
 * Remap object IDs.
 */
void pddlRemapObjs(pddl_t *pddl, const pddl_obj_id_t *remap);

/**
 * Remove empty types and all related predicates and actions from the task
 */
void pddlRemoveEmptyTypes(pddl_t *pddl, bor_err_t *err);

/**
 * Remove assign and increase atoms to enforce the task to be unit cost
 */
void pddlEnforceUnitCost(pddl_t *pddl, bor_err_t *err);

/**
 * Returns -1 on error, 0 if pddl wasn't changed, and 1 if the pddl was
 * enriched with additional conditions pruning mutexes and dead-ends.
 */
int pddlCompileInLiftedMGroups(pddl_t *pddl,
                               const pddl_lifted_mgroups_t *mgroups,
                               bor_err_t *err);

/**
 * Prints PDDL domain file.
 */
void pddlPrintPDDLDomain(const pddl_t *pddl, FILE *fout);

/**
 * Prints PDDL problem file.
 */
void pddlPrintPDDLProblem(const pddl_t *pddl, FILE *fout);

void pddlPrintDebug(const pddl_t *pddl, FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_STRUCT_H__ */
