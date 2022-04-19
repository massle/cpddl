#ifndef __PDDL_PROCESS_STRIPS_H__
#define __PDDL_PROCESS_STRIPS_H__

#include <pddl/strips.h>
#include <pddl/mgroup.h>
#include <pddl/mutex_pair.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_process_strips {
    pddl_list_t steps;
    pddl_iset_t rm_op;
    pddl_iset_t rm_fact;
    int removed_op;
    int removed_fact;

    pddl_strips_t *strips;
    pddl_mgroups_t *mgroups;
    pddl_mutex_pairs_t *mutex;
};
typedef struct pddl_process_strips pddl_process_strips_t;

void pddlProcessStripsInit(pddl_process_strips_t *prune);
void pddlProcessStripsFree(pddl_process_strips_t *prune);

int pddlProcessStripsExecute(pddl_process_strips_t *prune,
                             pddl_strips_t *strips,
                             pddl_mgroups_t *mgroups,
                             pddl_mutex_pairs_t *mutex,
                             pddl_err_t *err);

void pddlProcessStripsAddIrrelevance(pddl_process_strips_t *prune);
void pddlProcessStripsAddFAMGroupsDeadEndOps(pddl_process_strips_t *prune);
void pddlProcessStripsAddH2Fw(pddl_process_strips_t *prune,
                              float time_limit_in_s);
void pddlProcessStripsAddH2FwBw(pddl_process_strips_t *prune,
                                float time_limit_in_s);
void pddlProcessStripsAddH3Fw(pddl_process_strips_t *prune,
                              float time_limit_in_s,
                              size_t excess_memory);
void pddlProcessStripsAddDeduplicateOps(pddl_process_strips_t *prune);
void pddlProcessStripsAddSortOps(pddl_process_strips_t *prune);

void pddlProcessStripsAddOpMutex(pddl_process_strips_t *prune,
                                 int ts, int op_fact, int hm_op,
                                 int no_prune, const char *out);
void pddlProcessStripsAddEndomorphFDR(pddl_process_strips_t *prune,
                                      const pddl_endomorphism_config_t *cfg);
void pddlProcessStripsAddEndomorphMGStrips(pddl_process_strips_t *prune,
                                           const pddl_endomorphism_config_t *cfg);
void pddlProcessStripsAddEndomorphTS(pddl_process_strips_t *prune,
                                     const pddl_endomorphism_config_t *cfg);
void pddlProcessStripsAddEndomorphFDRTS(pddl_process_strips_t *prune,
                                        const pddl_endomorphism_config_t *cfg);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_PROCESS_STRIPS_H__ */
