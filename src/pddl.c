/***
 * Copyright (c)2023 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "parser.h"
#include "pddl/pddl_struct.h"
#include "pddl/sort.h"

#include <string.h>
#include <unistd.h>

void
pddlLogStatsOneLine(const pddl_t* pddl, const char* prefix, pddl_err_t* err)
{
    int pre_size_min = 0;
    int pre_size_max = 0;
    float pre_size_avg = 0;
    float pre_size_median = 0;
    if (pddl->action.action_size > 0) {
        int* pre_size = ALLOC_ARR(int, pddl->action.action_size);
        for (int ai = 0; ai < pddl->action.action_size; ++ai)
            pre_size[ai] = pddlFmNumAtoms(pddl->action.action[ai].pre);
        pddlSortInt(pre_size, pddl->action.action_size);
        pre_size_min = pre_size[0];
        pre_size_max = pre_size[pddl->action.action_size - 1];
        for (int ai = 0; ai < pddl->action.action_size; ++ai)
            pre_size_avg += pre_size[ai];
        pre_size_avg /= (float)pddl->action.action_size;

        if (pddl->action.action_size % 2 == 1) {
            pre_size_median = pre_size[pddl->action.action_size / 2];
        } else {
            pre_size_median = pre_size[pddl->action.action_size / 2];
            pre_size_median += pre_size[pddl->action.action_size / 2 - 1];
            pre_size_median /= 2.;
        }
        FREE(pre_size);
    }

    LOG(err,
        "%s"
        " types: %d,"
        " objects: %d,"
        " predicates: %d,"
        " functions: %d,"
        " actions: %d,"
        " action-pre-size: [min: %d, max: %d, avg: %.2f, med: %.2f]",
        prefix,
        pddl->type.type_size,
        pddl->obj.obj_size,
        pddl->pred.pred_size,
        pddl->func.pred_size,
        pddl->action.action_size,
        pre_size_min,
        pre_size_max,
        pre_size_avg,
        pre_size_median);
}

void
pddlConfigLog(const pddl_config_t* cfg, pddl_err_t* err)
{
    CTX_NO_TIME(err, "Cfg");
    LOG_CONFIG_BOOL(cfg, force_adl, err);
    LOG_CONFIG_BOOL(cfg, normalize, err);
    LOG_CONFIG_BOOL(cfg, remove_empty_types, err);
    LOG_CONFIG_BOOL(cfg, compile_away_cond_eff, err);
    LOG_CONFIG_BOOL(cfg, enforce_unit_cost, err);
    CTXEND(err);
}

static int
checkConfig(const pddl_config_t* cfg)
{
    return 1;
}

int
pddlInit(
    pddl_t* pddl,
    const char* domain_fn,
    const char* problem_fn,
    const pddl_config_t* cfg,
    pddl_err_t* err)
{
    CTX(err, "PDDL");
    pddlConfigLog(cfg, err);

    ZEROIZE(pddl);
    pddl->cfg = *cfg;

    if (problem_fn == NULL)
        pddl->only_domain = pddl_true;

    LOG(err,
        "Processing %s and %s.",
        domain_fn,
        (problem_fn != NULL ? problem_fn : "null"));

    if (!checkConfig(cfg))
        goto pddl_fail;

    if (pddl->cfg.force_adl)
        pddlRequireFlagsSetADL(&pddl->require);

    if (domain_fn != NULL)
        pddl->domain_file = STRDUP(domain_fn);
    if (problem_fn != NULL)
        pddl->problem_file = STRDUP(problem_fn);
    {
        int sep = strlen(domain_fn) - 1;
        for (; domain_fn[sep] != '/' && sep >= 0; --sep) { }
        if (sep >= 0) {
            pddl->avoid_cond_file = ALLOC_ARR(char, sep + 11);
            for (int i = 0; i <= sep; ++i) {
                pddl->avoid_cond_file[i] = domain_fn[i];
            }
            sprintf(pddl->avoid_cond_file + sep + 1, "avoid.pddl");
        } else {
            pddl->avoid_cond_file = ALLOC_ARR(char, 10);
            sprintf(pddl->avoid_cond_file, "avoid.pddl");
        }
    }

    pddlTypesInit(&pddl->type);
    pddlObjsInit(&pddl->obj);
    pddlPredsInitEq(&pddl->pred);
    pddlPredsInitEmpty(&pddl->func);
    pddlActionsInit(&pddl->action);

    if (pddlParseDomain(pddl, domain_fn, err) != 0)
        goto pddl_fail;

    if (!pddl->only_domain) {
        if (pddlParseProblem(pddl, problem_fn, err) != 0)
            goto pddl_fail;
    }

    printf("DOMAIN: %s\n", domain_fn);
    printf("AVOID: %s\n", pddl->avoid_cond_file);
    if (access(pddl->avoid_cond_file, F_OK) == 0) {
        if (pddlParseAvoidCondition(pddl, pddl->avoid_cond_file, err) != 0)
            goto pddl_fail;
    } else {
        FREE(pddl->avoid_cond_file);
        pddl->avoid_cond_file = NULL;
    }

    pddlObjsPropagateToTypes(&pddl->obj, &pddl->type);
    pddlResetPredInInit(pddl);
    pddlResetPredReadWrite(pddl);

    if (cfg->normalize)
        pddlNormalize(pddl, err);

    if (cfg->remove_empty_types) {
        pddlRemoveEmptyTypes(pddl, err);
        if (cfg->normalize)
            pddlNormalize(pddl, err);
    }

    if (cfg->enforce_unit_cost) {
        LOG(err, "Enforcing unit-cost...");
        pddlEnforceUnitCost(pddl, err);
        LOG(err, "Enforcing unit-cost DONE.");
    }

    if (cfg->compile_away_cond_eff) {
        LOG(err, "Compiling away conditional effects...");
        pddlCompileAwayCondEff(pddl);
        LOG(err, "Conditional effects compiled away.");
    }

    LOG(err, "Number of PDDL Types: %d", pddl->type.type_size);
    LOG(err, "Number of PDDL Objects: %d", pddl->obj.obj_size);
    LOG(err, "Number of PDDL Predicates: %d", pddl->pred.pred_size);
    LOG(err, "Number of PDDL Functions: %d", pddl->func.pred_size);
    LOG(err, "Number of PDDL Actions: %d", pddl->action.action_size);
    LOG(err, "PDDL Metric: %s", F_BOOL(pddl->metric));

    CTXEND(err);
    return 0;

pddl_fail:
    CTXEND(err);
    if (pddl != NULL)
        pddlFree(pddl);
    TRACE_RET(err, -1);
}

void
pddlInitCopy(pddl_t* dst, const pddl_t* src)
{
    ZEROIZE(dst);
    dst->cfg = src->cfg;
    if (src->domain_file != NULL)
        dst->domain_file = STRDUP(src->domain_file);
    if (src->problem_file != NULL)
        dst->problem_file = STRDUP(src->problem_file);
    if (src->avoid_cond_file != NULL)
        dst->avoid_cond_file = STRDUP(src->avoid_cond_file);
    if (src->domain_name != NULL)
        dst->domain_name = STRDUP(src->domain_name);
    if (src->problem_name != NULL)
        dst->problem_name = STRDUP(src->problem_name);
    dst->require = src->require;
    pddlTypesInitCopy(&dst->type, &src->type);
    pddlObjsInitCopy(&dst->obj, &src->obj);
    pddlPredsInitCopy(&dst->pred, &src->pred);
    pddlPredsInitCopy(&dst->func, &src->func);
    if (src->init != NULL)
        dst->init = pddlFmToAnd(pddlFmClone(&src->init->fm));
    if (src->goal != NULL)
        dst->goal = pddlFmClone(src->goal);
    if (src->avoid != NULL)
        dst->avoid = pddlFmClone(src->avoid);
    pddlActionsInitCopy(&dst->action, &src->action);
    dst->metric = src->metric;
    dst->normalized = src->normalized;
}

pddl_t*
pddlNew(
    const char* domain_fn,
    const char* problem_fn,
    const pddl_config_t* cfg,
    pddl_err_t* err)
{
    pddl_t* pddl = ALLOC(pddl_t);

    if (pddlInit(pddl, domain_fn, problem_fn, cfg, err) != 0) {
        FREE(pddl);
        return NULL;
    }

    return pddl;
}

void
pddlDel(pddl_t* pddl)
{
    pddlFree(pddl);
    FREE(pddl);
}

pddl_bool_t
pddlHasCondEff(const pddl_t* pddl)
{
    for (int ai = 0; ai < pddl->action.action_size; ++ai) {
        pddl_fm_const_it_when_t it;
        const pddl_fm_when_t* w;
        PDDL_FM_FOR_EACH_WHEN(pddl->action.action[ai].eff, &it, w)
        {
            return pddl_true;
        }
    }

    return pddl_false;
}

void
pddlFree(pddl_t* pddl)
{
    if (pddl->domain_file != NULL)
        FREE(pddl->domain_file);
    if (pddl->problem_file != NULL)
        FREE(pddl->problem_file);
    if (pddl->domain_name != NULL)
        FREE(pddl->domain_name);
    if (pddl->avoid_cond_file != NULL)
        FREE(pddl->avoid_cond_file);
    if (pddl->problem_name != NULL)
        FREE(pddl->problem_name);
    pddlTypesFree(&pddl->type);
    pddlObjsFree(&pddl->obj);
    pddlPredsFree(&pddl->pred);
    pddlPredsFree(&pddl->func);
    if (pddl->init)
        pddlFmDel(&pddl->init->fm);
    if (pddl->goal)
        pddlFmDel(pddl->goal);
    if (pddl->avoid)
        pddlFmDel(pddl->avoid);
    pddlActionsFree(&pddl->action);
}

static int
isFalsePre(const pddl_fm_t* c)
{
    if (pddlFmIsBool(c)) {
        const pddl_fm_bool_t* b = pddlFmToBoolConst(c);
        return !b->val;
    }
    return 0;
}

static pddl_fm_t*
simplifyPre(pddl_fm_t* pre, const pddl_t* pddl, const pddl_params_t* param)
{
    pddl_fm_t* c = pddlFmSimplify(pre, pddl, param);
    if (pddlFmIsTrue(c)) {
        pddlFmDel(c);
        c = pddlFmNewEmptyAnd();
    } else if (pddlFmIsAtom(c)) {
        pddl_fm_t* a = pddlFmNewEmptyAnd();
        pddlFmJuncAdd(pddlFmToJunc(a), c);
        c = a;
    }
    return c;
}

static void
removeIrrelevantActions(pddl_t* pddl)
{
    PDDL_ISET(rm);
    for (int ai = 0; ai < pddl->action.action_size; ++ai) {
        pddl_action_t* a = pddl->action.action + ai;
        a->pre = simplifyPre(a->pre, pddl, &a->param);
        a->eff = pddlFmDeconflictEff(a->eff, pddl, &a->param);

        if (isFalsePre(a->pre) || !pddlFmHasAtom(a->eff))
            pddlISetAdd(&rm, ai);
    }

    pddlActionsRemoveSet(&pddl->action, &rm);
    pddlISetFree(&rm);
}

static int
removeActionsWithUnsatisfiableArgs(pddl_t* pddl)
{
    PDDL_ISET(rm);
    int ret = 0;
    for (int ai = 0; ai < pddl->action.action_size; ++ai) {
        pddl_action_t* a = pddl->action.action + ai;
        int remove = 0;
        for (int pi = 0; pi < a->param.param_size; ++pi) {
            if (pddlTypeNumObjs(&pddl->type, a->param.param[pi].type) == 0) {
                remove = 1;
                break;
            }
        }

        if (remove) {
            pddlISetAdd(&rm, ai);
            ret = 1;
        }
    }

    pddlActionsRemoveSet(&pddl->action, &rm);
    pddlISetFree(&rm);

    return ret;
}

static int
isStaticPreUnreachable(const pddl_t* pddl, const pddl_fm_t* c)
{
    pddl_fm_const_it_atom_t it;
    const pddl_fm_atom_t* atom;
    PDDL_FM_FOR_EACH_ATOM(c, &it, atom)
    {
        const pddl_pred_t* pred = pddl->pred.pred + atom->pred;
        if (!atom->neg && pred->id != pddl->pred.eq_pred
            && pddlPredIsStatic(pred) && !pred->in_init) {
            return 1;
        }
    }
    return 0;
}

static int
isInequalityUnsatisfiable(const pddl_t* pddl, const pddl_action_t* action)
{
    pddl_fm_const_it_atom_t it;
    const pddl_fm_atom_t* atom;
    PDDL_FM_FOR_EACH_ATOM(action->pre, &it, atom)
    {
        if (atom->neg && atom->pred == pddl->pred.eq_pred) {
            int param1 = atom->arg[0].param;
            int obj1 = atom->arg[0].obj;
            int param2 = atom->arg[1].param;
            int obj2 = atom->arg[1].obj;
            if (param1 >= 0) {
                int type = action->param.param[param1].type;
                if (pddlTypeNumObjs(&pddl->type, type) == 1)
                    obj1 = pddlTypeGetObj(&pddl->type, type, 0);
            }
            if (param2 >= 0) {
                int type = action->param.param[param2].type;
                if (pddlTypeNumObjs(&pddl->type, type) == 1)
                    obj2 = pddlTypeGetObj(&pddl->type, type, 0);
            }

            if (obj1 >= 0 && obj2 >= 0 && obj1 == obj2)
                return 1;
        }
    }
    return 0;
}

static int
removeUnreachableActions(pddl_t* pddl)
{
    PDDL_ISET(rm);
    int ret = 0;
    for (int ai = 0; ai < pddl->action.action_size; ++ai) {
        pddl_action_t* a = pddl->action.action + ai;
        a->pre = simplifyPre(a->pre, pddl, &a->param);
        a->eff = pddlFmDeconflictEff(a->eff, pddl, &a->param);

        if (isStaticPreUnreachable(pddl, a->pre)
            || isInequalityUnsatisfiable(pddl, a)) {
            pddlISetAdd(&rm, ai);
            ret = 1;
        }
    }

    pddlActionsRemoveSet(&pddl->action, &rm);
    pddlISetFree(&rm);

    return ret;
}

void
pddlNormalize(pddl_t* pddl, pddl_err_t* err)
{
    CTX(err, "PDDL-Norm");
    pddlLogStatsOneLine(pddl, "Before normalization:", err);

    if (pddl->init != NULL) {
        pddl_fm_t* c = pddlFmDeduplicateAtoms(&pddl->init->fm, pddl);
        PANIC_IF(
            !pddlFmIsAnd(c),
            "Deduplication of the init didn't result in a conjunction");
        pddl->init = pddlFmToAnd(c);
    }

    if (!pddl->only_domain && !pddl->cfg.keep_all_actions)
        removeActionsWithUnsatisfiableArgs(pddl);

    for (int i = 0; i < pddl->action.action_size; ++i)
        pddlActionNormalize(pddl->action.action + i, pddl);

    for (int i = 0; i < pddl->action.action_size; ++i)
        pddlActionSplit(pddl->action.action + i, pddl);

    if (!pddl->only_domain && !pddl->cfg.keep_all_actions)
        removeIrrelevantActions(pddl);

#ifdef PDDL_DEBUG
    for (int i = 0; i < pddl->action.action_size; ++i) {
        pddlActionAssertPreConjuction(pddl->action.action + i);
    }
#endif

    if (pddl->goal != NULL)
        pddl->goal = pddlFmNormalize(pddl->goal, pddl, NULL);

    if (pddl->avoid != NULL)
        pddl->avoid = pddlFmNormalize(pddl->avoid, pddl, NULL);

    if (pddl->cfg.normalize_compile_away_dynamic_neg_cond
        || pddl->cfg.normalize_compile_away_all_neg_cond) {
        pddl_bool_t only_dynamic =
            !pddl->cfg.normalize_compile_away_all_neg_cond;
        pddl_bool_t only_goal =
            pddl->cfg.normalize_compile_away_only_goal_neg_cond;
        pddl_bool_t only_relevant =
            pddl->cfg.normalize_compile_away_neg_cond_only_relevant_facts;
        pddlCompileAwayNegativeConditions(
            pddl, only_dynamic, only_goal, only_relevant, err);
    }

    if (!pddl->only_domain && !pddl->cfg.keep_all_actions) {
        removeIrrelevantActions(pddl);
        do {
            pddlResetPredReadWrite(pddl);
        } while (removeUnreachableActions(pddl));
    }
    pddl->normalized = pddl_true;

    pddlLogStatsOneLine(pddl, "PDDL task normalized.", err);
    CTXEND(err);
}

static void
compileAwayCondEff(pddl_t* pddl, int only_non_static)
{
    pddl_action_t *a, *new_a;
    pddl_fm_when_t* w;
    pddl_fm_t* neg_pre;
    int asize;
    int change;

    do {
        change = 0;
        pddlNormalize(pddl, NULL);

        asize = pddl->action.action_size;
        for (int ai = 0; ai < asize; ++ai) {
            a = pddl->action.action + ai;
            if (only_non_static) {
                w = pddlFmRemoveFirstNonStaticWhen(a->eff, pddl);
            } else {
                w = pddlFmRemoveFirstWhen(a->eff, pddl);
            }
            if (w != NULL) {
                // Create a new action
                new_a = pddlActionsAddCopy(&pddl->action, ai);

                // Get the original action again, because pddlActionsAdd()
                // could realloc the array.
                a = pddl->action.action + ai;

                // The original takes additional precondition which is the
                // negation of w->pre
                if ((neg_pre = pddlFmNegate(w->pre, pddl)) == NULL) {
                    // This shoud never fail, because we force
                    // normalization before this.
                    PANIC(
                        "Fatal Error: Encountered problem in"
                        " the normalization.");
                }
                a->pre = pddlFmNewAnd2(a->pre, neg_pre);

                // The new action extends both pre and eff by w->pre and
                // w->eff.
                new_a->pre = pddlFmNewAnd2(new_a->pre, pddlFmClone(w->pre));
                new_a->eff = pddlFmNewAnd2(new_a->eff, pddlFmClone(w->eff));

                pddlFmDel(&w->fm);
                change = 1;
            }
        }
    } while (change);
    pddlResetPredReadWrite(pddl);
}

void
pddlCompileAwayCondEff(pddl_t* pddl)
{
    compileAwayCondEff(pddl, 0);
}

void
pddlCompileAwayNonStaticCondEff(pddl_t* pddl)
{
    compileAwayCondEff(pddl, 1);
}

struct compile_away_eq_pred {
    const pddl_t* pddl;
    int eq_pred;
    pddl_iset_t relevant_objs;
    const pddl_params_t* params;
};

static int
compileAwayEqPred(pddl_fm_t* fm, void* _data)
{
    struct compile_away_eq_pred* data = _data;
    if (pddlFmIsQuant(fm)) {
        pddl_fm_quant_t* q = pddlFmToQuant(fm);
        const pddl_params_t* old_params = data->params;
        data->params = &q->param;
        pddlFmTraverse(q->qfm, compileAwayEqPred, NULL, data);
        data->params = old_params;
        return -1;

    } else if (pddlFmIsAtom(fm)) {
        pddl_fm_atom_t* atom = pddlFmToAtom(fm);
        if (atom->pred == data->pddl->pred.eq_pred) {
            atom->pred = data->eq_pred;
            ASSERT(atom->arg_size == 2);
            for (int argi = 0; argi < atom->arg_size; ++argi) {
                if (atom->arg[argi].param >= 0) {
                    ASSERT(data->params != NULL);
                    int type = data->params->param[atom->arg[argi].param].type;
                    int size = 0;
                    const int* objs =
                        pddlTypesObjsByType(&data->pddl->type, type, &size);
                    for (int i = 0; i < size; ++i)
                        pddlISetAdd(&data->relevant_objs, objs[i]);

                } else {
                    pddlISetAdd(&data->relevant_objs, atom->arg[argi].obj);
                }
            }
        }
    }

    return 0;
}

int
pddlCompileAwayEqPred(pddl_t* pddl)
{
    if (pddl->pred.eq_pred < 0)
        return 0;

    if (!pddlHasEqPred(pddl))
        return 0;

    // Create a new equality predicate
    char name[256];
    strcpy(name, "equal");
    while (pddlPredsGet(&pddl->pred, name) >= 0) {
        int len = strlen(name);
        name[len] = 'x';
        name[len + 1] = '\x0';
    }
    pddl_pred_t* eq_pred = pddlPredsAddCopy(&pddl->pred, pddl->pred.eq_pred);
    if (eq_pred->name != NULL)
        FREE(eq_pred->name);
    eq_pred->name = STRDUP(name);

    // Replace '=' with the new equality predicate and collect relevant
    // objects
    struct compile_away_eq_pred data;
    data.pddl = pddl;
    data.eq_pred = eq_pred->id;
    pddlISetInit(&data.relevant_objs);
    for (int ai = 0; ai < pddl->action.action_size; ++ai) {
        const pddl_action_t* a = pddl->action.action + ai;
        data.params = &a->param;
        if (a->pre != NULL)
            pddlFmTraverse(a->pre, compileAwayEqPred, NULL, &data);
        if (a->eff != NULL)
            pddlFmTraverse(a->eff, compileAwayEqPred, NULL, &data);
    }

    // Set up initial state
    if (pddl->init == NULL && pddlISetSize(&data.relevant_objs) > 0)
        pddl->init = pddlFmToAnd(pddlFmNewEmptyAnd());

    int obj_id;
    PDDL_ISET_FOR_EACH(&data.relevant_objs, obj_id)
    {
        int arg[2] = { obj_id, obj_id };
        pddl_fm_atom_t* a = pddlFmCreateFactAtom(eq_pred->id, 2, arg);
        pddlFmJuncAdd(pddl->init, &a->fm);
    }
    pddlISetFree(&data.relevant_objs);

    pddlResetPredReadWrite(pddl);

    return 1;
}

struct has_eq_pred {
    const pddl_t* pddl;
    pddl_bool_t found;
};

static int
hasEqPred(pddl_fm_t* fm, void* _data)
{
    struct has_eq_pred* data = _data;
    if (pddlFmIsAtom(fm)) {
        const pddl_fm_atom_t* atom = pddlFmToAtom(fm);
        if (atom->pred == data->pddl->pred.eq_pred) {
            data->found = pddl_true;
            return -2;
        }
    }
    return 0;
}

pddl_bool_t
pddlHasEqPred(const pddl_t* pddl)
{
    if (pddl->pred.eq_pred < 0)
        return pddl_false;

    struct has_eq_pred data = { .pddl = pddl, .found = pddl_false };
    for (int ai = 0; ai < pddl->action.action_size; ++ai) {
        const pddl_action_t* a = pddl->action.action + ai;
        if (a->pre != NULL)
            pddlFmTraverse(a->pre, hasEqPred, NULL, &data);
        if (data.found)
            return pddl_true;

        if (a->eff != NULL)
            pddlFmTraverse(a->eff, hasEqPred, NULL, &data);
        if (data.found)
            return pddl_true;
    }

    return pddl_false;
}

int
pddlPredFuncMaxParamSize(const pddl_t* pddl)
{
    int max = 0;

    for (int i = 0; i < pddl->pred.pred_size; ++i)
        max = PDDL_MAX(max, pddl->pred.pred[i].param_size);
    for (int i = 0; i < pddl->func.pred_size; ++i)
        max = PDDL_MAX(max, pddl->func.pred[i].param_size);

    return max;
}

void
pddlAddObjectTypes(pddl_t* pddl)
{
    for (int obj_id = 0; obj_id < pddl->obj.obj_size; ++obj_id) {
        pddl_obj_t* obj = pddl->obj.obj + obj_id;
        ASSERT(obj->type >= 0);
        if (pddlTypeNumObjs(&pddl->type, obj->type) <= 1)
            continue;

        char* name = ALLOC_ARR(char, strlen(obj->name) + 8 + 1);
        sprintf(name, "%s-OBJTYPE", obj->name);
        int type_id = pddlTypesAdd(&pddl->type, name, obj->type);
        ASSERT(type_id == pddl->type.type_size - 1);
        pddlTypesAddObj(&pddl->type, obj_id, type_id);
        obj->type = type_id;
        FREE(name);
    }
    pddlTypesBuildObjTypeMap(&pddl->type, pddl->obj.obj_size);
}

void
pddlRemoveObjs(pddl_t* pddl, const pddl_iset_t* rm_obj, pddl_err_t* err)
{
    if (pddlISetSize(rm_obj) == 0)
        return;
    int* remap = ALLOC_ARR(int, pddl->obj.obj_size);
    pddlRemoveObjsGetRemap(pddl, rm_obj, remap, err);
    FREE(remap);
}

void
pddlRemoveObjsGetRemap(
    pddl_t* pddl,
    const pddl_iset_t* rm_obj,
    int* remap,
    pddl_err_t* err)
{
    if (pddlISetSize(rm_obj) == 0)
        return;
    CTX(err, "PDDL rm objs");
    LOG(err, "Removing %d objects", pddlISetSize(rm_obj));

    for (int i = 0, idx = 0, id = 0; i < pddl->obj.obj_size; ++i) {
        if (idx < pddlISetSize(rm_obj) && pddlISetGet(rm_obj, idx) == i) {
            remap[i] = -1;
            ++idx;
        } else {
            remap[i] = id++;
        }
    }

    pddlRemapObjs(pddl, remap);
    CTXEND(err);
}

void
pddlRemapObjs(pddl_t* pddl, const int* remap)
{
    pddlFmRemapObjs(&pddl->init->fm, remap);
    pddl_fm_t* c = pddlFmRemoveInvalidAtoms(&pddl->init->fm);
    PANIC_IF(
        !pddlFmIsAnd(c),
        "Removing invalid atoms from the init didn't"
        " result in a conjunction");
    pddl->init = pddlFmToAnd(c);

    pddlFmRemapObjs(pddl->goal, remap);
    pddl->goal = pddlFmRemoveInvalidAtoms(pddl->goal);
    if (pddl->goal == NULL)
        pddl->goal = &pddlFmNewBool(1)->fm;

    pddlActionsRemapObjs(&pddl->action, remap);
    pddlTypesRemapObjs(&pddl->type, remap);
    pddlObjsRemap(&pddl->obj, remap);
}

void
pddlRemoveEmptyTypes(pddl_t* pddl, pddl_err_t* err)
{
    CTX(err, "PDDL rm empty-types");
    int* type_remap = CALLOC_ARR(int, pddl->type.type_size);
    int* pred_remap = CALLOC_ARR(int, pddl->pred.pred_size);
    int* func_remap = CALLOC_ARR(int, pddl->func.pred_size);
    int type_size = pddl->type.type_size;
    int pred_size = pddl->pred.pred_size;
    int func_size = pddl->func.pred_size;
    int action_size = pddl->action.action_size;

    pddlTypesRemoveEmpty(&pddl->type, pddl->obj.obj_size, type_remap);
    LOG(err, "Removed %d empty types", type_size - pddl->type.type_size);
    if (type_size != pddl->type.type_size) {
        pddlObjsRemapTypes(&pddl->obj, type_remap);
        pddlPredsRemapTypes(&pddl->pred, type_remap, pred_remap);
        LOG(err, "Removed %d predicates", pred_size - pddl->pred.pred_size);
        pddlPredsRemapTypes(&pddl->func, type_remap, func_remap);
        LOG(err, "Removed %d functions", func_size - pddl->func.pred_size);
        pddlActionsRemapTypesAndPreds(
            &pddl->action, type_remap, pred_remap, func_remap);
        LOG(err, "Removed %d actions", action_size - pddl->action.action_size);

        if (pred_size != pddl->pred.pred_size
            || func_size != pddl->func.pred_size) {

            if (pddlFmRemapPreds(&pddl->init->fm, pred_remap, func_remap)
                != 0) {
                LOG(err,
                    "The task is unsolvable, because the initial"
                    " state is false");
                pddlFmDel(&pddl->init->fm);
                pddl_fm_t* c = pddlFmNewEmptyAnd();
                pddl->init = pddlFmToAnd(c);
                pddl_fm_bool_t* b = pddlFmNewBool(0);
                pddlFmJuncAdd(pddl->init, &b->fm);
            }

            if (pddlFmRemapPreds(pddl->goal, pred_remap, func_remap) != 0) {
                LOG(err,
                    "The task is unsolvable, because the goal"
                    " is false");
                pddlFmDel(pddl->goal);
                pddl_fm_bool_t* b = pddlFmNewBool(0);
                pddl->goal = &b->fm;
            }
        }
    }

    FREE(type_remap);
    FREE(pred_remap);
    FREE(func_remap);
    CTXEND(err);
}

static int
_removeAssignIncrease(pddl_fm_t** c, void* _)
{
    if (pddlFmIsAssign(*c) || pddlFmIsIncrease(*c)) {
        pddlFmDel(*c);
        *c = NULL;
    }
    return 0;
}

void
pddlEnforceUnitCost(pddl_t* pddl, pddl_err_t* err)
{
    CTX(err, "PDDL enforce unit-cost");
    // Remove (= ...) from the initial state
    if (pddl->init != NULL) {
        pddl_fm_t* init = &pddl->init->fm;
        pddlFmRebuild(&init, NULL, _removeAssignIncrease, NULL);
        PANIC_IF(
            !pddlFmIsAnd(init),
            "Enforcing unit cost in the initial state"
            " didn't result in a conjunction");
        pddl->init = pddlFmToAnd(init);

        for (int ai = 0; ai < pddl->action.action_size; ++ai) {
            pddl_action_t* a = pddl->action.action + ai;
            pddlFmRebuild(&a->eff, NULL, _removeAssignIncrease, NULL);
        }
    }

    pddl->metric = pddl_false;
    CTXEND(err);
}

void
pddlResetPredReadWrite(pddl_t* pddl)
{
    for (int i = 0; i < pddl->pred.pred_size; ++i)
        pddl->pred.pred[i].read = pddl->pred.pred[i].write = pddl_false;
    for (int i = 0; i < pddl->action.action_size; ++i) {
        const pddl_action_t* a = pddl->action.action + i;
        pddlFmSetPredRead(a->pre, &pddl->pred);
        pddlFmSetPredReadWriteEff(a->eff, &pddl->pred);
    }
}

void
pddlResetPredInInit(pddl_t* pddl)
{
    for (int i = 0; i < pddl->pred.pred_size; ++i)
        pddl->pred.pred[i].read = pddl->pred.pred[i].write = pddl_false;

    if (pddl->init == NULL)
        return;

    pddl_fm_const_it_atom_t it;
    const pddl_fm_atom_t* atom;
    PDDL_FM_FOR_EACH_ATOM(&pddl->init->fm, &it, atom)
    pddl->pred.pred[atom->pred].in_init = pddl_true;
}

void
pddlEnforceUniquelyNamedActions(pddl_t* pddl)
{
    pddlActionsEnforceUniqueNames(&pddl->action);
}

void
pddlPrintPDDLDomain(const pddl_t* pddl, FILE* fout)
{
    fprintf(fout, "(define (domain %s)\n", pddl->domain_name);
    pddlRequireFlagsPrintPDDL(&pddl->require, fout);
    pddlTypesPrintPDDL(&pddl->type, fout);
    // TODO: Print as constants only objects actually used in the domain file
    pddlObjsPrintPDDLConstants(&pddl->obj, &pddl->type, fout);
    pddlPredsPrintPDDL(&pddl->pred, &pddl->type, fout);
    pddlFuncsPrintPDDL(&pddl->func, &pddl->type, fout);
    pddlActionsPrintPDDL(&pddl->action, pddl, fout);
    fprintf(fout, ")\n");
}

void
pddlPrintPDDLProblem(const pddl_t* pddl, FILE* fout)
{
    pddl_list_t* item;
    pddl_fm_t* c;
    pddl_params_t params;

    fprintf(
        fout,
        "(define (problem %s) (:domain %s)\n",
        pddl->problem_name,
        pddl->domain_name);

    // TODO: Print objects

    pddlParamsInit(&params);
    fprintf(fout, "(:init\n");
    if (pddl->init != NULL) {
        PDDL_LIST_FOR_EACH(&pddl->init->part, item)
        {
            c = PDDL_LIST_ENTRY(item, pddl_fm_t, conn);
            fprintf(fout, "  ");
            pddlFmPrintPDDL(c, pddl, &params, fout);
            fprintf(fout, "\n");
        }
    }
    fprintf(fout, ")\n");
    pddlParamsFree(&params);

    fprintf(fout, "(:goal ");
    if (pddl->goal != NULL)
        pddlFmPrintPDDL(pddl->goal, pddl, NULL, fout);
    fprintf(fout, ")\n");

    if (pddl->metric)
        fprintf(fout, "(:metric minimize (total-cost))\n");

    fprintf(fout, ")\n");
}

static int
initCondSize(const pddl_t* pddl, int type)
{
    pddl_list_t* item;
    const pddl_fm_t* c;
    int size = 0;

    PDDL_LIST_FOR_EACH(&pddl->init->part, item)
    {
        c = PDDL_LIST_ENTRY(item, pddl_fm_t, conn);
        if (c->type == type)
            ++size;
    }
    return size;
}

static int
cmpGroundAtoms(const pddl_fm_t* fm1, const pddl_fm_t* fm2, void* _pddl)
{
    int cmp = (int)pddlFmIsAtom(fm2) - (int)pddlFmIsAtom(fm1);
    if (cmp == 0 && pddlFmIsAtom(fm1)) {
        const pddl_t* pddl = _pddl;
        const pddl_fm_atom_t* a1 = pddlFmToAtomConst(fm1);
        const pddl_fm_atom_t* a2 = pddlFmToAtomConst(fm2);

        int a1static = pddlPredIsStatic(pddl->pred.pred + a1->pred);
        int a2static = pddlPredIsStatic(pddl->pred.pred + a2->pred);
        cmp = a2static - a1static;
        if (cmp == 0)
            cmp = pddlFmAtomCmp(a1, a2);

    } else if (cmp == 0 && pddlFmIsFuncOp(fm1) && pddlFmIsFuncOp(fm2)) {
        const pddl_fm_func_op_t* f1 = pddlFmToFuncOpConst(fm1);
        const pddl_fm_func_op_t* f2 = pddlFmToFuncOpConst(fm2);
        ASSERT(f1->lvalue != NULL && f2->lvalue != NULL);
        int cmp = pddlFmAtomCmp(f1->lvalue, f2->lvalue);
        if (cmp == 0) {
            if (f1->fvalue == NULL && f2->fvalue == NULL) {
                cmp = f1->value - f2->value;
            } else if (f1->fvalue == NULL) {
                cmp = 1;
            } else if (f2->fvalue == NULL) {
                cmp = -1;
            } else {
                cmp = pddlFmAtomCmp(f1->fvalue, f2->fvalue);
            }
        }
    }

    return cmp;
}

void
pddlPrintDebug(const pddl_t* pddl, FILE* fout)
{

    fprintf(fout, "Domain: %s\n", pddl->domain_name);
    fprintf(fout, "Problem: %s\n", pddl->problem_name);
    fprintf(fout, "Require: %x\n", pddlRequireFlagsToMask(&pddl->require));
    pddlTypesPrint(&pddl->type, fout);
    pddlObjsPrint(&pddl->obj, fout);
    pddlPredsPrint(&pddl->pred, "Predicate", fout);
    pddlPredsPrint(&pddl->func, "Function", fout);
    pddlActionsPrint(pddl, &pddl->action, fout);

    pddl_fm_t* init = pddlFmClone(&pddl->init->fm);
    ASSERT(pddlFmIsJunc(init));
    pddlFmJuncSort(pddlFmToJunc(init), cmpGroundAtoms, (void*)pddl);

    pddl_params_t params;
    pddlParamsInit(&params);

    pddl_fm_const_it_atom_t it;
    const pddl_fm_atom_t* atom;
    fprintf(fout, "Init[%d]:\n", initCondSize(pddl, PDDL_FM_ATOM));
    PDDL_FM_FOR_EACH_ATOM(init, &it, atom)
    {
        fprintf(fout, "  ");
        if (pddlPredIsStatic(&pddl->pred.pred[atom->pred]))
            fprintf(fout, "S:");
        pddlFmPrintPDDL(&atom->fm, pddl, &params, fout);
        fprintf(fout, "\n");
    }

    fprintf(fout, "Init[%d]:\n", initCondSize(pddl, PDDL_FM_ASSIGN));
    pddl_fm_const_it_t fmit;
    const pddl_fm_t* ifm;
    PDDL_FM_FOR_EACH(init, &fmit, ifm)
    {
        if (pddlFmIsAssign(ifm)) {
            fprintf(fout, "  ");
            pddlFmPrintPDDL(ifm, pddl, &params, fout);
            fprintf(fout, "\n");
        }
    }
    pddlParamsFree(&params);
    pddlFmDel(init);

    fprintf(fout, "Goal: ");
    if (pddl->goal == NULL) {
        fprintf(fout, "()");
    } else {
        pddl_fm_t* g = pddlFmClone(pddl->goal);
        if (pddlFmIsJunc(g))
            pddlFmJuncSort(pddlFmToJunc(g), cmpGroundAtoms, (void*)pddl);
        pddlFmPrint(pddl, g, NULL, fout);
        pddlFmDel(g);
    }
    fprintf(fout, "\n");

    fprintf(fout, "Metric: %d\n", pddl->metric);
}
