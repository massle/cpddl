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

struct pddl_pot_conj_find_config {
    /** Time limit in seconds. Default: 20 minutes */
    double time_limit;
    /** Maximum number of improvement epochs. Default: 10 */
    int max_epochs;
    /** Maximum considered dimension of conjunctions. Default: 8 */
    int max_conj_dim;
    /** Log progress every {log_freq} seconds. Default: 1 second */
    double log_freq;
    /** Test on randomly generated conjunctions. Default: false */
    pddl_bool_t random_conjs;
    /** Random seed for .random_conjs = true. Default: 1193 */
    int random_seed;
    /** Whenever improvement is achieved, write the complete set of
     *  conjunctions into the file {write_progress_prefix}.{epoch_idx}
     *  where {epoch_idx} is index of the epoch formatted as %04d.
     *  Default: NULL, i.e., turned off */
    const char *write_progress_prefix;
};
typedef struct pddl_pot_conj_find_config pddl_pot_conj_find_config_t;

#define PDDL_POT_CONJ_FIND_CONFIG_INIT \
    { \
        20. * 60., /* .time_limit */ \
        10, /* .max_epochs */ \
        8, /* .max_conj_dim */ \
        1., /* .log_freq */ \
        pddl_false, /* .random_conjs */ \
        1193, /* .random_seed */ \
        NULL, /* .write_progress_prefix */ \
    }

void pddlPotConjFindConfigLog(const pddl_pot_conj_find_config_t *cfg,
                              pddl_err_t *err);

/**
 * Find conjunctions improving one-dimensional potential heuritic and add
 * these conjunctions into {conjs}. If {conjs} is non-empty, conjunctions
 * from {conjs} are always used, i.e., {conjs} is input/output argument.
 */
int pddlPotConjFind(pddl_set_iset_t *conjs,
                    const pddl_strips_t *strips,
                    const pddl_mutex_pairs_t *mutex,
                    const pddl_mgroups_t *mgroup,
                    const pddl_pot_conj_find_config_t *cfg,
                    pddl_err_t *err);


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
