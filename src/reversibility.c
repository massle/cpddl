/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#include <boruvka/sort.h>
#include "pddl/reversibility.h"
#include "alloc.h"

static void pddlConjFactFormulaFree(pddl_conj_fact_formula_t *f)
{
    borISetFree(&f->pos);
    borISetFree(&f->neg);
}

static void pddlReversePlanFree(pddl_reverse_plan_t *r)
{
    pddlConjFactFormulaFree(&r->formula);
    borIArrFree(&r->plan);
}

void pddlReversibilityUniformInit(pddl_reversibility_uniform_t *r)
{
    bzero(r, sizeof(*r));
}

void pddlReversibilityUniformFree(pddl_reversibility_uniform_t *r)
{
    for (int i = 0; i < r->plan_size; ++i){
        pddlReversePlanFree(r->plan + i);
    }
    if (r->plan != NULL)
        FREE(r->plan);
}

static int revPlanCmp(const void *a, const void *b, void *ud)
{
    const pddl_reverse_plan_t *p1 = a;
    const pddl_reverse_plan_t *p2 = b;
    int cmp = p1->reversible_op_id - p2->reversible_op_id;
    if (cmp == 0)
        cmp = borISetCmp(&p1->formula.pos, &p2->formula.pos);
    if (cmp == 0)
        cmp = borISetCmp(&p1->formula.neg, &p2->formula.neg);
    if (cmp == 0)
        cmp = borIArrSize(&p1->plan) - borIArrSize(&p2->plan);
    if (cmp == 0)
        cmp = borIArrCmp(&p1->plan, &p2->plan);
    return cmp;
}

void pddlReversibilityUniformSort(pddl_reversibility_uniform_t *r)
{
    borSort(r->plan, r->plan_size, sizeof(pddl_reverse_plan_t),
            revPlanCmp, NULL);
}

static int foundPlan(const bor_iset_t *pre_a,
                     const bor_iset_t *F0,
                     const bor_iset_t *Fpos,
                     const bor_iset_t *Fneg)
{
    return borISetIsSubset(pre_a, Fpos)
            && borISetIsDisjunct(F0, Fneg);
}

static int isApplicable(const bor_iset_t *Fneg,
                        const pddl_strips_op_t *op,
                        const bor_iset_t *F0pos,
                        const pddl_mutex_pairs_t *mutex)
{
    if (mutex != NULL && pddlMutexPairsIsMutexSetSet(mutex, &op->pre, F0pos))
        return 0;
    return borISetIsDisjunct(Fneg, &op->pre);
}

static void applyOp(bor_iset_t *F0,
                    bor_iset_t *Fpos,
                    bor_iset_t *Fneg,
                    const pddl_strips_op_t *op)
{
    BOR_ISET(pre_Fpos);
    borISetMinus2(&pre_Fpos, &op->pre, Fpos);
    borISetUnion(F0, &pre_Fpos);
    borISetFree(&pre_Fpos);

    borISetMinus(Fpos, &op->del_eff);
    borISetUnion(Fpos, &op->add_eff);
    borISetMinus(Fneg, &op->add_eff);
    borISetUnion(Fneg, &op->del_eff);
}

static pddl_reverse_plan_t *addEmptyPlan(pddl_reversibility_uniform_t *r)
{
    if (r->plan_size == r->plan_alloc){
        if (r->plan_alloc == 0)
            r->plan_alloc = 2;
        r->plan_alloc *= 2;
        r->plan = REALLOC_ARR(r->plan, pddl_reverse_plan_t, r->plan_alloc);
    }
    pddl_reverse_plan_t *plan = r->plan + r->plan_size++;
    bzero(plan, sizeof(*plan));
    return plan;
}

static void addPlan(pddl_reversibility_uniform_t *r,
                    const pddl_strips_ops_t *ops,
                    const pddl_strips_op_t *a,
                    const bor_iset_t *F0,
                    const bor_iset_t *Fpos,
                    const bor_iset_t *Fneg,
                    const bor_iarr_t *plan,
                    const pddl_mutex_pairs_t *mutex)
{
    BOR_ISET(pos);
    borISetUnion2(&pos, Fpos, F0);
    if (mutex != NULL && pddlMutexPairsIsMutexSet(mutex, &pos)){
        borISetFree(&pos);
        return;
    }

    pddl_reverse_plan_t *rplan = addEmptyPlan(r);
    rplan->reversible_op_id = a->id;
    borISetSet(&rplan->formula.pos, &pos);

    int fact;
    BOR_ISET_FOR_EACH(Fneg, fact){
        if (mutex == NULL || !pddlMutexPairsIsMutexFactSet(mutex, fact, &pos))
            borISetAdd(&rplan->formula.neg, fact);
    }

    if (borISetSize(&rplan->formula.neg) == 0
            && borISetIsSubset(&rplan->formula.pos, &a->pre)){
        borISetEmpty(&rplan->formula.pos);
    }
    borIArrAppendArr(&rplan->plan, plan);
        borISetFree(&pos);
}

static void reversibleRec(pddl_reversibility_uniform_t *r,
                          const pddl_strips_ops_t *ops,
                          const pddl_strips_op_t *a,
                          const bor_iset_t *F0,
                          const bor_iset_t *Fpos,
                          const bor_iset_t *Fneg,
                          bor_iarr_t *plan,
                          int depth,
                          const pddl_mutex_pairs_t *mutex)
{
    if (foundPlan(&a->pre, F0, Fpos, Fneg)){
        addPlan(r, ops, a, F0, Fpos, Fneg, plan, mutex);
        return;
    }
    if (depth == 0)
        return;

    BOR_ISET(F0pos);
    borISetUnion2(&F0pos, F0, Fpos);
    if (mutex != NULL && pddlMutexPairsIsMutexSet(mutex, &F0pos)){
        borISetFree(&F0pos);
        return;
    }

    BOR_ISET(F0_next);
    BOR_ISET(Fpos_next);
    BOR_ISET(Fneg_next);
    for (int op_id = 0; op_id < ops->op_size; ++op_id){
        const pddl_strips_op_t *op = ops->op[op_id];
        if (isApplicable(Fneg, op, &F0pos, mutex)){
            borISetSet(&F0_next, F0);
            borISetSet(&Fpos_next, Fpos);
            borISetSet(&Fneg_next, Fneg);

            applyOp(&F0_next, &Fpos_next, &Fneg_next, op);
            borIArrAdd(plan, op_id);
            reversibleRec(r, ops, a, &F0_next, &Fpos_next, &Fneg_next,
                          plan, depth - 1, mutex);
            borIArrRmLast(plan);
        }
    }
    borISetFree(&F0pos);
    borISetFree(&F0_next);
    borISetFree(&Fpos_next);
    borISetFree(&Fneg_next);
}

void pddlReversibilityUniformInfer(pddl_reversibility_uniform_t *r,
                                   const pddl_strips_ops_t *ops,
                                   const pddl_strips_op_t *rev_op,
                                   int max_depth,
                                   const pddl_mutex_pairs_t *mutex)
{
    BOR_ISET(F0);
    BOR_ISET(Fpos);
    BOR_ISET(Fneg);
    BOR_IARR(plan);

    borISetMinus2(&Fpos, &rev_op->pre, &rev_op->del_eff);
    borISetUnion(&Fpos, &rev_op->add_eff);
    borISetUnion(&Fneg, &rev_op->del_eff);

    reversibleRec(r, ops, rev_op, &F0, &Fpos, &Fneg, &plan, max_depth, mutex);

    borIArrFree(&plan);
    borISetFree(&F0);
    borISetFree(&Fpos);
    borISetFree(&Fneg);
}

void pddlReversePlanUniformPrint(const pddl_reverse_plan_t *p,
                                 const pddl_strips_ops_t *ops,
                                 FILE *fout)
{
    const pddl_strips_op_t *op = ops->op[p->reversible_op_id];
    //printf("%d:(%s)", p->reversible_op_id, op->name);
    fprintf(fout, "%d", p->reversible_op_id);
    fprintf(fout, " | \\phi:");
    int fact;
    BOR_ISET_FOR_EACH(&p->formula.pos, fact){
        fprintf(fout, " +%d", fact);
        if (borISetIn(fact, &op->pre))
            fprintf(fout, "*");
    }
    BOR_ISET_FOR_EACH(&p->formula.neg, fact)
        fprintf(fout, " -%d", fact);

    fprintf(fout, " | plan:");
    int op_id;
    BOR_IARR_FOR_EACH(&p->plan, op_id)
        fprintf(fout, " %d", op_id);

    fprintf(fout, " | (%s)", op->name);
    fprintf(fout, "\n");

    /*
    printf("\t%d: pre:", op->id);
    BOR_ISET_FOR_EACH(&op->pre, fact)
        printf(" %d", fact);
    printf(", del:");
    BOR_ISET_FOR_EACH(&op->del_eff, fact)
        printf(" %d", fact);
    printf(", add:");
    BOR_ISET_FOR_EACH(&op->add_eff, fact)
        printf(" %d", fact);
    printf("\n");
    BOR_IARR_FOR_EACH(&p->plan, op_id){
        const pddl_strips_op_t *op = ops->op[op_id];
        printf("\t%d: pre:", op->id);
        BOR_ISET_FOR_EACH(&op->pre, fact)
            printf(" %d", fact);
        printf(", del:");
        BOR_ISET_FOR_EACH(&op->del_eff, fact)
            printf(" %d", fact);
        printf(", add:");
        BOR_ISET_FOR_EACH(&op->add_eff, fact)
            printf(" %d", fact);
        printf("\n");
    }
    */
}

void pddlReversibilityUniformPrint(const pddl_reversibility_uniform_t *r,
                                   const pddl_strips_ops_t *ops,
                                   FILE *fout)
{
    for (int i = 0; i < r->plan_size; ++i)
        pddlReversePlanUniformPrint(r->plan + i, ops, fout);
}
