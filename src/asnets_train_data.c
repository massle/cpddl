/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/rand.h"
#include "pddl/hfunc.h"
#include "pddl/search.h"
#include "pddl/asnets_train_data.h"
#include "pddl/subprocess.h"
#include "pddl/strstream.h"

struct pddl_asnets_train_data_sample {
    pddl_htable_key_t hash;
    pddl_list_t htable;

    int selected_op_id;
    int fdr_state_size;
    int ground_task_id;
    int fdr_state[];
};

static pddl_htable_key_t htableHash(const pddl_list_t *key, void *_)
{
    const pddl_asnets_train_data_sample_t *sample;
    sample = PDDL_LIST_ENTRY(key, pddl_asnets_train_data_sample_t, htable);
    return sample->hash;
}

static int htableEq(const pddl_list_t *key1, const pddl_list_t *key2, void *_)
{
    const pddl_asnets_train_data_sample_t *sample1, *sample2;
    sample1 = PDDL_LIST_ENTRY(key1, pddl_asnets_train_data_sample_t, htable);
    sample2 = PDDL_LIST_ENTRY(key2, pddl_asnets_train_data_sample_t, htable);
    int cmp = sample1->ground_task_id - sample2->ground_task_id;
    if (cmp == 0){
        ASSERT(sample1->fdr_state_size == sample2->fdr_state_size);
        cmp = memcmp(sample1->fdr_state, sample2->fdr_state,
                     sizeof(int) * sample1->fdr_state_size);
    }
    return cmp == 0;
}

static pddl_htable_key_t sampleHash(const pddl_asnets_train_data_sample_t *s)
{
    return pddlFastHash_64(&s->ground_task_id,
                           sizeof(int) * (s->fdr_state_size + 1),
                           3929);
}

static size_t sampleSize(int state_size)
{
    size_t size = sizeof(pddl_asnets_train_data_sample_t);
    size += sizeof(int) * state_size;
    return size;
}

static pddl_asnets_train_data_sample_t *sampleNew(int ground_task_id,
                                                  const int *state,
                                                  int state_size,
                                                  int selected_op_id)
{
    size_t size = sampleSize(state_size);
    pddl_asnets_train_data_sample_t *sample = MALLOC(size);
    ZEROIZE_RAW(sample, size);
    sample->ground_task_id = ground_task_id;
    sample->selected_op_id = selected_op_id;
    sample->fdr_state_size = state_size;
    memcpy(sample->fdr_state, state, sizeof(int) * state_size);
    sample->hash = sampleHash(sample);
    return sample;
}

static void sampleDel(pddl_asnets_train_data_sample_t *sample)
{
    FREE(sample);
}

void pddlASNetsTrainDataInit(pddl_asnets_train_data_t *td,
                             const pddl_asnets_ground_task_t *task,
                             int task_size)
{
    ZEROIZE(td);
    td->htable = pddlHTableNew(htableHash, htableEq, NULL);
    td->fail_cache = pddlHTableNew(htableHash, htableEq, NULL);

    td->task_size = task_size;
    td->task = ALLOC_ARR(const pddl_asnets_ground_task_t *, td->task_size);
    for (int ti = 0; ti < td->task_size; ++ti)
        td->task[ti] = task + ti;
}

void pddlASNetsTrainDataFree(pddl_asnets_train_data_t *td)
{
    pddlHTableDel(td->htable);

    pddl_list_t list;
    pddlListInit(&list);
    pddlHTableGather(td->fail_cache, &list);
    while (!pddlListEmpty(&list)){
        pddl_list_t *item = pddlListNext(&list);
        pddlListDel(item);
        pddl_asnets_train_data_sample_t *s;
        s = PDDL_LIST_ENTRY(item, pddl_asnets_train_data_sample_t, htable);
        sampleDel(s);
    }
    pddlHTableDel(td->fail_cache);

    for (int i = 0; i < td->sample_size; ++i)
        sampleDel(td->sample[i]);
    if (td->sample != NULL)
        FREE(td->sample);
}

int pddlASNetsTrainDataGetSample(const pddl_asnets_train_data_t *td,
                                 int sample_id,
                                 int *ground_task_id,
                                 int *selected_op_id,
                                 pddl_iset_t *strips_state,
                                 pddl_iset_t *applicable_ops,
                                 pddl_iset_t *strips_goal)
{
    const pddl_asnets_train_data_sample_t *sample = td->sample[sample_id];
    if (ground_task_id != NULL)
        *ground_task_id = sample->ground_task_id;
    if (selected_op_id != NULL)
        *selected_op_id = sample->selected_op_id;
    if (strips_state != NULL){
        pddlISetEmpty(strips_state);
        pddlASNetsGroundTaskFDRStateToStrips(td->task[sample->ground_task_id],
                                             sample->fdr_state, strips_state);
    }
    if (applicable_ops != NULL){
        pddlISetEmpty(applicable_ops);
        pddlASNetsGroundTaskFDRApplicableOps(td->task[sample->ground_task_id],
                                             sample->fdr_state, applicable_ops);
    }
    if (strips_goal != NULL){
        pddlISetEmpty(strips_goal);
        // TODO: We might want to allow different goals for the given sample
        pddlASNetsGroundTaskFDRPartStateToStrips(td->task[sample->ground_task_id],
                                                 &td->task[sample->ground_task_id]->fdr.goal,
                                                 strips_goal);
    }
    return 0;
}

void pddlASNetsTrainDataAdd(pddl_asnets_train_data_t *td,
                            int ground_task_id,
                            const int *state,
                            int state_size,
                            int selected_op_id)
{
    pddl_asnets_train_data_sample_t *sample;
    sample = sampleNew(ground_task_id, state, state_size, selected_op_id);

    if (pddlHTableInsertUnique(td->htable, &sample->htable) == NULL){
        ARR_MAKE_SPACE(td->sample, pddl_asnets_train_data_sample_t *,
                       td->sample_size, td->sample_alloc, 2);
        td->sample[td->sample_size++] = sample;
    }else{
        sampleDel(sample);
    }
}

void pddlASNetsTrainDataAddFail(pddl_asnets_train_data_t *td,
                                int ground_task_id,
                                const int *state,
                                int state_size)
{
    pddl_asnets_train_data_sample_t *sample;
    sample = sampleNew(ground_task_id, state, state_size, -1);

    if (pddlHTableInsertUnique(td->fail_cache, &sample->htable) != NULL)
        sampleDel(sample);
}

void pddlASNetsTrainDataAddPlan(pddl_asnets_train_data_t *td,
                                int ground_task_id,
                                int state_size,
                                const int *init_state,
                                const pddl_fdr_ops_t *ops,
                                const pddl_iarr_t *plan)
{
    int state[state_size];
    memcpy(state, init_state, sizeof(int) * state_size);

    int op_id;
    PDDL_IARR_FOR_EACH(plan, op_id){
        pddlASNetsTrainDataAdd(td, ground_task_id, state, state_size, op_id);
        const pddl_fdr_op_t *op = ops->op[op_id];
        pddlFDROpApplyOnStateInPlace(op, state_size, state);
    }
}

void pddlASNetsTrainDataShuffle(pddl_asnets_train_data_t *td)
{
    pddl_rand_t rnd;
    pddlRandInitAuto(&rnd);
    for (int dst = td->sample_size - 1; dst > 0; --dst){
        int src = pddlRand(&rnd, 0, dst + 1);
        ASSERT(src <= dst && src >= 0);
        if (src != dst){
            pddl_asnets_train_data_sample_t *tmp;
            PDDL_SWAP(td->sample[src], td->sample[dst], tmp);
        }
    }
    pddlRandFree(&rnd);
}

static int stateExists(const pddl_asnets_train_data_t *td,
                       int ground_task_id,
                       const int *state,
                       int state_size)
{
    pddl_asnets_train_data_sample_t *sample;
    sample = alloca(sampleSize(state_size));
    sample->fdr_state_size = state_size;
    sample->ground_task_id = ground_task_id;
    memcpy(sample->fdr_state, state, sizeof(int) * state_size);
    sample->hash = sampleHash(sample);

    if (pddlHTableFind(td->htable, &sample->htable) == NULL){
        return 0;
    }else{
        return 1;
    }
}

static int failExists(const pddl_asnets_train_data_t *td,
                      int ground_task_id,
                      const int *state,
                      int state_size)
{
    pddl_asnets_train_data_sample_t *sample;
    sample = alloca(sampleSize(state_size));
    sample->fdr_state_size = state_size;
    sample->ground_task_id = ground_task_id;
    memcpy(sample->fdr_state, state, sizeof(int) * state_size);
    sample->hash = sampleHash(sample);

    if (pddlHTableFind(td->fail_cache, &sample->htable) == NULL){
        return 0;
    }else{
        return 1;
    }
}


static int rolloutAStar(pddl_asnets_train_data_t *td,
                        int ground_task_id,
                        const int *state,
                        const pddl_fdr_t *_fdr,
                        const pddl_heur_config_t *heur_cfg,
                        float max_time,
                        pddl_err_t *err)
{
    pddl_timer_t timer;
    pddlTimerStart(&timer);

    pddl_fdr_t fdr;
    pddlFDRInitShallowCopyWithDifferentInitState(&fdr, _fdr, state);

    pddl_heur_t *heur = pddlHeur(heur_cfg, err);
    if (heur == NULL){
        pddlFDRFree(&fdr);
        TRACE_RET(err, -1);
    }

    pddl_search_config_t search_cfg = PDDL_SEARCH_CONFIG_INIT;
    search_cfg.fdr = &fdr;
    search_cfg.alg = PDDL_SEARCH_ASTAR;
    search_cfg.heur = heur;
    pddl_search_t *search = pddlSearchNew(&search_cfg, err);
    if (search == NULL){
        pddlFDRFree(&fdr);
        pddlHeurDel(heur);
        TRACE_RET(err, -1);
    }

    int st = pddlSearchInitStep(search);
    while (st == PDDL_SEARCH_CONT){
        pddlTimerStop(&timer);
        if (pddlTimerElapsedInSF(&timer) > max_time){
            st = PDDL_SEARCH_ABORT;
            break;
        }

        st = pddlSearchStep(search);
    }

    if (st == PDDL_SEARCH_FOUND){
        LOG(err, "Plan found");
        pddl_plan_t plan;
        pddlPlanInit(&plan);
        if (pddlSearchExtractPlan(search, &plan) == 0){
            pddlASNetsTrainDataAddPlan(td, ground_task_id, fdr.var.var_size,
                                       state, &fdr.op, &plan.op);
        }
        pddlPlanFree(&plan);

    }else{
        pddlASNetsTrainDataAddFail(td, ground_task_id, state, fdr.var.var_size);
        if (st == PDDL_SEARCH_ABORT)
            LOG(err, "Search reached time-out");
        LOG(err, "Plan not found");
    }

    pddlSearchDel(search);
    pddlHeurDel(heur);
    pddlFDRFree(&fdr);
    return 0;
}

static void stringToPlan(const char *str,
                         const pddl_fdr_t *fdr,
                         pddl_iarr_t *plan)
{
    const char *cur = str;

    while (strncmp(cur, "(id-", 4) == 0){
        if (cur[4] < '0' || cur[4] > '9')
            return;
        int op_id = atoi(cur + 4);
        pddlIArrAdd(plan, op_id);

        for (; *cur != '\x0' && *cur != '\n'; ++cur);
        if (*cur == '\n')
            ++cur;
    }
}

static int rolloutExternalFD(pddl_asnets_train_data_t *td,
                             int ground_task_id,
                             const int *state,
                             const pddl_fdr_t *_fdr,
                             char * const *cmd,
                             pddl_bool_t osp_all_soft_goals,
                             pddl_err_t *err)
{
    PANIC_IF(cmd == NULL, "External command is not specified.");
    pddl_fdr_t fdr;
    pddlFDRInitShallowCopyWithDifferentInitState(&fdr, _fdr, state);

    // Write FDR task in FD format into fdrout buffer
    size_t fdrout_size = 0;
    char *fdrout_buf = NULL;
    FILE *fdrout = pddl_strstream(&fdrout_buf, &fdrout_size);
    if (fdrout == NULL){
        ERR_RET(err, -1, "Could not create a buffer for FDR task");
    }

    pddl_fdr_write_config_t wcfg = PDDL_FDR_WRITE_CONFIG_INIT;
    wcfg.fout = fdrout;
    wcfg.fd = pddl_true;
    wcfg.encode_op_ids = pddl_true;
    wcfg.osp_all_soft_goals = osp_all_soft_goals;
    pddlFDRWrite(&fdr, &wcfg);
    fclose(fdrout);

    // Run external planner. Write FDR task to stdin.
    int out_size;
    char *out;
    int oerr_size;
    char *oerr;
    pddl_exec_status_t status;
    int ret = pddlExecvpLimits(cmd, &status, fdrout_buf, fdrout_size, &out,
                               &out_size, &oerr, &oerr_size, -1, -1, err);

    // Free the input buffer
    if (fdrout_buf != NULL)
        FREE(fdrout_buf);

    // Check if running the subprocess failed -- this shouldn't fail
    if (ret != 0){
        if (oerr != NULL)
            FREE(oerr);
        if (out != NULL)
            FREE(out);
        pddlFDRFree(&fdr);
        TRACE_RET(err, -1);
    }

    // If we got something from stderr, then terminate -- this is a bug in
    // the external planner
    if (oerr_size > 0){
        char *outline = oerr;
        while (*outline != '\x0'){
            char *end = outline;
            for (; *end != '\x0' && *end != '\n'; ++end);
            if (*end == '\n'){
                *end = '\x0';
                ++end;
            }
            LOG(err, "Error output: %s", outline);
            outline = end;
        }
    }
    if (oerr != NULL)
        FREE(oerr);


    if (status.exit_status != 0 || status.signaled || oerr_size > 0){
        // Something went wrong with the external planner -- terminate with
        // an error
        if (out != NULL)
            FREE(out);
        pddlFDRFree(&fdr);
        ERR_RET(err, -1, "Calling external teacher failed.");

    }else{
        // Extract plan from from stdout of the external planner
        PDDL_IARR(plan);
        if (out_size > 0)
            stringToPlan(out, &fdr, &plan);

        if (pddlIArrSize(&plan) == 0){
            pddlASNetsTrainDataAddFail(td, ground_task_id, state, fdr.var.var_size);
            LOG(err, "Plan not found");

        }else{
            pddlASNetsTrainDataAddPlan(td, ground_task_id, fdr.var.var_size,
                                       state, &fdr.op, &plan);
        }
        pddlIArrFree(&plan);
    }

    if (out != NULL)
        FREE(out);
    pddlFDRFree(&fdr);
    return 0;
}

int pddlASNetsTrainDataRollout(pddl_asnets_train_data_t *td,
                               int ground_task_id,
                               const int *state,
                               const pddl_fdr_t *fdr,
                               const pddl_asnets_config_t *cfg,
                               pddl_err_t *err)
{
    CTX(err, "ASNets-Teacher-Rollout");
    LOG(err, "start num samples: %d", td->sample_size);
    if (stateExists(td, ground_task_id, state, fdr->var.var_size)){
        LOG(err, "State already in the data pool -- skipping.");
        CTXEND(err);
        return 1;

    }else if (failExists(td, ground_task_id, state, fdr->var.var_size)){
        LOG(err, "State already seen and could not be solved -- skipping.");
        CTXEND(err);
        return 1;
    }

    pddl_heur_config_t heur_cfg = PDDL_HEUR_CONFIG_INIT;
    int ret = 0;
    switch (cfg->teacher){
        case PDDL_ASNETS_TEACHER_ASTAR_LMCUT:
            heur_cfg.fdr = fdr;
            heur_cfg.heur = PDDL_HEUR_LM_CUT;
            ret = rolloutAStar(td, ground_task_id, state, fdr, &heur_cfg,
                               cfg->teacher_timeout, err);
            break;

        case PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD:
            ret = rolloutExternalFD(td, ground_task_id, state, fdr,
                                    cfg->teacher_external_cmd,
                                    cfg->osp_all_soft_goals, err);
            break;
    }

    LOG(err, "num samples: %d", td->sample_size);
    CTXEND(err);
    return ret;
}
