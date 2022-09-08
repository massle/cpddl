/***
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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

#include "internal.h"
#include "pddl/op_mutex_pair.h"
#include "pddl/sym.h"
#include "pddl/lp.h"

struct redundant {
    int op_size;
    pddl_iset_t *op_mutex;
    pddl_iset_t *op_sym;
    pddl_iset_t relevant_ops;
    int *op_to_relevant_op;
};
typedef struct redundant redundant_t;

static void redundantInit(redundant_t *red,
                          const pddl_strips_t *strips,
                          const pddl_strips_sym_t *sym,
                          const pddl_op_mutex_pairs_t *op_mutex,
                          pddl_err_t *err)
{
    bzero(red, sizeof(*red));
    red->op_size = strips->op.op_size;

    LOG2(err, "Computing transitive closures on symmetries...");
    red->op_sym = CALLOC_ARR(pddl_iset_t, red->op_size);
    for (int op_id = 0; op_id < red->op_size; ++op_id){
        pddlStripsSymOpTransitiveClosure(sym, op_id, red->op_sym + op_id);
        pddlISetRm(red->op_sym + op_id, op_id);
        if (pddlISetSize(red->op_sym + op_id) > 0)
            pddlISetAdd(&red->relevant_ops, op_id);
    }
    LOG(err, "Symmetry-relevant ops: %d", pddlISetSize(&red->relevant_ops));

    LOG2(err, "Collecting op-mutexes...");
    red->op_mutex = CALLOC_ARR(pddl_iset_t, red->op_size);
    pddlOpMutexPairsGenMapOpToOpSet(op_mutex, &red->relevant_ops, red->op_mutex);

    LOG2(err, "Removing irrelevant operators...");
    int change = 1;
    while (change){
        change = 0;
        for (int op_id = 0; op_id < red->op_size; ++op_id){
            int opm_size = pddlISetSize(red->op_mutex + op_id);
            int sym_size = pddlISetSize(red->op_sym + op_id);
            if ((opm_size > 0 || sym_size > 0)
                    && (opm_size == 0 || sym_size == 0)){
                pddlISetEmpty(red->op_mutex + op_id);
                pddlISetEmpty(red->op_sym + op_id);
                pddlISetRm(&red->relevant_ops, op_id);
                change = 1;
                continue;
            }

            pddlISetIntersect(red->op_mutex + op_id, &red->relevant_ops);
            pddlISetIntersect(red->op_sym + op_id, &red->relevant_ops);
            if ((opm_size > 0 && pddlISetSize(red->op_mutex + op_id) == 0)
                    || (sym_size > 0 && pddlISetSize(red->op_sym + op_id) == 0)){
                pddlISetEmpty(red->op_mutex + op_id);
                pddlISetEmpty(red->op_sym + op_id);
                pddlISetRm(&red->relevant_ops, op_id);
                change = 1;
                continue;
            }

            if (opm_size != pddlISetSize(red->op_mutex + op_id)
                    || sym_size != pddlISetSize(red->op_sym + op_id)){
                change = 1;
            }
        }
    }
    LOG(err, "Relevant ops: %{relevant_ops}d", pddlISetSize(&red->relevant_ops));

    red->op_to_relevant_op = ALLOC_ARR(int, red->op_size);
    for (int op_id = 0; op_id < red->op_size; ++op_id)
        red->op_to_relevant_op[op_id] = -1;
    for (int i = 0; i < pddlISetSize(&red->relevant_ops); ++i){
        int op_id = pddlISetGet(&red->relevant_ops, i);
        red->op_to_relevant_op[op_id] = i;
    }
    LOG2(err, "Preparation done.");
}

static void redundantFree(redundant_t *red)
{
    for (int op_id = 0; op_id < red->op_size; ++op_id)
        pddlISetFree(red->op_mutex + op_id);
    if (red->op_mutex != NULL)
        FREE(red->op_mutex);
    for (int op_id = 0; op_id < red->op_size; ++op_id)
        pddlISetFree(red->op_sym + op_id);
    if (red->op_sym != NULL)
        FREE(red->op_sym);
    pddlISetFree(&red->relevant_ops);
    if (red->op_to_relevant_op != NULL)
        FREE(red->op_to_relevant_op);
}

void pddlOpMutexMaxRedundantSet(const pddl_strips_t *strips,
                                const pddl_strips_sym_t *sym,
                                const pddl_op_mutex_pairs_t *op_mutex,
                                pddl_iset_t *redundant,
                                pddl_err_t *err)
{
    CTX(err, "op_mutex_max_redundant", "OPM-Max-Redundant");
    if (sym->gen_size == 0 || op_mutex->num_op_mutex_pairs == 0){
        LOG2(err, "Found 0 redundant ops");
        CTXEND(err);
        return;
    }

    redundant_t red;
    redundantInit(&red, strips, sym, op_mutex, err);
    LOG(err, "Relevant ops: %{num_relevant_ops}d / %d",
        pddlISetSize(&red.relevant_ops), red.op_size);

    if (pddlISetSize(&red.relevant_ops) == 0){
        LOG2(err, "Found 0 redundant ops");
        redundantFree(&red);
        CTXEND(err);
        return;
    }

    unsigned lp_config = PDDL_LP_MAX;
    int num_ops = pddlISetSize(&red.relevant_ops);
    int num_vars = 2 * num_ops;
    LOG(err, "LP vars: %{num_lp_vars}d", num_vars);
    LOG(err, "LP rows: %{num_lp_rows}d", 2 * num_ops);

    pddl_lp_t *lp = pddlLPNew(2 * num_ops, num_vars, lp_config, err);
    for (int vi = 0; vi < num_vars; ++vi){
        pddlLPSetVarBinary(lp, vi);
        if (vi < num_ops)
            pddlLPSetObj(lp, vi, 1.);
    }

    for (int oi = 0; oi < num_ops; ++oi){
        int row1 = oi;
        int row2 = oi + num_ops;
        int op_id = pddlISetGet(&red.relevant_ops, oi);

        PDDL_ISET(not_connected);
        pddlISetMinus2(&not_connected, &red.relevant_ops, red.op_mutex + op_id);
        ASSERT(pddlISetIn(op_id, &not_connected));
        ASSERT_RUNTIME(pddlISetSize(&not_connected) > 0);
        pddlLPSetRHS(lp, row1, pddlISetSize(&not_connected), 'L');
        pddlLPSetCoef(lp, row1, oi, pddlISetSize(&not_connected));
        int op_id2;
        PDDL_ISET_FOR_EACH(&not_connected, op_id2){
            int oi2 = red.op_to_relevant_op[op_id2];
            pddlLPSetCoef(lp, row1, oi2 + num_ops, 1.);
        }
        pddlISetFree(&not_connected);

        ASSERT_RUNTIME(pddlISetSize(red.op_sym + op_id) > 0);
        pddlLPSetRHS(lp, row2, 0., 'G');
        pddlLPSetCoef(lp, row2, oi, -1.);
        PDDL_ISET_FOR_EACH(red.op_sym + op_id, op_id2){
            int oi2 = red.op_to_relevant_op[op_id2];
            pddlLPSetCoef(lp, row2, oi2 + num_ops, 1.);
        }
    }

    double val;
    double *obj = CALLOC_ARR(double, num_vars);
    LOG2(err, "Solving the ILP problem...");
    if (pddlLPSolve(lp, &val, obj) == 0){
        LOG(err, "Problem solved with objective value %.4f", val);
        int num = 0;
        for (int oi = 0; oi < num_ops; ++oi){
            if (obj[oi] > .5){
                fprintf(stderr, "%d: %s\n",
                        pddlISetGet(&red.relevant_ops, oi),
                        strips->op.op[pddlISetGet(&red.relevant_ops, oi)]->name);
                if (redundant != NULL)
                    pddlISetAdd(redundant, pddlISetGet(&red.relevant_ops, oi));
                ++num;
            }
        }
        for (int oi = 0; oi < num_ops; ++oi){
            if (obj[oi + num_ops] > .5){
                fprintf(stderr, "L%d: %s\n",
                        pddlISetGet(&red.relevant_ops, oi),
                        strips->op.op[pddlISetGet(&red.relevant_ops, oi)]->name);
            }
        }
        LOG(err, "Found %d redundant ops", num);
    }else{
        LOG2(err, "Found 0 redundant ops");
    }
    FREE(obj);
    pddlLPDel(lp);

    redundantFree(&red);

    // TODO: Verify that all operators are indeed redundant

    CTXEND(err);
}
