#include "internal.h"
#include "pddl/libs_info.h"
#include "pddl/asnets.h"
const char * const pddl_dynet_version = NULL;
#define ERROR PANIC("ASNets require the DyNet library; cpddl must be re-compiled with the DyNet support.")
void pddlFDConfigLog(const pddl_fd_config_t *cfg, pddl_err_t *err)
{ ERROR;}
void pddlFDConfigInit(pddl_fd_config_t *cfg)
{ ERROR;}
void pddlFDConfigFree(pddl_fd_config_t *cfg)
{ ERROR;}
void pddlFDConfigCopy(pddl_fd_config_t *dst, const pddl_fd_config_t *src)
{ ERROR;}
void pddlASNetsConfigLog(const pddl_asnets_config_t *cfg, pddl_err_t *err)
{ ERROR;}
void pddlASNetsConfigInit(pddl_asnets_config_t *cfg)
{ ERROR;}
void pddlASNetsConfigInitCopy(pddl_asnets_config_t *dst,
                              const pddl_asnets_config_t *src)
{ ERROR;}
int pddlASNetsConfigInitFromFile(pddl_asnets_config_t *cfg,
                                 const char *filename,
                                 pddl_err_t *err)
{ ERROR;return -1;}
int pddlASNetsConfigInitFromModel(pddl_asnets_config_t *cfg,
                                  const char *filename,
                                  pddl_err_t *err)
{ ERROR;return -1;}
void pddlASNetsConfigFree(pddl_asnets_config_t *cfg)
{ ERROR;}
void pddlASNetsConfigSetDomain(pddl_asnets_config_t *cfg, const char *fn)
{ ERROR;}
void pddlASNetsConfigAddProblem(pddl_asnets_config_t *cfg,
                                const char *problem_fn)
{ ERROR;}
void pddlASNetsConfigWrite(const pddl_asnets_config_t *cfg, FILE *fout)
{ ERROR;}
void pddlASNetsPolicyDistributionInit(pddl_asnets_policy_distribution_t *d)
{ ERROR;}
void pddlASNetsPolicyDistributionFree(pddl_asnets_policy_distribution_t *d)
{ ERROR;}
pddl_asnets_t *pddlASNetsNew(const pddl_asnets_config_t *cfg, pddl_err_t *err)
{ ERROR;return NULL;}
void pddlASNetsDel(pddl_asnets_t *a)
{ ERROR;}
int pddlASNetsSave(const pddl_asnets_t *a, const char *fn, pddl_err_t *err)
{ ERROR;return -1;}
int pddlASNetsLoad(pddl_asnets_t *a, const char *fn, pddl_err_t *err)
{ ERROR;return -1;}
int pddlASNetsPrintModelInfo(const char *fn, pddl_err_t *err)
{ ERROR;return -1;}
int pddlASNetsNumGroundTasks(const pddl_asnets_t *a)
{ ERROR;return -1;}
int pddlASNetsRunPolicy(pddl_asnets_t *a,
                        const pddl_asnets_ground_task_t *task,
                        const int *in_state,
                        int *out_state)
{ ERROR;return -1;}
int pddlASNetsPolicyDistribution(pddl_asnets_t *a,
                                 const pddl_asnets_ground_task_t *task,
                                 const int *in_state,
                                 pddl_asnets_policy_distribution_t *dist)
{ ERROR;return -1;}
int pddlASNetsSolveTask(pddl_asnets_t *a,
                        const pddl_asnets_ground_task_t *task,
                        pddl_iarr_t *trace,
                        pddl_asnets_softgoals_result_t *softgoals_result,
                        pddl_err_t *err)
{ ERROR;return -1;}
int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err)
{ ERROR;return -1;}
void pddlASNetsEvaluate(pddl_asnets_t *a, int write_plans, pddl_err_t *err)
{ ERROR;}
void pddlASNetsEvaluateOSP(pddl_asnets_t *a, int write_plans, int benchmark_trainer, pddl_err_t *err)
{ ERROR;}
int pddlASNetsBenchmarkTrainer(pddl_asnets_config_t* a_config, char* domain_filename, char* problem_filename, pddl_asnets_softgoals_result_t *msgs_result, pddl_err_t *err)
{ ERROR;return -1;}
