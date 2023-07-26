/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL_ASNETS_H__
#define __PDDL_ASNETS_H__

#include <pddl/iarr.h>
#include <pddl/asnets_task.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct pddl_asnets pddl_asnets_t;

enum pddl_asnets_trainer {
    PDDL_ASNETS_TRAINER_ASTAR_LMCUT = 0,
    PDDL_ASNETS_TRAINER_FAST_DOWNWARD,
};
typedef enum pddl_asnets_trainer pddl_asnets_trainer_t;

struct pddl_fast_downward_config {
    /** Path to SAS and Plan files. */
    char *saved_files_path;
    /** Python Interpreter*/
    char *fd_interpreter;
    /** Path to FD Executable. */
    char *fd_executable_path;
    /** SAS filename prefix. */
    char *sas_file_prefix;
    /** Plan filename prefix. */
    char *plan_file_prefix;

     /** Number of arguments to pass to FD */
    int fd_arg_size;
    /** Argumement to pass to FD. */
    char **fd_args;

    /** Set to true if OSP params expected by FD planner */
    int use_osp_planner;
    /** Unique Filenames Flag */
    int use_unique_filenames;
};
typedef struct pddl_fast_downward_config pddl_fd_config_t;

void pddlFDConfigLog(const pddl_fd_config_t *cfg, pddl_err_t *err);
void pddlFDConfigInit(pddl_fd_config_t *cfg);
void pddlFDConfigFree(pddl_fd_config_t *cfg);
void pddlFDConfigCopy(pddl_fd_config_t *dst, const pddl_fd_config_t *src);


struct pddl_asnets_config {
    /** Domain PDDL file. Set using *SetDomain() */
    char *domain_pddl;
    /** Number of input problem PDDL files (i.e., size of .problem_pddl[]) */
    int problem_pddl_size;
    /** Problem PDDL files. Set using *AddProblem() */
    char **problem_pddl;

    /** Set to true if OSP problem */
    int is_osp_problem;

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

    /** Which trainer will be used. One of PDDL_ASNETS_TRAINER_* */
    pddl_asnets_trainer_t trainer;

    /** Configure call to Fast Downward trainer.
     *  Is NULL by default.
     *  Will be replaced by the config created using config file if FD used as trainer.
     */
    pddl_fd_config_t *fd_config;

    /** If set to non-NULL, pddlASNetsTrain() saves a model to the path
     *  with this prefix every time it finds a model with improved success
     *  rate */
    const char *save_model_prefix;
};
typedef struct pddl_asnets_config pddl_asnets_config_t;

void pddlASNetsConfigLog(const pddl_asnets_config_t *cfg, pddl_err_t *err);
void pddlASNetsConfigInit(pddl_asnets_config_t *cfg);
void pddlASNetsConfigInitCopy(pddl_asnets_config_t *dst,
                              const pddl_asnets_config_t *src);
int pddlASNetsConfigInitFromFile(pddl_asnets_config_t *cfg,
                                 const char *filename,
                                 pddl_err_t *err);
int pddlASNetsConfigInitFromModel(pddl_asnets_config_t *cfg,
                                  const char *filename,
                                  pddl_err_t *err);
void pddlASNetsConfigFree(pddl_asnets_config_t *cfg);

void pddlASNetsConfigSetDomain(pddl_asnets_config_t *cfg, const char *fn);
void pddlASNetsConfigAddProblem(pddl_asnets_config_t *cfg,
                                const char *problem_fn);
void pddlASNetsConfigWrite(const pddl_asnets_config_t *cfg, FILE *fout);

struct pddl_asnets_softgoals_result {
    /** Total Number of Soft Goals */
    int total_softgoals;
    /** Max Number of Soft Goals Achieved */
    int max_softgoals_achieved;
    /** Length of Plan achieveing MSGS*/
    /** i.e, Number of Rollout Steps in which MSGS Achieved when using Policy*/
    int max_softgoals_plan_steps;
    /** Total Number of Steps Attempted when using Policy*/
    int total_policy_steps;
};
typedef struct pddl_asnets_softgoals_result pddl_asnets_softgoals_result_t;

#define PDDL_ASNETS_SOFTGOALS_RESULT_INIT \
    { \
        0, /* .total_softgoals */ \
        0, /* .max_softgoals_achieved */ \
        0, /* .max_softgoals_policy_steps */ \
        0, /* .total_policy_steps */ \
    }

struct pddl_asnets_policy_distribution {
    /** Number of applicable operators */
    int op_size;
    int op_alloc;
    /** Array of applicable operators */
    int *op_id;
    /** Array of probabilities/confidence of the corresponding operator
     *  being selected by the policy */
    float *prob;
};
typedef struct pddl_asnets_policy_distribution
    pddl_asnets_policy_distribution_t;

/**
 * Initialize empty distribution
 */
void pddlASNetsPolicyDistributionInit(pddl_asnets_policy_distribution_t *d);

/**
 * Free allocated memory
 */
void pddlASNetsPolicyDistributionFree(pddl_asnets_policy_distribution_t *d);

/**
 * Creates a new instance of ASNets according to the configuration
 */
pddl_asnets_t *pddlASNetsNew(const pddl_asnets_config_t *cfg, pddl_err_t *err);

/**
 * Free allocated memory.
 */
void pddlASNetsDel(pddl_asnets_t *a);

/**
 * Save ASNets model into the given file.
 */
int pddlASNetsSave(const pddl_asnets_t *a, const char *fn, pddl_err_t *err);

/**
 * Load ASNets from the given file.
 */
int pddlASNetsLoad(pddl_asnets_t *a, const char *fn, pddl_err_t *err);

/**
 * Load model information from the given file and print it out.
 */
int pddlASNetsPrintModelInfo(const char *fn, pddl_err_t *err);

/**
 * Returns number of ground tasks stored in the given object.
 */
int pddlASNetsNumGroundTasks(const pddl_asnets_t *a);

/**
 * Returns ASNets task with the given ID
 */
const pddl_asnets_ground_task_t *
pddlASNetsGetGroundTask(const pddl_asnets_t *a, int id);

/**
 * Run policy on the given state from the given task.
 * If {out_state} is non-NULL, it is filled with the resulting state.
 * Returns ID of the selected operator, or -1 if no operator is applicable.
 */
int pddlASNetsRunPolicy(pddl_asnets_t *a,
                        const pddl_asnets_ground_task_t *task,
                        const int *in_state,
                        int *out_state);

/**
 * Run policy on the given state from the given task and returns a
 * distribution over applicable actions. The function can be repeatedly
 * called on the same pddl_asnets_policy_distribution_t struct -- it will
 * be rewritten every time.
 * Returns 0 on success, -1 otherwise.
 */
int pddlASNetsPolicyDistribution(pddl_asnets_t *a,
                                 const pddl_asnets_ground_task_t *task,
                                 const int *in_state,
                                 pddl_asnets_policy_distribution_t *dist);

/**
 * Try to solve the task using the ASNets policy.
 * {trace} is filled with the policy trace.
 * {softgoals_result} captures maximum solved softgoal size and corresponding policy steps, is NULL for non-OSP problems.
 * Return true if a plan was found, and false otherwise.
 */
int pddlASNetsSolveTask(pddl_asnets_t *a,
                        const pddl_asnets_ground_task_t *task,
                        pddl_iarr_t *trace,
                        pddl_asnets_softgoals_result_t *softgoals_result,
                        pddl_err_t *err);

/**
 * Train ASNets according to the configuration it was created with.
 */
int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err);

/**
 * Evaluate ASNets for test problems given in configuration.
 */
void pddlASNetsEvaluate(pddl_asnets_t *a, int write_plans, pddl_err_t *err);

/**
 * Evaluate ASNets for test problems given in configuration for OSP problems.
 */
void pddlASNetsEvaluateOSP(pddl_asnets_t *a, int write_plans, int benchmark_trainer, pddl_err_t *err);

/**
 * Find benchmarks using trainer planner to compare ASNets with.
 */
int pddlASNetsBenchmarkTrainer(pddl_asnets_config_t* a_config, char* domain_filename, char* problem_filename, pddl_asnets_softgoals_result_t *msgs_result, pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_ASNETS_H__ */
