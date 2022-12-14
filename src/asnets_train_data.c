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


void pddlASNetsTrainDataInit(pddl_asnets_train_data_t *td)
{
    ZEROIZE(td);
    td->htable = pddlHTableNew(htableHash, htableEq, NULL);
    td->fail_cache = pddlHTableNew(htableHash, htableEq, NULL);
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
                                 int *fdr_state_size,
                                 const int **fdr_state)
{
    const pddl_asnets_train_data_sample_t *sample = td->sample[sample_id];
    if (ground_task_id != NULL)
        *ground_task_id = sample->ground_task_id;
    if (selected_op_id != NULL)
        *selected_op_id = sample->selected_op_id;
    if (fdr_state_size != NULL)
        *fdr_state_size = sample->fdr_state_size;
    if (fdr_state != NULL)
        *fdr_state = sample->fdr_state;
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

// TO-DO: remove this and clean up
// void pddlASNetsTrainDataAddPlanFastDownward(pddl_asnets_train_data_t *td,
//                                 int ground_task_id,
//                                 int state_size,
//                                 const int *init_state,
//                                 const pddl_fdr_ops_t *ops,
//                                 const int *plan_ops, // integer array "plan_ops" to hold the op_ids
//                                 const int plan_size) 
// {
//     int state[state_size];
//     memcpy(state, init_state, sizeof(int) * state_size);

//     for (int index = 0; index < plan_size; index++)
//     {
//         pddlASNetsTrainDataAdd(td, ground_task_id, state, state_size, plan_ops[index]);
//         const pddl_fdr_op_t *op = ops->op[plan_ops[index]];
//         pddlFDROpApplyOnStateInPlace(op, state_size, state);
//     }
// }

void pddlASNetsTrainDataShuffle(pddl_asnets_train_data_t *td)
{
    pddl_rand_t rnd;
    pddlRandInitAuto(&rnd);
    for (int dst = td->sample_size - 1; dst > 0; --dst){
        int src = pddlRand(&rnd, 0, dst + 1);
        ASSERT_RUNTIME(src <= dst && src >= 0);
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

int pddlASNetsTrainDataRolloutAStar(pddl_asnets_train_data_t *td,
                                    int ground_task_id,
                                    const int *state,
                                    const pddl_fdr_t *_fdr,
                                    const pddl_heur_config_t *heur_cfg,
                                    float max_time,
                                    pddl_err_t *err)
{
    CTX(err, "asnets_teacher_rollout", "ASNets-Teacher-Rollout");
    LOG(err, "start num samples: %{start_num_samples}d", td->sample_size);
    if (stateExists(td, ground_task_id, state, _fdr->var.var_size)){
        LOG2(err, "State already in the data pool -- skipping.");
        CTXEND(err);
        return 1;

    }else if (failExists(td, ground_task_id, state, _fdr->var.var_size)){
        LOG2(err, "State already seen and could not be solved -- skipping.");
        CTXEND(err);
        return 1;
    }

    pddl_timer_t timer;
    pddlTimerStart(&timer);

    pddl_fdr_t fdr;
    pddlFDRInitShallowCopyWithDifferentInitState(&fdr, _fdr, state);

    pddl_heur_t *heur = pddlHeur(heur_cfg, err);
    if (heur == NULL){
        pddlFDRFree(&fdr);
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    pddl_search_t *search = pddlSearchAStar(&fdr, heur, err);
    if (search == NULL){
        pddlFDRFree(&fdr);
        pddlHeurDel(heur);
        CTXEND(err);
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
        LOG2(err, "Plan found");
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
            LOG2(err, "Search reached time-out");
        LOG2(err, "Plan not found");
    }

    pddlSearchDel(search);
    pddlHeurDel(heur);
    pddlFDRFree(&fdr);
    LOG(err, "num samples: %{num_samples}d", td->sample_size);
    CTXEND(err);
    return 0;
}

int pddlASNetsTrainDataRolloutAStarLMCut(pddl_asnets_train_data_t *td,
                                         int ground_task_id,
                                         const int *state,
                                         const pddl_fdr_t *fdr,
                                         float max_time,
                                         pddl_err_t *err)
{
    pddl_heur_config_t heur_cfg = PDDL_HEUR_CONFIG_INIT;
    heur_cfg.fdr = fdr;
    heur_cfg.heur = PDDL_HEUR_LM_CUT;
    return pddlASNetsTrainDataRolloutAStar(td, ground_task_id, state, fdr,
                                           &heur_cfg, max_time, err);
}

int pddlASNetsTrainDataRolloutFastDownward(pddl_asnets_train_data_t *td,
                                    int ground_task_id,
                                    const int *state,
                                    const pddl_fdr_t *_fdr,
                                    float max_time,
                                    pddl_err_t *err)
{
    CTX(err, "asnets_teacher_rollout_fast_downward", "ASNets-Teacher-Rollout-Fast-Downward");
    LOG(err, "start num samples: %{start_num_samples}d", td->sample_size);
    if (stateExists(td, ground_task_id, state, _fdr->var.var_size)){
        LOG2(err, "State already in the data pool -- skipping.");
        CTXEND(err);
        return 1;

    }else if (failExists(td, ground_task_id, state, _fdr->var.var_size)){
        LOG2(err, "State already seen and could not be solved -- skipping.");
        CTXEND(err);
        return 1;
    }

    // To-Do: for now, not using the timer - instead, passing the time limit to FD via execution command.
    // But later, have to keep track of time passed and kill the task to be safe.
    // pddl_timer_t timer;
    // pddlTimerStart(&timer);

    // copy the planning task and change the initial state
    pddl_fdr_t fdr;
    pddlFDRInitShallowCopyWithDifferentInitState(&fdr, _fdr, state);

    // write the FDR task with changed initial state and op_id mentioned in op_names to sas file 
    // parametrize from config file and append problem name later
    pddl_fdr_write_config_t cfg = PDDL_FDR_WRITE_CONFIG_INIT;
    cfg.fd = 1;
    cfg.encode_op_ids = 1;
    cfg.use_osp_params = 1;
    // temporarily hardcoding filename
    cfg.filename = "pddlopfile.sas";
    pddlFDRWrite(&fdr, &cfg);
    
    
    // attempt to execute fast-downward with the output.sas file
    // TO-DO: make this parametrized using config file
    char *argv[] = {
        "../../auxiliarystuff/fast-downward/fast-downward.py", // TO-DO: pass python executable and filename as argument
        "--build", // anyway to avoid adding this?
        "release64", // anyway to avoid adding this?
        "../../cpddl-asnets/cpddl-dev/pddlopfile.sas", // TO-DO: change to absolute paths + concatenate filename from config
        "--search",
        "astar(lmcut())", 
        NULL
    };
    pddl_exec_status_t status;
    char *solbuf = NULL;
    int solbuf_size;
    LOG2(err, "about to call pddlExecvp() for fd");
    int execret = pddlExecvp(argv, &status, NULL, 0,
                             &solbuf, &solbuf_size, NULL, NULL, err);
    LOG2(err, "returned from pddlExecvp() for fd");
    LOG(err, "execret is %d", execret);
    LOG(err, "solbuf is: %s", solbuf);
    ASSERT_RUNTIME(execret == 0);

    // read plan_ops from output file if successful, else call ..TrainDataAddFail()
    // TO-DO: identify if "plan found" OR "timeout" OR "no solution", i.e, "search exhausted".
    // extract planOps and apply in cpddl to retreive intermediate states and build the plan, then add to train data
    FILE *fin = fopen("sas_plan", "r");
    if (fin == NULL){
        fprintf(stderr, "Error: Failed to open plan_sas file");
        return -1;
    }
    LOG2(err, "successfully opened file sas_plan");

    // TO-DO: change to mmap() for efficiency
    int max_size = 25; // TO-DO: fix this, make it dynamic
    char str[max_size];
    char *str_val;
    int val;
    //pddl_iarr_t *plan_ops;
    PDDL_IARR(plan_ops);
    int plan_size = 0;
    pddlIArrInit(&plan_ops);
    while(!feof(fin)){
        LOG2(err, "attempting to read from fin");
        str_val = fgets(str, max_size, fin);
        if(feof(fin)) {
            break;
        }
        LOG(err, "str_val is: %s", str_val);
        if(str_val[0] == '(') {
            val = strtol(str_val+4,NULL,10); // use regex instead?
            LOG(err, "op_id val is: %d", val);
            pddlIArrAdd(&plan_ops, val);
        }
        else if(str_val[0] == ';') {
            val = strtol(str_val+9,NULL,10); // use regex instead?
            LOG(err, "cost val is: %d", val);
            plan_size = val;
            break; // remaining info irrelevant
        }
    }
    fclose(fin);
    LOG2(err, "fin closed");
    ASSERT(plan_ops.size == plan_size);

    // TO-DO: identify if "plan found" OR "timeout" OR "no solution", i.e, "search exhausted".
    // extract planOps and apply in cppdl to retreive intermediate states and build the plan, then add to train data
    //if (st == PDDL_SEARCH_FOUND){
        LOG2(err, "Plan found");
        pddlASNetsTrainDataAddPlan(td, ground_task_id, fdr.var.var_size,
                                       state, &fdr.op, &plan_ops);
    // }else{
    //     pddlASNetsTrainDataAddFail(td, ground_task_id, state, fdr.var.var_size);
    //     if (st == PDDL_SEARCH_ABORT)
    //         LOG2(err, "Search reached time-out");
    //     LOG2(err, "Plan not found");
    // }
    pddlIArrFree(&plan_ops);
    pddlFDRFree(&fdr);
    LOG(err, "num samples: %{num_samples}d", td->sample_size);
    CTXEND(err);
    return 0;
}

