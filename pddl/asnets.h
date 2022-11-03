/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL_ASNETS_H__
#define __PDDL_ASNETS_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_asnets_config {
    /** Output size of the hidden layers */
    int hidden_dimension;
    /** Number of the layers */
    int num_layers;

    /* Training parameters: */
    /** Fixed random seed */
    int random_seed;
    /** Weigth decay rate for regularization */
    float weight_decay;
    /** Dropout rate if set to >0. */
    float dropout_rate;
    /** Number of samples in a minibatch */
    int batch_size;
    /** Maximum number of epochs used for training */
    int max_train_epochs;
    /** Number of train cycles within each epoch */
    int train_steps;
    /** Limit on the number of steps for the policy rollout */
    int policy_rollout_limit;
    /** Time limit in seconds for the teacher to solve the given task */
    float teacher_timeout;
};
typedef struct pddl_asnets_config pddl_asnets_config_t;

#define PDDL_ASNETS_CONFIG_INIT \
    { \
        16, /* .hidden_dimension */ \
        2, /* .num_layers */ \
        6961, /* .random_seed */ \
        2E-4, /* .weight_decay */ \
        0.1, /* .dropout_rate */ \
        64, /* .batch_size */ \
        10, /* .max_train_epochs */ \
        700, /* .train_steps */ \
        100, /* .policy_rollout_limit */ \
        30.f, /* .teacher_timeout */ \
    }

typedef struct pddl_asnets pddl_asnets_t;

pddl_asnets_t *pddlASNetsNew(const char *domain_fn,
                             const char **problem_fn,
                             int problem_fn_size,
                             const pddl_asnets_config_t *cfg,
                             pddl_err_t *err);

void pddlASNetsDel(pddl_asnets_t *a);

//void pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err);

void pddlASNetsSaveWeights(const pddl_asnets_t *a, const char *fn);
void pddlASNetsLoadWeights(pddl_asnets_t *a, const char *fn);


/**
 * TODO
 */
int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_ASNETS_H__ */
