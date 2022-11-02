/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/search.h"
#include "pddl/asnets_train_data.h"

static pddl_htable_key_t htableHash(const pddl_list_t *key, void *_)
{
    const pddl_asnets_train_data_row_t *row;
    row = PDDL_LIST_ENTRY(key, pddl_asnets_train_data_row_t, htable);
    return row->hash;
}

static int htableEq(const pddl_list_t *key1, const pddl_list_t *key2, void *_)
{
    const pddl_asnets_train_data_row_t *row1, *row2;
    row1 = PDDL_LIST_ENTRY(key1, pddl_asnets_train_data_row_t, htable);
    row2 = PDDL_LIST_ENTRY(key2, pddl_asnets_train_data_row_t, htable);
    int cmp = row1->ground_task_id - row2->ground_task_id;
    if (cmp == 0){
        ASSERT(row1->fdr_state_size == row2->fdr_state_size);
        cmp = memcmp(row1->fdr_state, row2->fdr_state,
                     sizeof(int) * row1->fdr_state_size);
    }
    return cmp == 0;
}

static size_t rowSize(int state_size)
{
    size_t size = sizeof(pddl_asnets_train_data_row_t);
    size += sizeof(int) * state_size;
    return size;
}

static pddl_asnets_train_data_row_t *rowNew(int ground_task_id,
                                            const int *state,
                                            int state_size,
                                            int selected_op_id)
{
    size_t size = rowSize(state_size);
    pddl_asnets_train_data_row_t *row = MALLOC(size);
    ZEROIZE_RAW(row, size);
    row->ground_task_id = ground_task_id;
    row->selected_op_id = selected_op_id;
    row->fdr_state_size = state_size;
    memcpy(row->fdr_state, state, sizeof(int) * state_size);
    return row;
}

static void rowDel(pddl_asnets_train_data_row_t *row)
{
    FREE(row);
}


void pddlASNetsTrainDataInit(pddl_asnets_train_data_t *td)
{
    ZEROIZE(td);
    td->htable = pddlHTableNew(htableHash, htableEq, NULL);
}

void pddlASNetsTrainDataFree(pddl_asnets_train_data_t *td)
{
    pddlHTableDel(td->htable);
    for (int i = 0; i < td->row_size; ++i)
        rowDel(td->row[i]);
    if (td->row != NULL)
        FREE(td->row);
}

void pddlASNetsTrainDataAdd(pddl_asnets_train_data_t *td,
                            int ground_task_id,
                            const int *state,
                            int state_size,
                            int selected_op_id)
{
    pddl_asnets_train_data_row_t *row;
    row = rowNew(ground_task_id, state, state_size, selected_op_id);

    if (pddlHTableInsertUnique(td->htable, &row->htable) == NULL){
        ARR_MAKE_SPACE(td->row, pddl_asnets_train_data_row_t *,
                       td->row_size, td->row_alloc, 2);
        td->row[td->row_size++] = row;
    }else{
        rowDel(row);
    }
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

int pddlASNetsTrainDataRolloutAStar(pddl_asnets_train_data_t *td,
                                    int ground_task_id,
                                    const int *state,
                                    const pddl_fdr_t *_fdr,
                                    const pddl_heur_config_t *heur_cfg,
                                    float max_time,
                                    pddl_err_t *err)
{
    CTX(err, "asnets_teacher_rollout", "ASNets-Teacher-Rollout");
    LOG(err, "start num rows: %{start_num_rows}d", td->row_size);

    pddl_timer_t timer;
    pddlTimerStart(&timer);

    // TODO: This is a hacky way to change the initial state without
    //       copying the whole planning task
    pddl_fdr_t fdr = *_fdr;
    fdr.init = (int *)state;

    pddl_heur_t *heur = pddlHeur(heur_cfg, err);
    if (heur == NULL){
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    pddl_search_t *search = pddlSearchAStar(&fdr, heur, err);
    if (search == NULL){
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
        LOG2(err, "Plan not found");
    }

    pddlSearchDel(search);
    pddlHeurDel(heur);
    LOG(err, "num rows: %{num_rows}d", td->row_size);
    CTXEND(err);
    return 0;
}
