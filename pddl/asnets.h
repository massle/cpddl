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
    int hidden_dimension;
    int num_layers;
    int random_seed;
    float weight_decay;
    float dropout_rate;
    int batch_size;
    int max_train_epochs;
    int policy_rollout_limit;
    float teacher_timeout;
};
typedef struct pddl_asnets_config pddl_asnets_config_t;

#define PDDL_ASNETS_CONFIG_INIT \
    { \
        16, /* .hidden_dimension */ \
        2, /* .num_layers */ \
        /* TODO */ \
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


int pddlASNetsTrain(const char *domain_fn,
                    const char **problem_fn,
                    int problem_fn_size,
                    pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_ASNETS_H__ */
