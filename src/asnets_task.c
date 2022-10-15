/***
 * Copyright (c)2016 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/asnets_task.h"
#include "pddl/strips_ground_datalog.h"
#include "pddl/sort.h"
#include "pddl/critical_path.h"
#include "pddl/lifted_mgroup_infer.h"
#include "internal.h"

static pddl_asnets_task_relate_t *relatednessAdd(pddl_asnets_task_t *task)
{
    if (task->relatedness.rel_size == task->relatedness.rel_alloc){
        if (task->relatedness.rel_alloc == 0)
            task->relatedness.rel_alloc = 2;
        task->relatedness.rel_alloc *= 2;
        task->relatedness.rel = REALLOC_ARR(task->relatedness.rel,
                                            pddl_asnets_task_relate_t,
                                            task->relatedness.rel_alloc);
    }

    pddl_asnets_task_relate_t *rel;
    rel = task->relatedness.rel + task->relatedness.rel_size++;
    return rel;
}

static int relateCmp(const void *a, const void *b, void *_)
{
    const pddl_asnets_task_relate_t *r1 = a;
    const pddl_asnets_task_relate_t *r2 = b;
    int cmp = r1->op_id - r2->op_id;
    if (cmp == 0)
        cmp = r1->position - r2->position;
    if (cmp == 0)
        cmp = r1->fact_id - r2->fact_id;
    return cmp;
}

static int atomEq(const pddl_ground_atom_t *a1,
                  const pddl_fm_atom_t *a2,
                  const pddl_obj_id_t *args)
{
    if (a1->pred != a2->pred)
        return 0;
    for (int argi = 0; argi < a1->arg_size; ++argi){
        pddl_obj_id_t obj2 = a2->arg[argi].obj;
        if (a2->arg[argi].param >= 0)
            obj2 = args[a2->arg[argi].param];
        if (a1->arg[argi] != obj2)
            return 0;
    }
    return 1;
}

static void computeRelatednessOpFact(pddl_asnets_task_t *task,
                                     const pddl_strips_op_t *op,
                                     int fact_id)
{
    pddl_asnets_task_action_t *a = task->pddl_action + op->pddl_action_id;
    const pddl_ground_atom_t *atom = task->strips.fact.fact[fact_id]->ground_atom;
    ASSERT(atom != NULL);

    for (int pos = 0; pos < a->atom.size; ++pos){
        const pddl_fm_atom_t *atom2 = PDDL_FM_CAST(a->atom.cond[pos], atom);
        if (atomEq(atom, atom2, op->action_args)){
            pddl_asnets_task_relate_t *rel = relatednessAdd(task);
            rel->op_id = op->id;
            rel->fact_id = fact_id;
            rel->position = pos;
        }
    }
}

static void computeRelatednessOp(pddl_asnets_task_t *task,
                                 const pddl_strips_op_t *op)
{
    // TODO: Conditional effects not supported yet
    ASSERT(op->cond_eff_size == 0);
    ASSERT(op->action_args != NULL);
    ASSERT(op->pddl_action_id >= 0);

    PDDL_ISET(facts);
    pddlISetUnion(&facts, &op->pre);
    pddlISetUnion(&facts, &op->add_eff);
    pddlISetUnion(&facts, &op->del_eff);
    int fact;
    PDDL_ISET_FOR_EACH(&facts, fact)
        computeRelatednessOpFact(task, op, fact);
    pddlISetFree(&facts);
}

static void computeRelatedness(pddl_asnets_task_t *task)
{
    for (int oi = 0; oi < task->strips.op.op_size; ++oi){
        computeRelatednessOp(task, task->strips.op.op[oi]);
    }
    pddlSort(task->relatedness.rel, task->relatedness.rel_size,
             sizeof(pddl_asnets_task_relate_t), relateCmp, NULL);

}

static void condArrAddUnique(pddl_fm_arr_t *carr,
                             const pddl_fm_atom_t *atom)
{
    for (int i = 0; i < carr->size; ++i){
        const pddl_fm_atom_t *atom2;
        atom2 = PDDL_FM_CAST(carr->cond[i], atom);
        if (pddlFmAtomCmpNoNeg(atom, atom2) == 0)
            return;
    }
    pddlFmArrAdd(carr, &atom->fm);
}

int pddlASNetsTaskInit(pddl_asnets_task_t *task,
                       const char *domain_fn,
                       const char *problem_fn,
                       pddl_err_t *err)
{
    CTX(err, "asnets_task", "ASNets-task");
    ZEROIZE_PTR(task);
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = 1;
    pddl_cfg.normalize = 1;
    pddl_cfg.enforce_unit_cost = 1;
    if (pddlInit(&task->pddl, domain_fn, problem_fn, &pddl_cfg, err) != 0){
        CTXEND(err);
        TRACE_RET(err, -1);
    }
    pddlNormalize(&task->pddl);

    pddl_lifted_mgroups_infer_limits_t lifted_mgroups_limits
            = PDDL_LIFTED_MGROUPS_INFER_LIMITS_INIT;
    pddl_lifted_mgroups_t lifted_mgroups;
    pddlLiftedMGroupsInit(&lifted_mgroups);
    pddlLiftedMGroupsInferFAMGroups(&task->pddl, &lifted_mgroups_limits,
                                    &lifted_mgroups, err);

    task->pddl_action = CALLOC_ARR(pddl_asnets_task_action_t,
                                   task->pddl.action.action_size);
    for (int ai = 0; ai < task->pddl.action.action_size; ++ai){
        pddl_asnets_task_action_t *a = task->pddl_action + ai;
        a->action_id = ai;

        pddl_fm_const_it_t it;
        const pddl_fm_atom_t *atom;
        PDDL_FM_FOR_EACH_ATOM(task->pddl.action.action[ai].pre, &it, atom){
            if (atom->pred != task->pddl.pred.eq_pred)
                condArrAddUnique(&a->atom, atom);
        }
        PDDL_FM_FOR_EACH_ATOM(task->pddl.action.action[ai].eff, &it, atom){
            if (atom->pred != task->pddl.pred.eq_pred)
                condArrAddUnique(&a->atom, atom);
        }
    }

    pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;
    ground_cfg.prune_op_pre_mutex = 0;
    ground_cfg.prune_op_dead_end = 0;
    ground_cfg.remove_static_facts = 0;
    ground_cfg.keep_action_args = 1;
    ground_cfg.keep_all_static_facts = 1;
    if (pddlStripsGroundDatalog(&task->strips, &task->pddl,
                                &ground_cfg, err) != 0){
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    pddl_mutex_pairs_t mutex;
    PDDL_ISET(unreachable_op);
    pddlMutexPairsInitStrips(&mutex, &task->strips);
    pddlH2(&task->strips, &mutex, NULL, &unreachable_op, -1., err);
    pddlStripsReduce(&task->strips, NULL, &unreachable_op);
    pddlISetFree(&unreachable_op);

    pddl_mgroups_t mgroups;
    pddlMGroupsInitEmpty(&mgroups);
    pddlMGroupsGround(&mgroups, &task->pddl, &lifted_mgroups, &task->strips);

    pddlFDRInitFromStrips(&task->fdr, &task->strips, &mgroups, &mutex,
                          PDDL_FDR_VARS_LARGEST_FIRST, 0, err);
    ASSERT_RUNTIME(task->strips.op.op_size == task->fdr.op.op_size);

    computeRelatedness(task);

    pddlMGroupsFree(&mgroups);
    pddlMutexPairsFree(&mutex);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    CTXEND(err);
    return 0;
}

void pddlASNetsTaskFree(pddl_asnets_task_t *task)
{
    for (int i = 0; i < task->pddl.action.action_size; ++i)
        pddlFmArrFree(&task->pddl_action[i].atom);
    if (task->pddl_action != NULL)
        FREE(task->pddl_action);
    if (task->relatedness.rel != NULL)
        FREE(task->relatedness.rel);
    pddlFDRFree(&task->fdr);
    pddlStripsFree(&task->strips);
    pddlFree(&task->pddl);
}

static void printSet(const pddl_iset_t *set, FILE *fout)
{
    fprintf(fout, "%d", pddlISetSize(set));
    int fact;
    PDDL_ISET_FOR_EACH(set, fact)
        fprintf(fout, " %d", fact);
    fprintf(fout, "\n");
}

void pddlASNetsTaskPrintPDDLStrips(const pddl_asnets_task_t *task, FILE *fout)
{
    // Predicates
    fprintf(fout, "%d\n", task->pddl.pred.pred_size);
    for (int pi = 0; pi < task->pddl.pred.pred_size; ++pi){
        fprintf(fout, "%s\n", task->pddl.pred.pred[pi].name);
    }

    // Action schemas
    fprintf(fout, "%d\n", task->pddl.action.action_size);
    for (int ai = 0; ai < task->pddl.action.action_size; ++ai){
        fprintf(fout, "%s\n", task->pddl.action.action[ai].name);
        fprintf(fout, "%d\n", task->pddl_action[ai].atom.size);
        ASSERT(task->pddl_action[ai].action_id == ai);
    }

    // STRIPS Facts
    fprintf(fout, "%d\n", task->strips.fact.fact_size);
    for (int fi = 0; fi < task->strips.fact.fact_size; ++fi){
        fprintf(fout, "%s\n", task->strips.fact.fact[fi]->name);

        fprintf(fout, "%d\n", task->strips.fact.fact[fi]->ground_atom->pred);

        const pddl_iset_t *val_ids = task->fdr.var.strips_id_to_val + fi;
        ASSERT_RUNTIME(pddlISetSize(val_ids) == 1);
        int val_id = pddlISetGet(val_ids, 0);
        const pddl_fdr_val_t *val = task->fdr.var.global_id_to_val[val_id];
        fprintf(fout, "%d %d\n", val->var_id, val->val_id);
    }

    // STRIPS operators
    fprintf(fout, "%d\n", task->strips.op.op_size);
    for (int oi = 0; oi < task->strips.op.op_size; ++oi){
        const pddl_strips_op_t *op = task->strips.op.op[oi];
        fprintf(fout, "%s\n", op->name);
        printSet(&op->pre, fout);
        printSet(&op->add_eff, fout);
        printSet(&op->del_eff, fout);
    }

    // STRIPS Init
    printSet(&task->strips.init, fout);

    // STRIPS Goal
    printSet(&task->strips.goal, fout);

    // Relatedness
    fprintf(fout, "%d\n", task->relatedness.rel_size);
    for (int i = 0; i < task->relatedness.rel_size; ++i){
        const pddl_asnets_task_relate_t *r = task->relatedness.rel + i;
        fprintf(fout, "%d %d %d\n", r->op_id, r->fact_id, r->position);
    }
    fflush(fout);
}

int pddlASNetsTaskDump(const pddl_asnets_task_t *task,
                       const char *fn_pddl_strips,
                       const char *fn_fdr,
                       pddl_err_t *err)
{
    FILE *fout1 = fopen(fn_pddl_strips, "w");
    if (fout1 == NULL)
        ERR_RET(err, -1, "Could not open %s", fn_pddl_strips);

    FILE *fout2 = fopen(fn_fdr, "w");
    if (fout2 == NULL){
        fclose(fout1);
        ERR_RET(err, -1, "Could not open %s", fn_fdr);
    }

    pddlASNetsTaskPrintPDDLStrips(task, fout1);
    pddlFDRPrintFD(&task->fdr, NULL, 0, fout2);

    fclose(fout1);
    fclose(fout2);
    return 0;
}
