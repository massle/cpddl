/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL_HPOT_H__
#define __PDDL_HPOT_H__

#include <pddl/pot.h>
#include <pddl/task.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum pddl_hpot_type {
    PDDL_HPOT_OPT_STATE_TYPE = 1,
    PDDL_HPOT_OPT_ALL_SYNTACTIC_STATES_TYPE,
    PDDL_HPOT_OPT_ALL_STATES_MUTEX_TYPE,
    PDDL_HPOT_OPT_SAMPLED_STATES_TYPE,
    PDDL_HPOT_OPT_ENSEMBLE_SAMPLED_STATES_TYPE,
    PDDL_HPOT_OPT_ENSEMBLE_DIVERSIFICATION_TYPE,
    PDDL_HPOT_OPT_ENSEMBLE_ALL_STATES_MUTEX_TYPE,
};
typedef enum pddl_hpot_type pddl_hpot_type_t;

struct _pddl_hpot_config {
    pddl_hpot_type_t type;
    pddl_list_t conn;
};
typedef struct _pddl_hpot_config _pddl_hpot_config_t;

#define _PDDL_HPOT_CONFIG_INIT(type) \
    { \
        (type), /* .type */ \
        { NULL, NULL }, /* .conn */ \
    }

/**
 * Maximize the h-value for the given state
 */
struct pddl_hpot_config_opt_state {
    _pddl_hpot_config_t cfg;
    const int *fdr_state;
};
typedef struct pddl_hpot_config_opt_state pddl_hpot_config_opt_state_t;

#define PDDL_HPOT_CONFIG_OPT_STATE_INIT \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_STATE_TYPE), /* .cfg */ \
        NULL, /* .fdr_state */ \
    }
    

/**
 * Maximize the average h-value over all syntactic states
 */
struct pddl_hpot_config_opt_all_syntactic_states {
    _pddl_hpot_config_t cfg;
    /** If set to non-NULL, add the constraint maximizing h-value for the
     *  given state. default: NULL */
    const int *add_fdr_state_constr;
    /** Coefficient used for the added state constraint. default: 1 */
    double add_state_coef;
};
typedef struct pddl_hpot_config_opt_all_syntactic_states
    pddl_hpot_config_opt_all_syntactic_states_t;

#define PDDL_HPOT_CONFIG_OPT_ALL_SYNTACTIC_STATES \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_ALL_SYNTACTIC_STATES_TYPE), /* .cfg */ \
        NULL, /* .add_fdr_state_constr */ \
        1., /* .add_state_coef */ \
    }

/**
 * TODO
 */
struct pddl_hpot_config_opt_all_states_mutex {
    _pddl_hpot_config_t cfg;
    /** TODO */
    int mutex_size;
    /** If set to non-NULL, add the constraint maximizing h-value for the
     *  given state. default: NULL */
    const int *add_fdr_state_constr;
    /** Coefficient used for the added state constraint. default: 1 */
    double add_state_coef;
};
typedef struct pddl_hpot_config_opt_all_states_mutex
    pddl_hpot_config_opt_all_states_mutex_t;

#define PDDL_HPOT_CONFIG_OPT_ALL_STATES_MUTEX \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_ALL_STATES_MUTEX_TYPE), /* .cfg */ \
        2, /* .mutex_size */ \
        NULL, /* .add_fdr_state_constr */ \
        1., /* .add_state_coef */ \
    }

/**
 * Maximize the average h-value over the sampled states.
 */
struct pddl_hpot_config_opt_sampled_states {
    _pddl_hpot_config_t cfg;
    /** Number of sampled states. default: 1000 */
    int num_samples;
    /** True if random walk should be used. default: true */
    int use_random_walk;
    /** Sample states by uniform sampling over syntactic states. default: false */
    int use_syntactic_samples;
    /** Sample (syntactic) states while removing mutex states */
    int use_mutex_samples;
    /** If set to non-NULL, add the constraint maximizing h-value for the
     *  given state. default: NULL */
    const int *add_fdr_state_constr;
    /** Coefficient used for the added state constraint. default: 1 */
    double add_state_coef;
};
typedef struct pddl_hpot_config_opt_sampled_states
    pddl_hpot_config_opt_sampled_states_t;

#define PDDL_HPOT_CONFIG_OPT_SAMPLED_STATES \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_SAMPLED_STATES_TYPE), /* .cfg */ \
        1000, /* .num_samples */ \
        1, /* .use_random_walk */ \
        0, /* .use_syntactic_samples */ \
        NULL, /* .mutex */ \
        NULL, /* .add_fdr_state_constr */ \
        1., /* .add_state_coef */ \
    }

/**
 * Ensemble each maximizing for a sampled state
 */
struct pddl_hpot_config_opt_ensemble_sampled_states {
    _pddl_hpot_config_t cfg;
    /** Number of sampled states. default: 1000 */
    int num_samples;
    /** True if random walk should be used. default: true */
    int use_random_walk;
    /** Sample states by uniform sampling over syntactic states. default: false */
    int use_syntactic_samples;
    /** Sample (syntactic) states while removing mutex states */
    int use_mutex_samples;
};
typedef struct pddl_hpot_config_opt_ensemble_sampled_states
    pddl_hpot_config_opt_ensemble_sampled_states_t;

#define PDDL_HPOT_CONFIG_OPT_ENSEMBLE_SAMPLED_STATES \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_ENSEMBLE_SAMPLED_STATES_TYPE), /* .cfg */ \
        1000, /* .num_samples */ \
        1, /* .use_random_walk */ \
        0, /* .use_syntactic_samples */ \
        0, /* .mutex */ \
    }

/**
 * Ensemble constructed with the diversification algorithm
 */
struct pddl_hpot_config_opt_ensemble_diversification {
    _pddl_hpot_config_t cfg;
    /** Number of sampled states. default: 1000 */
    int num_samples;
    /** True if random walk should be used. default: true */
    int use_random_walk;
    /** Sample states by uniform sampling over syntactic states. default: false */
    int use_syntactic_samples;
    /** Sample (syntactic) states while removing mutex states. default: false */
    int use_mutex_samples;
};
typedef struct pddl_hpot_config_opt_ensemble_diversification
    pddl_hpot_config_opt_ensemble_diversification_t;

#define PDDL_HPOT_CONFIG_OPT_ENSEMBLE_DIVERSIFICATION \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_ENSEMBLE_DIVERSIFICATION_TYPE), /* .cfg */ \
        1000, /* .num_samples */ \
        1, /* .use_random_walk */ \
        0, /* .use_syntactic_samples */ \
        0, /* .mutex */ \
    }

/**
 * Ensemble constructed with the diversification algorithm
 */
struct pddl_hpot_config_opt_ensemble_all_states_mutex {
    _pddl_hpot_config_t cfg;
    /** TODO */
    int cond_size;
    /** TODO */
    int mutex_size;
    /** Number of sampled states conditioned on random sets of facts.
     *  default: 0, i.e., disabled */
    int num_rand_samples;
};
typedef struct pddl_hpot_config_opt_ensemble_all_states_mutex
    pddl_hpot_config_opt_ensemble_all_states_mutex_t;

#define PDDL_HPOT_CONFIG_OPT_ENSEMBLE_ALL_STATES_MUTEX \
    { \
        _PDDL_HPOT_CONFIG_INIT(PDDL_HPOT_OPT_ENSEMBLE_ALL_STATES_MUTEX_TYPE), /* .cfg */ \
        1, /* .cond_size */ \
        2, /* .mutex_size */ \
        0, /* .num_rand_samples */ \
    }

struct pddl_hpot_config {
    /** A list of configurations (see above) */
    pddl_list_t cfg;
    /** If true, disambiguation is used. default: true */
    int disambiguation;
    /** If true, weak disambiguation is used. default: false */
    int weak_disambiguation;
    /** Infer operator potentials. default: false */
    int op_pot;
    /** Infer real-valued operator potentials. default: false */
    int op_pot_real;
    /** Time limit for each round of LP solver. default: disabled */
    float time_limit;
};
typedef struct pddl_hpot_config pddl_hpot_config_t;

#define PDDL_HPOT_CONFIG_INIT { \
        { NULL, NULL }, /* .cfg */ \
        1, /* .disambiguation */ \
        0, /* .weak_disambiguation */ \
        0, /* .op_pot */ \
        0, /* .op_pot_real */ \
        -1., /* .time_limit */ \
    }

/**
 * Add potential heuritic configuration config_el to the main configuration
 * struct config.
 */
#define PDDL_HPOT_CONFIG_ADD(config, config_el) \
    do { \
        if (pddlListNext(&(config)->cfg) == NULL) \
            pddlListInit(&(config)->cfg); \
        pddlListInit(&(config_el)->cfg.conn); \
        pddlListAppend(&(config)->cfg, &(config_el)->cfg.conn); \
    } while (0)
        

void pddlHPotConfigLog(const pddl_hpot_config_t *cfg, pddl_err_t *err);

/**
 * Returns true if the config produces an ensamble of potential
 * heuristics.
 */
int pddlHPotConfigIsEnsemble(const pddl_hpot_config_t *cfg);

int pddlHPot(pddl_pot_solutions_t *sols,
             pddl_task_t *task,
             const pddl_hpot_config_t *cfg,
             pddl_err_t *err);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_HPOT_H__ */
