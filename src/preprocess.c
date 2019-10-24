/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include "pddl/mg_strips.h"
#include "pddl/mutex_pair.h"
#include "pddl/critical_path.h"
#include "pddl/irrelevance.h"
#include "pddl/preprocess.h"

int pddlPruneFDR(pddl_fdr_t *fdr, bor_err_t *err)
{
    BOR_INFO(err, "Pruning of FDR. ops: %d, facts: %d, vars: %d",
             fdr->op.op_size, fdr->var.global_id_size, fdr->var.var_size);

    pddl_mg_strips_t mg_strips;
    pddl_mutex_pairs_t mutex;

    pddlMGStripsInitFDR(&mg_strips, fdr);
    pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);

    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    if (fdr->has_cond_eff){
        BOR_INFO2(err, "Skipping h^2, because FDR has conditional effects.");

    }else if (pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &mutex,
                         &rm_fact, &rm_op, err) != 0){
        BOR_TRACE_RET(err, -1);
    }

    BOR_ISET(irr_fact);
    BOR_ISET(irr_op);
    BOR_ISET(static_fact);
    if (fdr->has_cond_eff){
        BOR_INFO2(err, "Skipping irrelevance analysis, because FDR has"
                       " conditional effects.");

    }else if (pddlIrrelevanceAnalysis(&mg_strips.strips, &irr_fact, &irr_op,
                                      &static_fact, err) != 0){
        BOR_TRACE_RET(err, -1);
    }
    borISetUnion(&rm_fact, &irr_fact);
    borISetUnion(&rm_op, &irr_op);

    borISetFree(&irr_fact);
    borISetFree(&irr_op);
    borISetFree(&static_fact);

    if (borISetSize(&rm_fact) > 0 || borISetSize(&rm_op) > 0)
        pddlFDRReduce(fdr, &rm_fact, &rm_op);

    borISetFree(&rm_op);
    borISetFree(&rm_fact);
    pddlMutexPairsFree(&mutex);
    pddlMGStripsFree(&mg_strips);

    BOR_INFO(err, "Pruning of FDR DONE. ops: %d, facts: %d, vars: %d",
             fdr->op.op_size, fdr->var.global_id_size, fdr->var.var_size);

    return 0;
}
