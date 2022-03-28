/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <boruvka/alloc.h>
#include "pddl/irrelevance.h"
#include "pddl/famgroup.h"
#include "pddl/critical_path.h"
#include "pddl/mg_strips.h"
#include "process_strips.h"

typedef struct pddl_process_strips_step pddl_process_strips_step_t;

typedef int (*pddl_process_strips_execute_fn)(pddl_process_strips_t *prune,
                                            pddl_process_strips_step_t *step,
                                            bor_err_t *err);
typedef void (*pddl_process_strips_free_fn)(pddl_process_strips_step_t *step);

struct pddl_process_strips_step {
    char *name;
    bor_list_t conn;
    int can_reuse_rm_op_fact;
    pddl_process_strips_execute_fn execute;
    pddl_process_strips_free_fn free;
};

struct pddl_process_strips_step_hm {
    pddl_process_strips_step_t step;
    float time_limit;
    size_t excess_memory;
};
typedef struct pddl_process_strips_step_hm pddl_process_strips_step_hm_t;

void pddlProcessStripsInit(pddl_process_strips_t *prune)
{
    bzero(prune, sizeof(*prune));
    borListInit(&prune->steps);
}

void pddlProcessStripsFree(pddl_process_strips_t *prune)
{
    bor_list_t *item, *tmp;
    PDDL_LIST_FOR_EACH_SAFE(&prune->steps, item, tmp){
        pddl_process_strips_step_t *step;
        step = PDDL_LIST_ENTRY(item, pddl_process_strips_step_t, conn);
        borListDel(&step->conn);
        step->free(step);
        if (step->name != NULL)
            BOR_FREE(step->name);
        BOR_FREE(step);
    }
    pddlISetFree(&prune->rm_op);
    pddlISetFree(&prune->rm_fact);
}

static int apply(pddl_process_strips_t *prune, bor_err_t *err)
{
    if (pddlISetSize(&prune->rm_fact) > 0 || pddlISetSize(&prune->rm_op) > 0){
        BOR_INFO(err, "Removing %d facts, %d operators",
                 pddlISetSize(&prune->rm_fact),
                 pddlISetSize(&prune->rm_op));
        pddlStripsReduce(prune->strips, &prune->rm_fact, &prune->rm_op);
        if (prune->mgroups != NULL && pddlISetSize(&prune->rm_fact) > 0)
            pddlMGroupsReduce(prune->mgroups, &prune->rm_fact);
        if (prune->mutex != NULL && pddlISetSize(&prune->rm_fact) > 0)
            pddlMutexPairsReduce(prune->mutex, &prune->rm_fact);
        prune->removed_op += pddlISetSize(&prune->rm_op);
        prune->removed_fact += pddlISetSize(&prune->rm_fact);
        pddlISetEmpty(&prune->rm_op);
        pddlISetEmpty(&prune->rm_fact);
    }
    return 0;
}

static int step(pddl_process_strips_t *prune,
                pddl_process_strips_step_t *step,
                bor_err_t *err)
{
    if (!step->can_reuse_rm_op_fact)
        apply(prune, err);

    BOR_INFO_PREFIX_PUSH(err, step->name);
    int rm_fact = pddlISetSize(&prune->rm_fact);
    int rm_op = pddlISetSize(&prune->rm_op);
    if (step->execute(prune, step, err) != 0){
        BOR_INFO_PREFIX_POP(err);
        BOR_TRACE_RET(err, -1);
    }
    BOR_INFO(err, "Found new redundant: %d facts, %d operators",
             pddlISetSize(&prune->rm_fact) - rm_fact,
             pddlISetSize(&prune->rm_op) - rm_op);
    BOR_INFO(err, "Found redundant so far: %d facts, %d operators",
             prune->removed_fact + pddlISetSize(&prune->rm_fact),
             prune->removed_op + pddlISetSize(&prune->rm_op));
    BOR_INFO_PREFIX_POP(err);
    return 0;
}

int pddlProcessStripsExecute(pddl_process_strips_t *prune,
                           pddl_strips_t *strips,
                           pddl_mgroups_t *mgroups,
                           pddl_mutex_pairs_t *mutex,
                           bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "STRIPS: ");
    prune->strips = strips;
    prune->mgroups = mgroups;
    prune->mutex = mutex;

    // TODO: Configure fixpoint
    bor_list_t *item;
    PDDL_LIST_FOR_EACH(&prune->steps, item){
        pddl_process_strips_step_t *s;
        s = PDDL_LIST_ENTRY(item, pddl_process_strips_step_t, conn);
        if (step(prune, s, err) != 0){
            BOR_INFO_PREFIX_POP(err);
            BOR_TRACE_RET(err, -1);
        }
    }

    apply(prune, err);
    BOR_INFO(err, "Removed %d facts, %d operators",
             prune->removed_fact,
             prune->removed_op);
    pddlStripsLogInfo(strips, err);
    BOR_INFO_PREFIX_POP(err);
    return 0;
}

static void stepInit(const char *name,
                     pddl_process_strips_step_t *step,
                     pddl_process_strips_t *prune,
                     pddl_process_strips_execute_fn execute,
                     pddl_process_strips_free_fn free)
{
    bzero(step, sizeof(*step));
    step->name = BOR_STRDUP(name);
    borListInit(&step->conn);
    step->execute = execute;
    step->free = free;
    borListAppend(&prune->steps, &step->conn);
}

static pddl_process_strips_step_t *stepNew(const char *name,
                                         pddl_process_strips_t *prune,
                                         pddl_process_strips_execute_fn execute,
                                         pddl_process_strips_free_fn free)
{
    pddl_process_strips_step_t *step = BOR_ALLOC(pddl_process_strips_step_t);
    stepInit(name, step, prune, execute, free);
    return step;
}

static pddl_process_strips_step_hm_t *
        stepHmNew(const char *name,
                  pddl_process_strips_t *prune,
                  pddl_process_strips_execute_fn execute,
                  pddl_process_strips_free_fn free)
{
    pddl_process_strips_step_hm_t *step;
    step = BOR_ALLOC(pddl_process_strips_step_hm_t);
    stepInit(name, &step->step, prune, execute, free);
    step->time_limit = 0.f;
    return step;
}

static void emptyFree(pddl_process_strips_step_t *_)
{
}

static int irrelevance(pddl_process_strips_t *prune,
                       pddl_process_strips_step_t *step,
                       bor_err_t *err)
{
    return pddlIrrelevanceAnalysis(prune->strips,
                                   &prune->rm_fact,
                                   &prune->rm_op,
                                   NULL,
                                   err);
}

void pddlProcessStripsAddIrrelevance(pddl_process_strips_t *prune)
{
    pddl_process_strips_step_t *step;
    step = stepNew("irrelevance: ", prune, irrelevance, emptyFree);
    step->can_reuse_rm_op_fact = 0;
}

static int famgroupsDeadEndOps(pddl_process_strips_t *prune,
                               pddl_process_strips_step_t *step,
                               bor_err_t *err)
{
    pddlFAMGroupsDeadEndOps(prune->mgroups, prune->strips, &prune->rm_op);
    return 0;
}

void pddlProcessStripsAddFAMGroupsDeadEndOps(pddl_process_strips_t *prune)
{
    pddl_process_strips_step_t *step;
    step = stepNew("fam dead-end: ", prune, famgroupsDeadEndOps, emptyFree);
    step->can_reuse_rm_op_fact = 0;
}

static int h2fw(pddl_process_strips_t *prune,
                pddl_process_strips_step_t *_step,
                bor_err_t *err)
{
    pddl_process_strips_step_hm_t *step;
    step = bor_container_of(_step, pddl_process_strips_step_hm_t, step);
    return pddlH2(prune->strips,
                  prune->mutex,
                  &prune->rm_fact,
                  &prune->rm_op,
                  step->time_limit,
                  err);
}

void pddlProcessStripsAddH2Fw(pddl_process_strips_t *prune,
                              float time_limit_in_s)
{
    pddl_process_strips_step_hm_t *step;
    step = stepHmNew("h^2 fw: ", prune, h2fw, emptyFree);
    step->step.can_reuse_rm_op_fact = 1;
    step->time_limit = time_limit_in_s;
}

static int h2fwbw(pddl_process_strips_t *prune,
                  pddl_process_strips_step_t *_step,
                  bor_err_t *err)
{
    pddl_process_strips_step_hm_t *step;
    step = bor_container_of(_step, pddl_process_strips_step_hm_t, step);
    pddl_mg_strips_t mg_strips;
    pddlMGStripsInit(&mg_strips, prune->strips, prune->mgroups);
    int ret = pddlH2FwBw(&mg_strips.strips,
                         &mg_strips.mg,
                         prune->mutex,
                         &prune->rm_fact,
                         &prune->rm_op,
                         step->time_limit,
                         err);
    pddlMGStripsFree(&mg_strips);
    return ret;
}

void pddlProcessStripsAddH2FwBw(pddl_process_strips_t *prune,
                                float time_limit_in_s)
{
    pddl_process_strips_step_hm_t *step;
    step = stepHmNew("h^2 fw+bw: ", prune, h2fwbw, emptyFree);
    step->step.can_reuse_rm_op_fact = 1;
    step->time_limit = time_limit_in_s;
}

static int h3fw(pddl_process_strips_t *prune,
                pddl_process_strips_step_t *_step,
                bor_err_t *err)
{
    pddl_process_strips_step_hm_t *step;
    step = bor_container_of(_step, pddl_process_strips_step_hm_t, step);
    return pddlH3(prune->strips,
                  prune->mutex,
                  &prune->rm_fact,
                  &prune->rm_op,
                  step->time_limit,
                  step->excess_memory,
                  err);
}

void pddlProcessStripsAddH3Fw(pddl_process_strips_t *prune,
                              float time_limit_in_s,
                              size_t excess_memory)
{
    pddl_process_strips_step_hm_t *step;
    step = stepHmNew("h^3 fw: ", prune, h3fw, emptyFree);
    step->step.can_reuse_rm_op_fact = 1;
    step->time_limit = time_limit_in_s;
    step->excess_memory = excess_memory;
}

static int deduplicateOps(pddl_process_strips_t *prune,
                          pddl_process_strips_step_t *step,
                          bor_err_t *err)
{
    pddlStripsOpsDeduplicateSet(&prune->strips->op, &prune->rm_op);
    return 0;
}

void pddlProcessStripsAddDeduplicateOps(pddl_process_strips_t *prune)
{
    pddl_process_strips_step_t *step;
    step = stepNew("deduplicate ops: ", prune, deduplicateOps, emptyFree);
    step->can_reuse_rm_op_fact = 0;
}
