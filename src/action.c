/***
 * cpddl
 * -------
 * Copyright (c)2016 Daniel Fiser <danfis@danfis.cz>,
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

#include "internal.h"
#include "pddl/config.h"
#include "pddl/pddl.h"
#include "pddl/action.h"


#define PDDL_ACTIONS_ALLOC_INIT 4

#define ERR_PREFIX_MAXSIZE 128

void pddlActionsInit(pddl_actions_t *a)
{
    ZEROIZE(a);
}

void pddlActionsInitCopy(pddl_actions_t *dst, const pddl_actions_t *src)
{
    ZEROIZE(dst);
    dst->action_size = dst->action_alloc = src->action_size;
    dst->action = CALLOC_ARR(pddl_action_t, src->action_size);
    for (int i = 0; i < src->action_size; ++i)
        pddlActionInitCopy(dst->action + i, src->action + i);
}

void pddlActionInit(pddl_action_t *a)
{
    ZEROIZE(a);
    pddlParamsInit(&a->param);
    a->id = -1;
}

void pddlActionFree(pddl_action_t *a)
{
    if (a->name != NULL)
        FREE(a->name);
    pddlParamsFree(&a->param);
    if (a->pre != NULL)
        pddlFmDel(a->pre);
    if (a->eff != NULL)
        pddlFmDel(a->eff);
}

void pddlActionInitCopy(pddl_action_t *dst, const pddl_action_t *src)
{
    pddlActionInit(dst);
    if (src->name != NULL)
        dst->name = STRDUP(src->name);
    pddlParamsInitCopy(&dst->param, &src->param);
    if (src->pre != NULL)
        dst->pre = pddlFmClone(src->pre);
    if (src->eff != NULL)
        dst->eff = pddlFmClone(src->eff);
    dst->id = src->id;
}

struct propagate_eq {
    pddl_action_t *a;
    int eq_pred;

    const pddl_fm_atom_t *eq_atom;
    int param;
    int obj;
};

static int setParamToObj(pddl_fm_t *cond, void *ud)
{
    struct propagate_eq *ctx = ud;

    if (pddlFmIsAtom(cond)){
        pddl_fm_atom_t *atom = pddlFmToAtom(cond);
        if (atom == ctx->eq_atom)
            return 0;

        for (int i = 0; i < atom->arg_size; ++i){
            if (atom->arg[i].param == ctx->param){
                atom->arg[i].param = -1;
                atom->arg[i].obj = ctx->obj;
            }
        }
    }

    return 0;
}

static int _propagateEquality(pddl_fm_t *c, void *ud)
{
    struct propagate_eq *ctx = ud;

    if (pddlFmIsAtom(c)){
        const pddl_fm_atom_t *atom = pddlFmToAtom(c);
        if (atom->pred == ctx->eq_pred && !atom->neg){
            if (atom->arg[0].param >= 0 && atom->arg[1].obj >= 0){
                ctx->eq_atom = atom;
                ctx->param = atom->arg[0].param;
                ctx->obj = atom->arg[1].obj;
                pddlFmTraverse(ctx->a->pre, NULL, setParamToObj, ctx);
                pddlFmTraverse(ctx->a->eff, NULL, setParamToObj, ctx);
            }else if (atom->arg[1].param >= 0 && atom->arg[0].obj >= 0){
                ctx->eq_atom = atom;
                ctx->param = atom->arg[1].param;
                ctx->obj = atom->arg[0].obj;
                pddlFmTraverse(ctx->a->pre, NULL, setParamToObj, ctx);
                pddlFmTraverse(ctx->a->eff, NULL, setParamToObj, ctx);
            }
        }
    }
    return 0;
}

static void propagateEquality(pddl_action_t *a, const pddl_t *pddl)
{
    if (a->pre == NULL || pddl->pred.eq_pred < 0)
        return;

    struct propagate_eq ctx = { a, pddl->pred.eq_pred, NULL, -1, -1 };
    if (!pddlFmIsAnd(a->pre) && !pddlFmIsAtom(a->pre))
        return;
    pddlFmTraverse(a->pre, _propagateEquality, NULL, &ctx);
}

void pddlActionNormalize(pddl_action_t *a, const pddl_t *pddl)
{
    a->pre = pddlFmNormalize(a->pre, pddl, &a->param);
    a->eff = pddlFmNormalize(a->eff, pddl, &a->param);

    if (pddlFmIsBool(a->pre) && pddlFmToBool(a->pre)->val){
        pddlFmDel(a->pre);
        a->pre = pddlFmNewEmptyAnd();
    }
    if (pddlFmIsAtom(a->pre))
        a->pre = pddlFmAtomToAnd(a->pre);
    if (!pddlFmIsAnd(a->eff))
        a->eff = pddlFmAtomToAnd(a->eff);

    propagateEquality(a, pddl);
}

pddl_action_t *pddlActionsAddEmpty(pddl_actions_t *as)
{
    return pddlActionsAddCopy(as, -1);
}

pddl_action_t *pddlActionsAddCopy(pddl_actions_t *as, int copy_id)
{
    pddl_action_t *a;

    if (as->action_size == as->action_alloc){
        if (as->action_alloc == 0)
            as->action_alloc = PDDL_ACTIONS_ALLOC_INIT;
        as->action_alloc *= 2;
        as->action = REALLOC_ARR(as->action, pddl_action_t,
                                     as->action_alloc);
    }

    a = as->action + as->action_size;
    ++as->action_size;
    if (copy_id >= 0){
        pddlActionInitCopy(a, as->action + copy_id);
    }else{
        pddlActionInit(a);
    }
    a->id = as->action_size - 1;
    return a;
}

void pddlActionsFree(pddl_actions_t *actions)
{
    pddl_action_t *a;
    int i;

    for (i = 0; i < actions->action_size; ++i){
        a = actions->action + i;
        pddlActionFree(a);
    }
    if (actions->action != NULL)
        FREE(actions->action);
}

void pddlActionSplit(pddl_action_t *a, pddl_t *pddl)
{
    pddl_actions_t *as = &pddl->action;
    pddl_action_t *newa;
    pddl_fm_junc_t *pre;
    pddl_fm_t *first_cond, *cond;
    pddl_list_t *item;
    int aidx;

    if (!pddlFmIsOr(a->pre))
        return;

    pre = pddlFmToJunc(a->pre);
    if (pddlListEmpty(&pre->part))
        return;

    item = pddlListNext(&pre->part);
    pddlListDel(item);
    first_cond = PDDL_LIST_ENTRY(item, pddl_fm_t, conn);
    a->pre = NULL;
    aidx = a - as->action;
    while (!pddlListEmpty(&pre->part)){
        item = pddlListNext(&pre->part);
        pddlListDel(item);
        cond = PDDL_LIST_ENTRY(item, pddl_fm_t, conn);
        newa = pddlActionsAddCopy(as, aidx);
        newa->pre = cond;
        pddlActionNormalize(newa, pddl);
    }
    as->action[aidx].pre = first_cond;
    pddlActionNormalize(as->action + aidx, pddl);

    pddlFmDel(&pre->fm);
}

void pddlActionAssertPreConjuction(pddl_action_t *a)
{
    pddl_list_t *item;
    pddl_fm_junc_t *pre;
    pddl_fm_t *c;

    if (!pddlFmIsAnd(a->pre)){
        PANIC("Precondition of the action `%s' is" " not a conjuction.", a->name);
    }

    pre = pddlFmToJunc(a->pre);
    PDDL_LIST_FOR_EACH(&pre->part, item){
        c = PDDL_LIST_ENTRY(item, pddl_fm_t, conn);
        if (!pddlFmIsAtom(c)){
            PANIC("Precondition of the action `%s' is"
                       " not a flatten conjuction (conjuction contains"
                       " something else besides atoms).", a->name);
        }
    }
}

void pddlActionRemapObjs(pddl_action_t *a, const int *remap)
{
    pddlFmRemapObjs(a->pre, remap);
    pddlFmRemapObjs(a->eff, remap);
}

void pddlActionsRemapObjs(pddl_actions_t *as, const int *remap)
{
    for (int i = 0; i < as->action_size; ++i)
        pddlActionRemapObjs(as->action + i, remap);
}

int pddlActionRemapTypesAndPreds(pddl_action_t *a,
                                 const int *type_remap,
                                 const int *pred_remap,
                                 const int *func_remap)
{
    for (int i = 0; i < a->param.param_size; ++i){
        if (type_remap[a->param.param[i].type] < 0)
            return -1;
        a->param.param[i].type = type_remap[a->param.param[i].type];
    }
    if (pddlFmRemapPreds(a->pre, pred_remap, func_remap) != 0)
        return -1;
    if (pddlFmRemapPreds(a->eff, pred_remap, func_remap) != 0)
        return -1;

    return 0;
}

void pddlActionsRemapTypesAndPreds(pddl_actions_t *as,
                                   const int *type_remap,
                                   const int *pred_remap,
                                   const int *func_remap)
{
    int ins = 0;
    for (int i = 0; i < as->action_size; ++i){
        if (pddlActionRemapTypesAndPreds(as->action + i, type_remap,
                    pred_remap, func_remap) == 0){
            as->action[ins] = as->action[i];
            as->action[ins].id = ins;
            ++ins;
        }else{
            pddlActionFree(as->action + i);
        }
    }
    as->action_size = ins;
}

void pddlActionsRemoveSet(pddl_actions_t *as, const pddl_iset_t *ids)
{
    int size = pddlISetSize(ids);
    if (size == 0)
        return;

    int cur = 0;
    int ins = 0;
    for (int ai = 0; ai < as->action_size; ++ai){
        if (cur < pddlISetSize(ids) && ai == pddlISetGet(ids, cur)){
            pddlActionFree(as->action + ai);
            ++cur;

        }else{
            if (ai != ins){
                as->action[ins] = as->action[ai];
                as->action[ins].id = ins;
            }
            ++ins;
        }
    }
    as->action_size = ins;
}

static int cmpFms(const pddl_fm_t *fm1,
                  const pddl_fm_t *fm2,
                  void *_pddl)
{
    int cmp = (int)pddlFmIsAtom(fm2) - (int)pddlFmIsAtom(fm1);
    if (cmp == 0 && pddlFmIsAtom(fm1)){
        const pddl_t *pddl = _pddl;
        const pddl_fm_atom_t *a1 = pddlFmToAtomConst(fm1);
        const pddl_fm_atom_t *a2 = pddlFmToAtomConst(fm2);

        int a1static = pddlPredIsStatic(pddl->pred.pred + a1->pred);
        int a2static = pddlPredIsStatic(pddl->pred.pred + a2->pred);
        cmp = a2static - a1static;
        if (cmp == 0)
            cmp = pddlFmAtomCmp(a1, a2);

    }else if (cmp == 0 && pddlFmIsFuncOp(fm1) && pddlFmIsFuncOp(fm2)){
        const pddl_fm_func_op_t *f1 = pddlFmToFuncOpConst(fm1);
        const pddl_fm_func_op_t *f2 = pddlFmToFuncOpConst(fm2);
        ASSERT(f1->lvalue != NULL && f2->lvalue != NULL);
        int cmp = pddlFmAtomCmp(f1->lvalue, f2->lvalue);
        if (cmp == 0){
            if (f1->fvalue == NULL && f2->fvalue == NULL){
                cmp = f1->value - f2->value;
            }else if (f1->fvalue == NULL){
                cmp = 1;
            }else if (f2->fvalue == NULL){
                cmp = -1;
            }else{
                cmp = pddlFmAtomCmp(f1->fvalue, f2->fvalue);
            }
        }
    }

    return cmp;
}

void pddlActionPrint(const pddl_t *pddl, const pddl_action_t *a, FILE *fout)
{
    fprintf(fout, "    %s: ", a->name);
    pddlParamsPrint(&a->param, fout);
    fprintf(fout, "\n");

    fprintf(fout, "        pre: ");
    if (a->pre == NULL){
        fprintf(fout, "()");
    }else{
        pddl_fm_t *fm = pddlFmClone(a->pre);
        if (pddlFmIsJunc(fm))
            pddlFmJuncSort(pddlFmToJunc(fm), cmpFms, (void *)pddl);
        pddlFmPrint(pddl, fm, &a->param, fout);
        pddlFmDel(fm);
    }
    fprintf(fout, "\n");

    fprintf(fout, "        eff: ");
    if (a->eff == NULL){
        fprintf(fout, "()");
    }else{
        pddl_fm_t *fm = pddlFmClone(a->eff);
        if (pddlFmIsJunc(fm))
            pddlFmJuncSort(pddlFmToJunc(fm), cmpFms, (void *)pddl);
        pddlFmPrint(pddl, fm, &a->param, fout);
        pddlFmDel(fm);
    }
    fprintf(fout, "\n");
}

void pddlActionsPrint(const pddl_t *pddl,
                      const pddl_actions_t *actions,
                      FILE *fout)
{
    int i;

    fprintf(fout, "Action[%d]:\n", actions->action_size);
    for (i = 0; i < actions->action_size; ++i){
        ASSERT(actions->action[i].id == i);
        pddlActionPrint(pddl, actions->action + i, fout);
    }
}

static void pddlActionPrintPDDL(const pddl_action_t *a,
                                const pddl_t *pddl,
                                FILE *fout)
{
    fprintf(fout, "(:action %s\n", a->name);
    if (a->param.param_size > 0){
        fprintf(fout, "    :parameters (");
        pddlParamsPrintPDDL(&a->param, &pddl->type, fout);
        fprintf(fout, ")\n");
    }

    if (a->pre != NULL){
        fprintf(fout, "    :precondition ");
        pddlFmPrintPDDL(a->pre, pddl, &a->param, fout);
        fprintf(fout, "\n");
    }

    if (a->eff != NULL){
        fprintf(fout, "    :effect ");
        pddlFmPrintPDDL(a->eff, pddl, &a->param, fout);
        fprintf(fout, "\n");
    }

    fprintf(fout, ")\n");

}

void pddlActionsPrintPDDL(const pddl_actions_t *actions,
                          const pddl_t *pddl,
                          FILE *fout)
{
    for (int i = 0; i < actions->action_size; ++i){
        pddlActionPrintPDDL(&actions->action[i], pddl, fout);
        fprintf(fout, "\n");
    }
}
