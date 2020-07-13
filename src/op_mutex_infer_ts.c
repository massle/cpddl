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

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <boruvka/alloc.h>
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include <boruvka/timer.h>
#include "pddl/op_mutex_infer.h"
#include "pddl/trans_system.h"
#include "pddl/trans_system_graph.h"
#include "pddl/critical_path.h"
#include "assert.h"

static void setMemLimit(size_t mem_in_mb)
{
    struct rlimit mem_limit;
    mem_limit.rlim_cur = mem_limit.rlim_max = mem_in_mb * 1024 * 1024;
    setrlimit(RLIMIT_AS, &mem_limit);
}

struct op_mutex {
    int from;
    int to;
    int count_fw;
    int count_bw;
    bor_list_t htable;
} bor_packed;
typedef struct op_mutex op_mutex_t;

struct op_mutex_infer {
    int num_states;
    int num_ops;
    const pddl_trans_system_t *ts;
    bor_iset_t *op_start; /*!< Maps states to operators that start there */
    bor_iset_t *op_end; /*!< Maps states to operators that end there */
    int *op_count_start; /*!< Maps operators to the number of start states */
    int *op_count_end; /*!< Maps operators to the number of end states */
};
typedef struct op_mutex_infer op_mutex_infer_t;


static bor_htable_key_t opMutexHash(const bor_list_t *key, void *ud)
{
    op_mutex_t *om = BOR_LIST_ENTRY(key, op_mutex_t, htable);
    return borFastHash_64(om, sizeof(int) * 2, 13);
}

static int opMutexEq(const bor_list_t *k1, const bor_list_t *k2, void *u)
{
    op_mutex_t *o1 = BOR_LIST_ENTRY(k1, op_mutex_t, htable);
    op_mutex_t *o2 = BOR_LIST_ENTRY(k2, op_mutex_t, htable);
    return o1->from == o2->from && o1->to == o2->to;
}

static int _added = 0;
static void addOpMutexDirect(bor_htable_t *htable, int from, int to)
{
    op_mutex_t *opm = BOR_ALLOC(op_mutex_t);
    if (from < to){
        opm->from = from;
        opm->to = to;
    }else{
        opm->from = to;
        opm->to = from;
    }
    opm->count_fw = 0;
    opm->count_bw = 0;
    borListInit(&opm->htable);

    bor_list_t *found;
    if ((found = borHTableInsertUnique(htable, &opm->htable)) != NULL){
        BOR_FREE(opm);
        opm = BOR_LIST_ENTRY(found, op_mutex_t, htable);
    }else{
        ++_added;
    }

    if (from < to){
        ++opm->count_fw;
    }else{
        ++opm->count_bw;
    }
}

static int isOpMutex(const op_mutex_infer_t *opms, const op_mutex_t *opm)
{
    return opm->count_fw
            == opms->op_count_end[opm->from] * opms->op_count_start[opm->to]
        && opm->count_bw
            == opms->op_count_end[opm->to] * opms->op_count_start[opm->from];
}


static void opMutexInferInit(op_mutex_infer_t *opms,
                             const pddl_trans_system_t *ts)
{
    bzero(opms, sizeof(*opms));
    opms->num_states = ts->num_states;
    opms->num_ops = ts->trans_systems->label.label_size;
    opms->ts = ts;

    opms->op_start = BOR_CALLOC_ARR(bor_iset_t, opms->num_states);
    opms->op_end = BOR_CALLOC_ARR(bor_iset_t, opms->num_states);
    for (int ltri = 0; ltri < ts->trans.trans_size; ++ltri){
        const pddl_labeled_transitions_t *ltr = ts->trans.trans + ltri;
        pddlISetPrintCompressed(&ltr->label->label, stderr);
        fprintf(stderr, ":");
        for (int tri = 0; tri < ltr->trans.trans_size; ++tri){
            int from = ltr->trans.trans[tri].from;
            int to = ltr->trans.trans[tri].to;
            fprintf(stderr, " %d->%d", from, to);
            borISetUnion(opms->op_start + from, &ltr->label->label);
            borISetUnion(opms->op_end + to, &ltr->label->label);
        }
        fprintf(stderr, "\n");
    }

    opms->op_count_start = BOR_CALLOC_ARR(int, opms->num_ops);
    opms->op_count_end = BOR_CALLOC_ARR(int, opms->num_ops);
    for (int s = 0; s < opms->num_states; ++s){
        int op;
        BOR_ISET_FOR_EACH(opms->op_start + s, op)
            opms->op_count_start[op] += 1;
        BOR_ISET_FOR_EACH(opms->op_end + s, op)
            opms->op_count_end[op] += 1;
    }
}

static void opMutexInferFree(op_mutex_infer_t *opms)
{
    BOR_FREE(opms->op_count_start);
    BOR_FREE(opms->op_count_end);
    for (int i = 0; i < opms->num_states; ++i){
        borISetFree(opms->op_start + i);
        borISetFree(opms->op_end + i);
    }
    BOR_FREE(opms->op_start);
    BOR_FREE(opms->op_end);
}

static int opMutexInfer(op_mutex_infer_t *opms,
                        int fd,
                        pddl_op_mutex_pairs_t *m,
                        bor_err_t *err)
{
    pddl_trans_system_graph_t graph;
    pddlTransSystemGraphInit(&graph, opms->ts);

    bor_iset_t *reach_state = BOR_CALLOC_ARR(bor_iset_t, opms->num_states);
    pddlTransSystemGraphFwReachability(&graph, reach_state, 1);

    _added = 0;
    bor_timer_t timer;
    borTimerStart(&timer);
    bor_htable_t *htable = borHTableNew(opMutexHash, opMutexEq, NULL);
    for (int start = 0; start < opms->num_states; ++start){
        /*
        int end = 0;
        for (int i = 0; i < borISetSize(reach_state + start); ++end){
            int r_state = borISetGet(reach_state + start, i);
            if (r_state == end){
                ++i;
            }else{
                fprintf(stderr, "S %d %d\n",
                        borISetSize(opms->op_end + end),
                        borISetSize(opms->op_start + start));
                int op_from;
                BOR_ISET_FOR_EACH(opms->op_end + end, op_from){
                    int op_to;
                    BOR_ISET_FOR_EACH(opms->op_start + start, op_to){
                        if (op_from != op_to){
                            addOpMutexDirect(htable, op_from, op_to);
                        }
                    }
                }
            }
        }
        for (; end < opms->num_states; ++end){
            int op_from;
            BOR_ISET_FOR_EACH(opms->op_end + end, op_from){
                int op_to;
                BOR_ISET_FOR_EACH(opms->op_start + start, op_to){
                    if (op_from != op_to){
                        addOpMutexDirect(htable, op_from, op_to);
                    }
                }
            }
        }
        */
        for (int end = 0; end < opms->num_states; ++end){
            // If start is not reachable from end, then iterate overall
            // operators that are on transitions x->end and start->y and
            // record that start->y cannot appear after x->end.
            if (!borISetIn(end, reach_state + start)){
                int op_from;
                BOR_ISET_FOR_EACH(opms->op_end + end, op_from){
                    int op_to;
                    BOR_ISET_FOR_EACH(opms->op_start + start, op_to){
                        if (op_from != op_to){
                            addOpMutexDirect(htable, op_from, op_to);
                        }
                    }
                }
            }
            fprintf(stderr, "S-E %d-%d -- %d\n", start, end, _added);
        }
    }
    borTimerStop(&timer);
    fprintf(stderr, "T5 %.2f %d %d -- %d\n", borTimerElapsedInSF(&timer),
            opms->num_states, opms->num_ops, _added);
    _added = 0;

    bor_list_t list;
    borListInit(&list);
    borHTableGather(htable, &list);
    while (!borListEmpty(&list)){
        bor_list_t *item = borListNext(&list);
        borListDel(item);
        op_mutex_t *opm = BOR_LIST_ENTRY(item, op_mutex_t, htable);
        if (isOpMutex(opms, opm)){
            if (fd >= 0){
                int data[2] = { opm->from, opm->to };
                ssize_t written = write(fd, data, sizeof(int) * 2);
                while (written != sizeof(int) * 2){
                    void *buf = ((char *)data) + written;
                    written += write(fd, buf, sizeof(int) * 2 - written);
                }
            }

            if (m != NULL){
                pddlOpMutexPairsAdd(m, opm->from, opm->to);
            }
        }
        BOR_FREE(opm);
    }
    borHTableDel(htable);
    borTimerStop(&timer);
    fprintf(stderr, "T6 %.2f %d %d\n", borTimerElapsedInSF(&timer),
            opms->num_states, opms->num_ops);
    for (int s = 0; s < opms->num_states; ++s)
        borISetFree(reach_state + s);
    BOR_FREE(reach_state);
    return 0;
}

static int graphVertOnLine(const pddl_trans_system_graph_t *graph,
                           int vert,
                           int *fw,
                           int *bw)
{
    if (graph->fw[vert].edge_size > 2 || graph->bw[vert].edge_size > 2)
        return 0;

    *fw = *bw = -1;
    for (int i = 0; i < graph->fw[vert].edge_size; ++i){
        int end = graph->fw[vert].edge[i].end;
        if (end != vert){
            if (*fw != -1)
                return 0;
            *fw = end;
        }
    }
    for (int i = 0; i < graph->bw[vert].edge_size; ++i){
        int end = graph->bw[vert].edge[i].end;
        if (end != vert){
            if (*bw != -1)
                return 0;
            *bw = end;
        }
    }
    return 1;
}

static void condenseStraightPaths(pddl_trans_systems_t *tss, int tsi)
{
    const pddl_trans_system_t *ts = tss->ts[tsi];
    if (ts->num_states <= 1)
        return;


    pddl_trans_system_graph_t graph;
    pddlTransSystemGraphInit(&graph, ts);
    int *used_states = BOR_CALLOC_ARR(int, graph.num_states);

    pddl_set_iset_t conds;
    pddlSetISetInit(&conds);

    for (int start = 0; start < graph.num_states; ++start){
        if (used_states[start])
            continue;
        used_states[start] = 1;
        int fw, bw;
        if (graphVertOnLine(&graph, start, &fw, &bw)){
            if (fw < 0 && bw < 0)
                continue;
            BOR_ISET(cond);
            borISetAdd(&cond, start);
            while (fw >= 0){
                ASSERT(!used_states[fw]);
                borISetAdd(&cond, fw);
                used_states[fw] = 1;
                int _bw;
                if (!graphVertOnLine(&graph, fw, &fw, &_bw))
                    break;
            }

            while (bw >= 0){
                ASSERT(!used_states[bw]);
                borISetAdd(&cond, bw);
                used_states[bw] = 1;
                int _fw;
                if (!graphVertOnLine(&graph, bw, &_fw, &bw))
                    break;
            }
            fprintf(stderr, "Cond %d: ", borISetSize(&cond));
            pddlISetPrintCompressed(&cond, stderr);
            fprintf(stderr, "\n");
            pddlSetISetAdd(&conds, &cond);
            borISetFree(&cond);
        }
    }

    if (pddlSetISetSize(&conds) > 0){
        pddl_trans_system_abstr_map_t map;
        pddlTransSystemAbstrMapInit(&map, ts->num_states);
        for (int ci = 0; ci < pddlSetISetSize(&conds); ++ci){
            const bor_iset_t *c = pddlSetISetGet(&conds, ci);
            if (borISetSize(c) > 1)
                pddlTransSystemAbstrMapCondense(&map, c);
        }

        pddlTransSystemAbstrMapFinalize(&map);
        pddlTransSystemsAbstract(tss, tsi, &map);
        pddlTransSystemAbstrMapFree(&map);
    }
    pddlSetISetFree(&conds);
    BOR_FREE(used_states);
    pddlTransSystemGraphFree(&graph);
}

static int transformTransSystemAndFindOpMutexes(int fd,
                                                pddl_op_mutex_pairs_t *m,
                                                pddl_trans_systems_t *tss,
                                                const bor_iset_t *ts_ids,
                                                bor_err_t *err)
{
    int ts_last;

    bor_timer_t timer;
    borTimerStart(&timer);
    if (borISetSize(ts_ids) == 1){
        ts_last = borISetGet(ts_ids, 0);

    }else{
        // Construct the merge
        int t1i = borISetGet(ts_ids, 0);
        int t2i = borISetGet(ts_ids, 1);
        ts_last = pddlTransSystemsMerge(tss, t1i, t2i);
        for (int i = 2; i < borISetSize(ts_ids); ++i){
            t1i = ts_last;
            t2i = borISetGet(ts_ids, i);
            int ts_next = pddlTransSystemsMerge(tss, t1i, t2i);
            pddlTransSystemsDelTransSystem(tss, ts_last);
            ts_last = ts_next;
        }
    }

    pddl_trans_system_abstr_map_t map;
    pddlTransSystemAbstrMapInit(&map, tss->ts[ts_last]->num_states);

    pddl_trans_system_graph_t graph;
    pddlTransSystemGraphInit(&graph, tss->ts[ts_last]);

    // Remove unreachable/dead-end states
    int *dist = BOR_ALLOC_ARR(int, graph.num_states);
    pddlTransSystemGraphFwDist(&graph, dist);
    for (int i = 0; i < graph.num_states; ++i){
        if (dist[i] < 0)
            pddlTransSystemAbstrMapPruneState(&map, i);
    }
    pddlTransSystemGraphBwDist(&graph, dist);
    for (int i = 0; i < graph.num_states; ++i){
        if (dist[i] < 0)
            pddlTransSystemAbstrMapPruneState(&map, i);
    }
    BOR_FREE(dist);

    // Condense strongly connected components
    pddl_set_iset_t comp;
    pddlSetISetInit(&comp);
    pddlTransSystemGraphFwSCC(&graph, &comp);
    for (int ci = 0; ci < pddlSetISetSize(&comp); ++ci){
        const bor_iset_t *c = pddlSetISetGet(&comp, ci);
        if (borISetSize(c) > 1)
            pddlTransSystemAbstrMapCondense(&map, c);
    }
    pddlSetISetFree(&comp);

    pddlTransSystemAbstrMapFinalize(&map);
    pddlTransSystemsAbstract(tss, ts_last, &map);
    pddlTransSystemAbstrMapFree(&map);
    pddlTransSystemGraphFree(&graph);

    //condenseStraightPaths(tss, ts_last);

    if (tss->ts[ts_last]->num_states > 1){
        op_mutex_infer_t opms;
        opMutexInferInit(&opms, tss->ts[ts_last]);
    borTimerStop(&timer);
    fprintf(stderr, "T4 %.2f\n", borTimerElapsedInSF(&timer));
        opMutexInfer(&opms, fd, m, err);
        opMutexInferFree(&opms);
    }
    fprintf(stderr, "X %d %d\n", ts_last, tss->ts_size);
    if (borISetSize(ts_ids) > 1){
        pddlTransSystemsDelTransSystem(tss, ts_last);
        pddlTransSystemsCleanDeletedTransSystems(tss);
    }
    fprintf(stderr, "X %d %d\n", ts_last, tss->ts_size);
    return 0;
}

static void readMutexPairs(pddl_op_mutex_pairs_t *m, int fd_in)
{
    int buf[2];
    ssize_t readlen;

    while ((readlen = read(fd_in, buf, sizeof(int) * 2)) > 0){
        int remain = (sizeof(int) * 2) - readlen;
        while (remain > 0){
            ssize_t r = read(fd_in, buf, remain);
            if (r <= 0)
                return;
            remain -= r;
        }

        ASSERT(readlen == 2 * sizeof(int));
        pddlOpMutexPairsAdd(m, buf[0], buf[1]);
    }
}

static int findOpMutexesWithMemLimit(pddl_op_mutex_pairs_t *m,
                                     pddl_trans_systems_t *tss,
                                     size_t max_mem_in_mb,
                                     const bor_iset_t *ts_ids,
                                     bor_err_t *err)
{
    fprintf(stderr, "mem-limit\n");
    fflush(stderr);
    int fd[2];

    if (pipe(fd) < 0){
        perror("Error: Could not create a pipe:");
        return -1;
    }

    int pid = fork();
    if (pid < 0){
        perror("Error: Could not fork:");
        return -1;

    }else if (pid == 0){
        // child process
        setMemLimit(max_mem_in_mb);
        // close unused read end
        close(fd[0]);
        int r;
        bor_timer_t timer;
        borTimerStart(&timer);
        r = transformTransSystemAndFindOpMutexes(fd[1], NULL, tss, ts_ids, err);
        borTimerStop(&timer);
        fprintf(stderr, "T3 %.2f %d\n", borTimerElapsedInSF(&timer),
                tss->ts[borISetGet(ts_ids, 0)]->num_states);
        fflush(stderr);
        close(fd[1]);
        exit(r);

    }else{
        // parent process
        // close unused write end
        close(fd[1]);
        readMutexPairs(m, fd[0]);

        int ret = 0;
        int wstatus;
        waitpid(pid, &wstatus, 0);
        if (WIFEXITED(wstatus)){
            if (WEXITSTATUS(wstatus) != 0)
                ret = -1;
        }else{
            // TODO: analyase what happened!
            // TODO: Handle out of memory error printout in child!
            ret = -1;
        }

        close(fd[0]);
        return ret;
    }
}

static int findOpMutexesRec(pddl_op_mutex_pairs_t *m,
                            pddl_trans_systems_t *tss,
                            size_t max_mem_mb,
                            const bor_iset_t *ts_ids,
                            int size,
                            bor_err_t *err)
{
    if (size == 0){
        if (max_mem_mb > 0)
            return findOpMutexesWithMemLimit(m, tss, max_mem_mb, ts_ids, err);
        return transformTransSystemAndFindOpMutexes(-1, m, tss, ts_ids, err);
    }

    int ret = 0;
    int tsi_from;
    if (ts_ids == NULL || borISetSize(ts_ids) == 0){
        tsi_from = 0;
    }else{
        tsi_from = borISetGet(ts_ids, borISetSize(ts_ids) - 1) + 1;
    }

    BOR_ISET(ts_ids_next);
    if (ts_ids != NULL)
        borISetUnion(&ts_ids_next, ts_ids);
    for (int tsi = tsi_from; tsi < tss->ts_size; ++tsi){
        if (borISetSize(&tss->ts[tsi]->mgroup_ids) != 1)
            continue;
        borISetAdd(&ts_ids_next, tsi);
        ret = findOpMutexesRec(m, tss, max_mem_mb, &ts_ids_next, size - 1, err);
        fprintf(stderr, "Ret %d -> %d\n", tsi, ret);
        fflush(stderr);
        if (ret != 0)
            break;
        borISetRm(&ts_ids_next, tsi);
    }
    borISetFree(&ts_ids_next);
    return ret;
}

int pddlOpMutexInferTransSystems(pddl_op_mutex_pairs_t *m,
                                 const pddl_mg_strips_t *mg_strips,
                                 const pddl_mutex_pairs_t *mutex,
                                 int merge_size,
                                 size_t max_mem_in_mb,
                                 bor_err_t *err)
{
    BOR_INFO(err, "Computing op-mutex pairs from abstract transition systems."
                  " merge-size: %d", merge_size);
    fprintf(stderr, "INFER %d %ld\n",
            mg_strips->strips.op.op_size,
            (long)mg_strips->strips.op.op_size *
            (long)mg_strips->strips.op.op_size);
    pddl_trans_systems_t tss;
    bor_timer_t timer;
    borTimerStart(&timer);
    pddlTransSystemsInit(&tss, mg_strips, mutex);
    borTimerStop(&timer);
    fprintf(stderr, "T %.2f\n", borTimerElapsedInSF(&timer));
    fflush(stderr);
    borTimerStart(&timer);
    int ret = findOpMutexesRec(m, &tss, max_mem_in_mb, NULL, merge_size, err);
    borTimerStop(&timer);
    fprintf(stderr, "T2 %.2f\n", borTimerElapsedInSF(&timer));
    pddlTransSystemsFree(&tss);
    BOR_INFO(err, "Computing op-mutex pairs from abstract transition"
                  "systems DONE (merge-size: %d)", merge_size);
    return ret;
}
