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

typedef struct pddl_asnets pddl_asnets_t;

struct pddl_asnets_config {
    /** Domain PDDL file. Set using *SetDomain() */
    char *domain_pddl;
    /** Number of input problem PDDL files (i.e., size of .problem_pddl[]) */
    int problem_pddl_size;
    /** Problem PDDL files. Set using *AddProblem() */
    char **problem_pddl;

    /** Output size of the hidden layers. Default: 16 */
    int hidden_dimension;
    /** Number of the layers. Default: 2 */
    int num_layers;

    /* Training parameters: */
    /** Fixed random seed. Default: 6961 */
    int random_seed;
    /** Weigth decay rate for regularization. Default: 2E-4 */
    float weight_decay;
    /** Dropout rate if set to >0. Default: 0.1 */
    float dropout_rate;
    /** Number of samples in a minibatch. Default: 64 */
    int batch_size;
    /** Double .batch_size every specified number of epochs. Default: 0 */
    int double_batch_size_every_epoch;
    /** Maximum number of epochs used for training. Default: 300 */
    int max_train_epochs;
    /** Number of train cycles within each epoch. Default: 700 */
    int train_steps;
    /** Limit on the number of steps for the policy rollout. Default: 1000 */
    int policy_rollout_limit;
    /** Time limit in seconds for the teacher to solve the given task.
     *  Default: 10.f */
    float teacher_timeout;
    /** Minimum success rate in .early_termination_epochs to terminate
     *  early. Default: 0.999 */
    float early_termination_success_rate;
    /** Number of epochs in which the success rate must be at higher than
     *  .early_termination_success_rate. Default: 20 */
    int early_termination_epochs;
};
typedef struct pddl_asnets_config pddl_asnets_config_t;

void pddlASNetsConfigLog(const pddl_asnets_config_t *cfg, pddl_err_t *err);
void pddlASNetsConfigInit(pddl_asnets_config_t *cfg);
void pddlASNetsConfigInitCopy(pddl_asnets_config_t *dst,
                              const pddl_asnets_config_t *src);
int pddlASNetsConfigInitFromFile(pddl_asnets_config_t *cfg,
                                 const char *filename,
                                 pddl_err_t *err);
void pddlASNetsConfigFree(pddl_asnets_config_t *cfg);

void pddlASNetsConfigSetDomain(pddl_asnets_config_t *cfg, const char *fn);
void pddlASNetsConfigAddProblem(pddl_asnets_config_t *cfg,
                                const char *problem_fn);



pddl_asnets_t *pddlASNetsNew(const pddl_asnets_config_t *cfg, pddl_err_t *err);

void pddlASNetsDel(pddl_asnets_t *a);

int pddlASNetsSave(const pddl_asnets_t *a, const char *fn, pddl_err_t *err);
int pddlASNetsLoad(pddl_asnets_t *a, const char *fn, pddl_err_t *err);


/**
 * TODO
 */
int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_ASNETS_H__ */
