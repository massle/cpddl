/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL_LIFTED_SEARCH_H__
#define __PDDL_LIFTED_SEARCH_H__

#include <pddl/search.h>
#include <pddl/lifted_heur.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum pddl_lifted_search_status {
    PDDL_LIFTED_SEARCH_CONT = 0,
    PDDL_LIFTED_SEARCH_UNSOLVABLE,
    PDDL_LIFTED_SEARCH_FOUND,
    PDDL_LIFTED_SEARCH_ABORT,
};
typedef enum pddl_lifted_search_status pddl_lifted_search_status_t;

struct pddl_lifted_plan {
    char **plan;
    int plan_len;
    int plan_cost;
    int plan_alloc;
};
typedef struct pddl_lifted_plan pddl_lifted_plan_t;

typedef struct pddl_lifted_search pddl_lifted_search_t;

pddl_lifted_search_t *pddlLiftedSearchAStar(const pddl_t *pddl,
                                            pddl_lifted_heur_t *heur,
                                            pddl_err_t *err);
pddl_lifted_search_t *pddlLiftedSearchGBFS(const pddl_t *pddl,
                                           pddl_lifted_heur_t *heur,
                                           pddl_err_t *err);
pddl_lifted_search_t *pddlLiftedSearchLazy(const pddl_t *pddl,
                                           pddl_lifted_heur_t *heur,
                                           pddl_err_t *err);

void pddlLiftedSearchDel(pddl_lifted_search_t *s);
pddl_lifted_search_status_t pddlLiftedSearchInitStep(pddl_lifted_search_t *s);
pddl_lifted_search_status_t pddlLiftedSearchStep(pddl_lifted_search_t *s);

void pddlLiftedSearchStat(const pddl_lifted_search_t *s,
                          pddl_search_stat_t *stat);
void pddlLiftedSearchStatLog(const pddl_lifted_search_t *s, pddl_err_t *err);

const pddl_lifted_plan_t *pddlLiftedSearchPlan(const pddl_lifted_search_t *s);
void pddlLiftedSearchPlanPrint(const pddl_lifted_search_t *s, FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_LIFTED_SEARCH_H__ */
