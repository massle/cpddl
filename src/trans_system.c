/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
 * AI Center, Department of Computer Science,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
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
#include "pddl/trans_system.h"
#include "assert.h"


void pddlMGroupIdxPairsFree(pddl_mgroup_idx_pairs_t *p)
{
    if (p->mgroup_idx != NULL)
        BOR_FREE(p->mgroup_idx);
}

void pddlMGroupIdxPairsAdd(pddl_mgroup_idx_pairs_t *p, int mg_id, int idx)
{
    if (p->mgroup_idx_size == p->mgroup_idx_alloc){
        if (p->mgroup_idx_alloc == 0){
            p->mgroup_idx_alloc = 1;
        }else{
            p->mgroup_idx_alloc *= 2;
        }
        p->mgroup_idx = BOR_REALLOC_ARR(p->mgroup_idx,
                                        pddl_mgroup_idx_pair_t,
                                        p->mgroup_idx_alloc);
    }
    pddl_mgroup_idx_pair_t *pair = p->mgroup_idx + p->mgroup_idx_size++;
    pair->mg_id = mg_id;
    pair->fact_idx = idx;
}

static int findFactMGroupIdx(const pddl_trans_systems_t *tss,
                             int mgroup_id,
                             int fact)
{
    const pddl_mgroup_idx_pairs_t *p = tss->fact_to_mgroup + fact;
    for (int i = 0; i < p->mgroup_idx_size; ++i){
        if (p->mgroup_idx[i].mg_id == mgroup_id)
            return p->mgroup_idx[i].fact_idx;
    }
    return -1;
}

static void tssInitSetDeadLabels(pddl_trans_systems_t *tss,
                                 const bor_iset_t *all_used_labels)
{
    if (borISetSize(all_used_labels) == tss->label_op_size)
        return;
    int op_id, cur;
    for (op_id = 0, cur = 0;
            op_id < tss->label_op_size && cur < borISetSize(all_used_labels);
            ++op_id){
        if (op_id == borISetGet(all_used_labels, cur)){
            ++cur;
        }else{
            borISetAdd(&tss->dead_labels, op_id);
        }
    }
    for (; op_id < tss->label_op_size; ++op_id)
        borISetAdd(&tss->dead_labels, op_id);
}

static void constructTransitions(pddl_trans_system_t *ts,
                                 const pddl_mg_strips_t *mg_strips,
                                 const pddl_mutex_pairs_t *mutex,
                                 const bor_iset_t *mgroup)
{
    // TODO: Split into multiple functions
    pddl_trans_systems_t *tss = ts->trans_systems;
    int mgroup_id = borISetGet(&ts->mgroup_ids, 0);
    int mgroup_size = borISetSize(mgroup);
    bor_iset_t *in_ops = BOR_CALLOC_ARR(bor_iset_t, mgroup_size);
    bor_iset_t *out_ops = BOR_CALLOC_ARR(bor_iset_t, mgroup_size);
    bor_iset_t *loop = BOR_CALLOC_ARR(bor_iset_t, mgroup_size);
    BOR_ISET(fset);
    BOR_ISET(eff);
    BOR_ISET(ops);

    int dead_cur = 0;
    for (int op_id = 0; op_id < mg_strips->strips.op.op_size; ++op_id){
        // Skip dead labels
        if (dead_cur < borISetSize(&tss->dead_labels)
                && borISetGet(&tss->dead_labels, dead_cur) == op_id){
            ++dead_cur;
            continue;
        }

        const pddl_strips_op_t *op = mg_strips->strips.op.op[op_id];
        borISetIntersect2(&fset, &op->del_eff, mgroup);
        if (borISetSize(&fset) > 0){
            ASSERT(borISetSize(&fset) == 1);
            int fact = borISetGet(&fset, 0);
            int fact_idx = findFactMGroupIdx(tss, mgroup_id, fact);
            borISetAdd(out_ops + fact_idx, op_id);
        }else{
            borISetMinus2(&eff, &op->pre, &op->del_eff);
            borISetUnion(&eff, &op->add_eff);
            for (int idx = 0; idx < mgroup_size; ++idx){
                int fact = borISetGet(mgroup, idx);
                if (!pddlMutexPairsIsMutexFactSet(mutex, fact, &op->pre)
                        && !pddlMutexPairsIsMutexFactSet(mutex, fact, &eff)){
                    borISetAdd(loop + idx, op_id);
                }
            }
        }

        borISetIntersect2(&fset, &op->add_eff, mgroup);
        if (borISetSize(&fset) > 0){
            ASSERT(borISetSize(&fset) == 1);
            int fact = borISetGet(&fset, 0);
            int fact_idx = findFactMGroupIdx(tss, mgroup_id, fact);
            borISetAdd(in_ops + fact_idx, op_id);
            if (!pddlMutexPairsIsMutexFactSet(mutex, fact, &op->pre))
                borISetAdd(loop + fact_idx, op_id);
        }
    }

    BOR_ISET(all_used_labels);
    for (int s1 = 0; s1 < mgroup_size; ++s1){
        for (int s2 = 0; s2 < mgroup_size; ++s2){
            if (s1 == s2){
                if (borISetSize(loop + s1) == 0)
                    continue;
                pddl_trans_system_label_set_t *l;
                l = pddlTransSystemLabelsAdd(&tss->label, loop + s1);
                if (pddlLabeledTransitionsSetAdd(&ts->trans, l, s1, s1) == 1){
                    pddlTransSystemLabelsDecRef(&tss->label, l);
                }else{
                    borISetUnion(&all_used_labels, &l->label);
                }

            }else{
                borISetIntersect2(&ops, out_ops + s1, in_ops + s2);
                if (borISetSize(&ops) == 0)
                    continue;

                pddl_trans_system_label_set_t *l;
                l = pddlTransSystemLabelsAdd(&tss->label, &ops);
                if (pddlLabeledTransitionsSetAdd(&ts->trans, l, s1, s2) == 1){
                    pddlTransSystemLabelsDecRef(&tss->label, l);
                }else{
                    borISetUnion(&all_used_labels, &l->label);
                }
            }
        }
    }

    tssInitSetDeadLabels(tss, &all_used_labels);
    borISetFree(&all_used_labels);

    borISetFree(&ops);
    borISetFree(&eff);
    borISetFree(&fset);
    for (int i = 0; i < mgroup_size; ++i){
        borISetFree(in_ops + i);
        borISetFree(out_ops + i);
        borISetFree(loop + i);
    }
    BOR_FREE(in_ops);
    BOR_FREE(out_ops);
    BOR_FREE(loop);
}

static void setInitState(pddl_trans_system_t *ts,
                         const bor_iset_t *mgroup,
                         const pddl_strips_t *strips)
{
    BOR_ISET(init);
    borISetIntersect2(&init, mgroup, &strips->init);
    ASSERT_RUNTIME(borISetSize(&init) == 1);
    for (int idx = 0; idx < borISetSize(mgroup); ++idx){
        if (borISetGet(mgroup, idx) == borISetGet(&init, 0)){
            ts->init_state = idx;
            break;
        }
    }
    borISetFree(&init);
}

static void setGoalStates(pddl_trans_system_t *ts,
                          const bor_iset_t *mgroup,
                          const pddl_strips_t *strips,
                          const pddl_mutex_pairs_t *mutex)
{
    int mg_size = borISetSize(mgroup);
    BOR_ISET(goal);
    borISetIntersect2(&goal, mgroup, &strips->goal);
    if (borISetSize(&goal) == 0){
        for (int idx = 0; idx < mg_size; ++idx){
            int fact = borISetGet(mgroup, idx);
            if (!pddlMutexPairsIsMutexFactSet(mutex, fact, &strips->goal))
                borISetAdd(&ts->goal_states, idx);
        }

    }else{
        int goal_size = borISetSize(&goal);
        int goal_id = 0;
        for (int idx = 0; idx < mg_size && goal_id < goal_size; ++idx){
            if (borISetGet(mgroup, idx) == borISetGet(&goal, goal_id)){
                borISetAdd(&ts->goal_states, idx);
                ++goal_id;
            }
        }
    }
    borISetFree(&goal);
}

pddl_trans_system_t *pddlTransSystemNewMGroup(pddl_trans_systems_t *tss,
                                              const pddl_mg_strips_t *mg_strips,
                                              const pddl_mutex_pairs_t *mutex,
                                              int mg_id)
{
    const pddl_mgroup_t *mg = mg_strips->mg.mgroup + mg_id;
    ASSERT_RUNTIME(mg->is_exactly_one);

    pddl_trans_system_t *ts = BOR_ALLOC(pddl_trans_system_t);
    bzero(ts, sizeof(*ts));
    ts->trans_systems = tss;
    borISetAdd(&ts->mgroup_ids, mg_id);
    ts->num_states = borISetSize(&mg->mgroup);
    ts->repr = pddlCascadingTableNewLeaf(mg_id, borISetSize(&mg->mgroup));
    ASSERT(pddlCascadingTableSize(ts->repr) == ts->num_states);
    pddlLabeledTransitionsSetInit(&ts->trans);

    setInitState(ts, &mg->mgroup, &mg_strips->strips);
    setGoalStates(ts, &mg->mgroup, &mg_strips->strips, mutex);

    constructTransitions(ts, mg_strips, mutex, &mg->mgroup);
    pddlLabeledTransitionsSetSort(&ts->trans);

    return ts;
}

static void mergeAddTransitions(pddl_trans_system_t *t,
                                const pddl_labeled_transitions_t *tr1,
                                const pddl_labeled_transitions_t *tr2,
                                const bor_iset_t *label)
{
    if (borISetSize(label) == 0)
        return;

    pddl_labeled_transitions_t *ltr = NULL;
    for (int tr1i = 0; tr1i < tr1->trans.trans_size; ++tr1i){
        int f1 = tr1->trans.trans[tr1i].from;
        int t1 = tr1->trans.trans[tr1i].to;
        for (int tr2i = 0; tr2i < tr2->trans.trans_size; ++tr2i){
            int f2 = tr2->trans.trans[tr2i].from;
            int t2 = tr2->trans.trans[tr2i].to;
            int from = pddlCascadingTableMergeValue(t->repr, f1, f2);
            int to = pddlCascadingTableMergeValue(t->repr, t1, t2);
            ASSERT(from >= 0 && from < t->num_states);
            ASSERT(to >= 0 && to < t->num_states);

            if (ltr == NULL){
                pddl_trans_systems_t *tss = t->trans_systems;
                pddl_trans_system_label_set_t *l;
                l = pddlTransSystemLabelsAdd(&tss->label, label);
                int added;
                ltr = pddlLabeledTransitionsSetAddLabel(&t->trans, l, &added);
                if (!added){
                    ASSERT(l->ref > 1);
                    pddlTransSystemLabelsDecRef(&tss->label, l);
                }
            }
            pddlTransitionsAdd(&ltr->trans, from, to);
        }
    }
}

pddl_trans_system_t *pddlTransSystemNewMerge(pddl_trans_systems_t *tss,
                                             const pddl_trans_system_t *t1,
                                             const pddl_trans_system_t *t2,
                                             const pddl_mutex_pairs_t *mutex)
{
    pddl_trans_system_t *ts = BOR_ALLOC(pddl_trans_system_t);
    bzero(ts, sizeof(*ts));
    ts->trans_systems = tss;
    borISetUnion2(&ts->mgroup_ids, &t1->mgroup_ids, &t2->mgroup_ids);
    ts->num_states = t1->num_states * t2->num_states;
    ts->repr = pddlCascadingTableMerge(t1->repr, t2->repr);
    ASSERT(ts->num_states == pddlCascadingTableSize(ts->repr));
    // TODO: use mutexes to prune unreachable states if possible

    // Construct transitions by iterating over all pairs of transitions
    // from both t1 and t2
    pddlLabeledTransitionsSetInit(&ts->trans);
    BOR_ISET(label);
    for (int t1i = 0; t1i < t1->trans.trans_size; ++t1i){
        const pddl_labeled_transitions_t *tr1 = t1->trans.trans + t1i;
        for (int t2i = 0; t2i < t2->trans.trans_size; ++t2i){
            const pddl_labeled_transitions_t *tr2 = t2->trans.trans + t2i;
            borISetIntersect2(&label, &tr1->label->label, &tr2->label->label);
            mergeAddTransitions(ts, tr1, tr2, &label);
        }
    }
    borISetFree(&label);
    pddlLabeledTransitionsSetSort(&ts->trans);

    // Set initial state
    ts->init_state = pddlCascadingTableMergeValue(ts->repr, t1->init_state,
                                                  t2->init_state);
    // Find all goal states
    int g1, g2;
    BOR_ISET_FOR_EACH(&t1->goal_states, g1){
        BOR_ISET_FOR_EACH(&t2->goal_states, g2){
            int g = pddlCascadingTableMergeValue(ts->repr, g1, g2);
            borISetAdd(&ts->goal_states, g);
        }
    }
    return ts;
}

static void freeLabeledTransitions(pddl_trans_system_t *ts)
{
    pddl_trans_systems_t *tss = ts->trans_systems;
    for (int ti = 0; ti < ts->trans.trans_size; ++ti){
        pddl_labeled_transitions_t *t = ts->trans.trans + ti;
        pddlTransSystemLabelsDecRef(&tss->label, t->label);
    }
    pddlLabeledTransitionsSetFree(&ts->trans);
}

void pddlTransSystemDel(pddl_trans_system_t *ts)
{
    borISetFree(&ts->mgroup_ids);
    pddlCascadingTableDel(ts->repr);
    freeLabeledTransitions(ts);
    borISetFree(&ts->goal_states);
    BOR_FREE(ts);
}

static void removeDeadLabels(pddl_trans_systems_t *tss,
                             pddl_trans_system_t *ts)
{
    if (borISetSize(&tss->dead_labels) == 0)
        return;

    pddl_labeled_transitions_set_t trans;
    pddlLabeledTransitionsSetInit(&trans);

    BOR_ISET(labels);
    for (int ti = 0; ti < ts->trans.trans_size; ++ti){
        pddl_labeled_transitions_t *t = ts->trans.trans + ti;
        borISetMinus2(&labels, &t->label->label, &tss->dead_labels);
        if (borISetSize(&labels) == 0)
            continue;

        // Replace the current label with the label set without dead labels
        pddl_trans_system_label_set_t *l;
        l = pddlTransSystemLabelsAdd(&tss->label, &labels);
        for (int trans_i = 0; trans_i < t->trans.trans_size; ++trans_i){
            int from = t->trans.trans[trans_i].from;
            int to = t->trans.trans[trans_i].to;
            if (pddlLabeledTransitionsSetAdd(&trans, l, from, to) == 1
                    && trans_i == 0){
                pddlTransSystemLabelsDecRef(&tss->label, l);
            }
        }
    }
    borISetFree(&labels);

    // Replace old set with the new one
    freeLabeledTransitions(ts);
    ts->trans = trans;
    pddlLabeledTransitionsSetSort(&ts->trans);
}

void pddlTransSystemsInit(pddl_trans_systems_t *tss,
                          const pddl_mg_strips_t *mg_strips,
                          const pddl_mutex_pairs_t *mutex)
{
    if (mg_strips->strips.has_cond_eff){
        fprintf(stderr, "Fatal Error: trans_system module does not support"
                        " conditional effects!\n");
        exit(-1);
    }

    bzero(tss, sizeof(*tss));
    tss->fact_size = mg_strips->strips.fact.fact_size;
    pddlMGroupsInitCopy(&tss->mgroup, &mg_strips->mg);

    tss->label_op_size = mg_strips->strips.op.op_size;
    tss->label_op = BOR_ALLOC_ARR(pddl_trans_systems_label_op_t,
                                  tss->label_op_size);
    for (int oi = 0; oi < mg_strips->strips.op.op_size; ++oi){
        tss->label_op[oi].op_id = oi;
        tss->label_op[oi].cost = mg_strips->strips.op.op[oi]->cost;
    }

    pddlTransSystemLabelsInit(&tss->label);

    tss->fact_to_mgroup = BOR_CALLOC_ARR(pddl_mgroup_idx_pairs_t,
                                         tss->fact_size);
    for (int mgi = 0; mgi < mg_strips->mg.mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mg_strips->mg.mgroup + mgi;
        ASSERT_RUNTIME(mg->is_exactly_one);
        for (int i = 0; i < borISetSize(&mg->mgroup); ++i){
            int fact = borISetGet(&mg->mgroup, i);
            pddlMGroupIdxPairsAdd(tss->fact_to_mgroup + fact, mgi, i);
        }
    }

    tss->ts_alloc = 1;
    while (tss->ts_alloc < mg_strips->mg.mgroup_size)
        tss->ts_alloc *= 2;
    tss->ts_size = mg_strips->mg.mgroup_size;
    tss->ts = BOR_ALLOC_ARR(pddl_trans_system_t *, tss->ts_alloc);
    for (int i = 0; i < mg_strips->mg.mgroup_size; ++i)
        tss->ts[i] = pddlTransSystemNewMGroup(tss, mg_strips, mutex, i);

    if (borISetSize(&tss->dead_labels) > 0){
        for (int i = 0; i < mg_strips->mg.mgroup_size; ++i)
            removeDeadLabels(tss, tss->ts[i]);
    }
}

void pddlTransSystemsFree(pddl_trans_systems_t *tss)
{
    pddlMGroupsFree(&tss->mgroup);
    if (tss->label_op != NULL)
        BOR_FREE(tss->label_op);
    borISetFree(&tss->dead_labels);

    for (int i = 0; i < tss->ts_size; ++i)
        pddlTransSystemDel(tss->ts[i]);
    if (tss->ts != NULL)
        BOR_FREE(tss->ts);

    pddlTransSystemLabelsFree(&tss->label);

    for (int f = 0; f < tss->fact_size; ++f)
        pddlMGroupIdxPairsFree(tss->fact_to_mgroup + f);
    if (tss->fact_to_mgroup != NULL)
        BOR_FREE(tss->fact_to_mgroup);
}


int pddlTransSystemsMerge(pddl_trans_systems_t *tss,
                          int t1,
                          int t2,
                          const pddl_mutex_pairs_t *mutex)
{
    pddl_trans_system_t *ts;
    ts = pddlTransSystemNewMerge(tss, tss->ts[t1], tss->ts[t2], mutex);

    if (tss->ts_size == tss->ts_alloc){
        if (tss->ts_alloc == 0)
            tss->ts_alloc = 4;
        tss->ts_alloc *= 2;
        tss->ts = BOR_REALLOC_ARR(tss->ts, pddl_trans_system_t *,
                                  tss->ts_alloc);
    }

    int ts_id = tss->ts_size++;
    tss->ts[ts_id] = ts;
    return ts_id;
}

void pddlTransSystemsPrintTS(const pddl_trans_systems_t *tss,
                             const pddl_strips_t *strips,
                             int ts_id,
                             FILE *fout)
{
    const pddl_trans_system_t *ts = tss->ts[ts_id];
    fprintf(fout, "TS: id: %d, num-states: %d\n", ts_id, ts->num_states);
    int mg_id;
    BOR_ISET_FOR_EACH(&ts->mgroup_ids, mg_id){
        int fact;
        fprintf(fout, "  MG %d:", mg_id);
        BOR_ISET_FOR_EACH(&tss->mgroup.mgroup[mg_id].mgroup, fact)
            fprintf(fout, " %d:(%s)", fact, strips->fact.fact[fact]->name);
        fprintf(fout, "\n");
    }

    fprintf(fout, "  Init: %d\n", ts->init_state);
    fprintf(fout, "  Goal:");
    int state;
    BOR_ISET_FOR_EACH(&ts->goal_states, state)
        fprintf(fout, " %d", state);
    fprintf(fout, "\n");

    fprintf(fout, "  Trans[%d]:\n", ts->trans.trans_size);
    for (int ti = 0; ti < ts->trans.trans_size; ++ti){
        const pddl_labeled_transitions_t *trans = ts->trans.trans + ti;
        int op_id;
        BOR_ISET_FOR_EACH(&trans->label->label, op_id){
            const pddl_strips_op_t *op = strips->op.op[op_id];
            fprintf(fout, "    %d:(%s)\n", op_id, op->name);
        }
        fprintf(fout, "     ");
        for (int i = 0; i < trans->trans.trans_size; ++i){
            fprintf(fout, " %d->%d", trans->trans.trans[i].from,
                                     trans->trans.trans[i].to);
        }
        fprintf(fout, "\n");
    }
}

void pddlTransSystemsPrintTS2(const pddl_trans_systems_t *tss,
                              const pddl_strips_t *strips,
                              int ts_id,
                              FILE *fout)
{
    const pddl_trans_system_t *ts = tss->ts[ts_id];
    fprintf(fout, "TS: id: %d, num-states: %d", ts_id, ts->num_states);
    fprintf(fout, ", init: %d", ts->init_state);
    fprintf(fout, ", goal:");
    int state;
    BOR_ISET_FOR_EACH(&ts->goal_states, state)
        fprintf(fout, " %d", state);

    fprintf(fout, ", trans:");
    for (int ti = 0; ti < ts->trans.trans_size; ++ti){
        const pddl_labeled_transitions_t *trans = ts->trans.trans + ti;
        fprintf(fout, " %d:[", borISetSize(&trans->label->label));
        for (int i = 0; i < trans->trans.trans_size; ++i){
            if (i != 0)
                fprintf(fout, " ");
            fprintf(fout, "%d->%d", trans->trans.trans[i].from,
                                    trans->trans.trans[i].to);
        }
        fprintf(fout, "]");
    }
    fprintf(fout, "\n");
}

void pddlTransSystemsPrintDebug1(const pddl_trans_systems_t *tss,
                                 const pddl_strips_t *strips,
                                 FILE *fout)
{
    for (int i = 0; i < tss->ts_size; ++i)
        pddlTransSystemsPrintTS(tss, strips, i, fout);
}

void pddlTransSystemsPrintDebug2(const pddl_trans_systems_t *tss,
                                 const pddl_strips_t *strips,
                                 FILE *fout)
{
    for (int i = 0; i < tss->ts_size; ++i)
        pddlTransSystemsPrintTS2(tss, strips, i, fout);
}
