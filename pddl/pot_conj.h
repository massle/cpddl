/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

/**
 * Potential Heuristics over Conjunctions
 * ---------------------------------------
 */

#ifndef __PDDL_POT_CONJ_H__
#define __PDDL_POT_CONJ_H__

#include <pddl/strips_conj.h>
#include <pddl/hpot.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int pddlPotConjMaxInitHValueBase(const pddl_strips_t *strips,
                                 const pddl_mutex_pairs_t *mutex,
                                 const pddl_mgroups_t *mgroup,
                                 pddl_err_t *err);
int pddlPotConjMaxInitHValueOnePair(const pddl_strips_t *strips,
                                    const pddl_mutex_pairs_t *mutex,
                                    const pddl_mgroups_t *mgroup,
                                    pddl_err_t *err);
int pddlPotConjMaxInitHValueOneTriple(const pddl_strips_t *strips,
                                      const pddl_mutex_pairs_t *mutex,
                                      const pddl_mgroups_t *mgroup,
                                      pddl_err_t *err);
int pddlPotConjMaxInitHValueTwoPairs(const pddl_strips_t *strips,
                                     const pddl_mutex_pairs_t *mutex,
                                     const pddl_mgroups_t *mgroup,
                                     pddl_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_POT_CONJ_H__ */
