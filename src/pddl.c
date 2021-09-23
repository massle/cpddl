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


#include <boruvka/alloc.h>
#include "pddl/pddl_struct.h"
#include "err.h"
#include "assert.h"

static int checkDerivedPredicates(const pddl_t *pddl, bor_err_t *err)
{
    const pddl_lisp_node_t *root = &pddl->domain_lisp->root;
    for (int i = 0; i < root->child_size; ++i){
        const pddl_lisp_node_t *n = root->child + i;
        if (pddlLispNodeHeadKw(n) == PDDL_KW_DERIVED){
            BOR_ERR_RET(err, -1, "Derived predicates are not supported"
                                 " (line %d).", n->lineno);
        }
    }
    return 0;
}

static int checkConfig(const pddl_config_t *cfg)
{
    return 1;
}

static const char *parseName(pddl_lisp_t *lisp, int kw,
                             const char *err_name, bor_err_t *err)
{
    const pddl_lisp_node_t *n;

    n = pddlLispFindNode(&lisp->root, kw);
    if (n == NULL){
        // TODO: Configure warn/err
        BOR_ERR_RET(err, NULL, "Could not find %s name definition in %s.",
                    err_name, lisp->filename);
    }

    if (n->child_size != 2 || n->child[1].value == NULL){
        BOR_ERR_RET(err, NULL, "Invalid %s name definition in %s.",
                    err_name, lisp->filename);
    }

    return n->child[1].value;
}

static char *parseDomainName(pddl_lisp_t *lisp, bor_err_t *err)
{
    const char *name = parseName(lisp, PDDL_KW_DOMAIN, "domain", err);
    if (name != NULL)
        return BOR_STRDUP(name);
    return NULL;
}

static char *parseProblemName(pddl_lisp_t *lisp, bor_err_t *err)
{
    const char *name = parseName(lisp, PDDL_KW_PROBLEM, "problem", err);
    if (name != NULL)
        return BOR_STRDUP(name);
    return NULL;
}

static int checkDomainName(pddl_t *pddl, bor_err_t *err)
{
    const char *problem_domain_name;

    // TODO: Configure err/warn/nothing
    problem_domain_name = parseName(pddl->problem_lisp,
                                    PDDL_KW_DOMAIN2, ":domain", err);
    if (problem_domain_name == NULL)
        BOR_TRACE_RET(err, 0);

    if (strcmp(problem_domain_name, pddl->domain_name) != 0){
        BOR_WARN(err, "Domain names does not match: `%s' x `%s'",
                 pddl->domain_name, problem_domain_name);
        return 0;
    }
    return 0;
}

static int parseMetric(pddl_t *pddl, const pddl_lisp_t *lisp, bor_err_t *err)
{
    const pddl_lisp_node_t *n;

    n = pddlLispFindNode(&lisp->root, PDDL_KW_METRIC);
    if (n == NULL)
        return 0;

    if (n->child_size != 3
            || n->child[1].value == NULL
            || n->child[1].kw != PDDL_KW_MINIMIZE
            || n->child[2].value != NULL
            || n->child[2].child_size != 1
            || strcmp(n->child[2].child[0].value, "total-cost") != 0){
        BOR_ERR_RET(err, -1, "Only (:metric minimize (total-cost)) is supported"
                    " (line %d in %s).", n->lineno, lisp->filename);
    }

    pddl->metric = 1;
    return 0;
}

static int parseInit(pddl_t *pddl, bor_err_t *err)
{
    const pddl_lisp_node_t *ninit;

    ninit = pddlLispFindNode(&pddl->problem_lisp->root, PDDL_KW_INIT);
    if (ninit == NULL){
        BOR_ERR_RET(err, -1, "Missing :init in %s.",
                    pddl->problem_lisp->filename);
    }

    pddl->init = pddlCondParseInit(ninit, pddl, err);
    if (pddl->init == NULL){
        BOR_TRACE_PREPEND_RET(err, -1, "While parsing :init specification"
                              " in %s: ", pddl->problem_lisp->filename);
    }

    pddl_cond_const_it_atom_t it;
    const pddl_cond_atom_t *atom;
    PDDL_COND_FOR_EACH_ATOM(&pddl->init->cls, &it, atom)
        pddl->pred.pred[atom->pred].in_init = 1;

    return 0;
}

static int parseGoal(pddl_t *pddl, bor_err_t *err)
{
    const pddl_lisp_node_t *ngoal;

    ngoal = pddlLispFindNode(&pddl->problem_lisp->root, PDDL_KW_GOAL);
    if (ngoal == NULL)
        BOR_ERR_RET(err, -1, "Missing :goal in %s.", pddl->problem_lisp->filename);

    if (ngoal->child_size != 2 || ngoal->child[1].value != NULL){
        BOR_ERR_RET(err, -1, "Invalid definition of :goal in %s (line %d).",
                    pddl->problem_lisp->filename, ngoal->lineno);
    }

    pddl->goal = pddlCondParse(ngoal->child + 1, pddl, NULL, "", err);
    if (pddl->goal == NULL){
        BOR_TRACE_PREPEND_RET(err, -1, "While parsing :goal specification"
                              " in %s: ", pddl->problem_lisp->filename);
    }
    return 0;
}

int pddlInit(pddl_t *pddl, const char *domain_fn, const char *problem_fn,
             const pddl_config_t *cfg, bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "PDDL: ");
    BOR_INFO(err, "Config force-adl: %d", cfg->force_adl);
    BOR_INFO(err, "Config normalize: %d", cfg->normalize);
    BOR_INFO(err, "Config compile-away-cond-eff: %d",
             cfg->compile_away_cond_eff);

    bzero(pddl, sizeof(*pddl));
    pddl->cfg = *cfg;

    BOR_INFO(err, "Processing %s and %s.", domain_fn, problem_fn);

    if (!checkConfig(cfg)){
        BOR_INFO_PREFIX_POP(err);
        BOR_TRACE_RET(err, -1);
    }

    BOR_INFO2(err, "Parsing domain lisp file...");
    pddl->domain_lisp = pddlLispParse(domain_fn, err);
    if (pddl->domain_lisp == NULL){
        BOR_INFO_PREFIX_POP(err);
        BOR_TRACE_RET(err, -1);
    }

    BOR_INFO2(err, "Parsing problem lisp file...");
    pddl->problem_lisp = pddlLispParse(problem_fn, err);
    if (pddl->problem_lisp == NULL){
        BOR_INFO_PREFIX_POP(err);
        if (pddl->domain_lisp)
            pddlLispDel(pddl->domain_lisp);
        BOR_TRACE_RET(err, -1);
    }

    BOR_INFO2(err, "Parsing entire contents of domain/problem PDDL...");
    pddl->domain_name = parseDomainName(pddl->domain_lisp, err);
    if (pddl->domain_name == NULL)
        goto pddl_fail;

    pddl->problem_name = parseProblemName(pddl->problem_lisp, err);
    if (pddl->domain_name == NULL)
        goto pddl_fail;

    if (checkDerivedPredicates(pddl, err) != 0
            || checkDomainName(pddl, err) != 0
            || pddlRequireParse(pddl, err) != 0
            || pddlTypesParse(pddl, err) != 0
            || pddlObjsParse(pddl, err) != 0
            || pddlPredsParse(pddl, err) != 0
            || pddlFuncsParse(pddl, err) != 0
            || parseInit(pddl, err) != 0
            || parseGoal(pddl, err) != 0
            || pddlActionsParse(pddl, err) != 0
            || parseMetric(pddl, pddl->problem_lisp, err) != 0){
        goto pddl_fail;
    }
    pddlTypesBuildObjTypeMap(&pddl->type, pddl->obj.obj_size);
    BOR_INFO2(err, "PDDL files processed.");

    if (cfg->normalize){
        pddlNormalize(pddl);
        BOR_INFO2(err, "PDDL task normalized.");
    }

    if (cfg->remove_empty_types){
        pddlRemoveEmptyTypes(pddl, err);
        if (cfg->normalize){
            pddlNormalize(pddl);
            BOR_INFO2(err, "PDDL task normalized again.");
        }
    }

    if (cfg->compile_away_cond_eff){
        BOR_INFO2(err, "Compiling away conditional effects...");
        pddlCompileAwayCondEff(pddl);
        BOR_INFO2(err, "Conditional effects compiled away.");
    }

    pddlCheckSizeTypes(pddl);
    BOR_INFO(err, "Number of PDDL Types: %d", pddl->type.type_size);
    BOR_INFO(err, "Number of PDDL Objects: %d", pddl->obj.obj_size);
    BOR_INFO(err, "Number of PDDL Predicates: %d", pddl->pred.pred_size);
    BOR_INFO(err, "Number of PDDL Functions: %d", pddl->func.pred_size);
    BOR_INFO(err, "Number of PDDL Actions: %d", pddl->action.action_size);
    BOR_INFO(err, "PDDL Metric: %d", pddl->metric);

    BOR_INFO_PREFIX_POP(err);
    return 0;

pddl_fail:
    BOR_INFO_PREFIX_POP(err);
    if (pddl != NULL)
        pddlFree(pddl);
    BOR_TRACE_RET(err, -1);
}

void pddlInitCopy(pddl_t *dst, const pddl_t *src)
{
    bzero(dst, sizeof(*dst));
    dst->cfg = src->cfg;
    dst->domain_lisp = pddlLispClone(src->domain_lisp);
    dst->problem_lisp = pddlLispClone(src->problem_lisp);
    if (src->domain_name != NULL)
        dst->domain_name = BOR_STRDUP(src->domain_name);
    if (src->problem_name != NULL)
        dst->problem_name = BOR_STRDUP(src->problem_name);
    dst->require = src->require;
    pddlTypesInitCopy(&dst->type, &src->type);
    pddlObjsInitCopy(&dst->obj, &src->obj);
    pddlPredsInitCopy(&dst->pred, &src->pred);
    pddlPredsInitCopy(&dst->func, &src->func);
    if (src->init != NULL)
        dst->init = PDDL_COND_CAST(pddlCondClone(&src->init->cls), part);
    if (src->goal != NULL)
        dst->goal = pddlCondClone(src->goal);
    pddlActionsInitCopy(&dst->action, &src->action);
    dst->metric = src->metric;
    dst->normalized = src->normalized;
}

pddl_t *pddlNew(const char *domain_fn, const char *problem_fn,
                const pddl_config_t *cfg, bor_err_t *err)
{
    pddl_t *pddl = BOR_ALLOC(pddl_t);

    if (pddlInit(pddl, domain_fn, problem_fn, cfg, err) != 0){
        BOR_FREE(pddl);
        return NULL;
    }

    return pddl;
}

void pddlDel(pddl_t *pddl)
{
    pddlFree(pddl);
    BOR_FREE(pddl);
}

void pddlFree(pddl_t *pddl)
{
    if (pddl->domain_lisp)
        pddlLispDel(pddl->domain_lisp);
    if (pddl->problem_lisp)
        pddlLispDel(pddl->problem_lisp);
    if (pddl->domain_name != NULL)
        BOR_FREE(pddl->domain_name);
    if (pddl->problem_name != NULL)
        BOR_FREE(pddl->problem_name);
    pddlTypesFree(&pddl->type);
    pddlObjsFree(&pddl->obj);
    pddlPredsFree(&pddl->pred);
    pddlPredsFree(&pddl->func);
    if (pddl->init)
        pddlCondDel(&pddl->init->cls);
    if (pddl->goal)
        pddlCondDel(pddl->goal);
    pddlActionsFree(&pddl->action);
}

static int markNegPre(pddl_cond_t *c, void *_m)
{
    pddl_cond_atom_t *atom;
    int *m = _m;

    if (c->type == PDDL_COND_ATOM){
        atom = PDDL_COND_CAST(c, atom);
        if (atom->neg)
            m[atom->pred] = 1;
    }

    return 0;
}

static int markNegPreWhen(pddl_cond_t *c, void *_m)
{
    pddl_cond_when_t *when;

    if (c->type == PDDL_COND_WHEN){
        when = PDDL_COND_CAST(c, when);
        pddlCondTraverse(when->pre, markNegPre, NULL, _m);
    }

    return 0;
}

/** Sets to 1 indexes in {np} of those predicates that are not static and
 *  appear as negative preconditions */
static void findNonStaticPredInNegPre(pddl_t *pddl, int *np)
{
    int i;

    bzero(np, sizeof(int) * pddl->pred.pred_size);
    for (i = 0; i < pddl->action.action_size; ++i){
        pddlCondTraverse(pddl->action.action[i].pre, markNegPre, NULL, np);
        pddlCondTraverse(pddl->action.action[i].eff, markNegPreWhen, NULL, np);
    }
    // Also, check the goal
    if (pddl->goal)
        pddlCondTraverse(pddl->goal, markNegPre, NULL, np);

    for (i = 0; i < pddl->pred.pred_size; ++i){
        if (pddlPredIsStatic(pddl->pred.pred + i))
            np[i] = 0;
    }
}

/** Create a new NOT-... predicate and returns its ID */
static int createNewNotPred(pddl_t *pddl, int pred_id)
{
    pddl_pred_t *pos = pddl->pred.pred + pred_id;
    pddl_pred_t *neg;
    int name_size;
    char *name;

    name_size = strlen(pos->name) + 4;
    name = BOR_ALLOC_ARR(char, name_size + 1);
    strcpy(name, "NOT-");
    strcpy(name + 4, pos->name);

    neg = pddlPredsAddCopy(&pddl->pred, pred_id);
    if (neg->name != NULL)
        BOR_FREE(neg->name);
    neg->name = name;
    neg->neg_of = pred_id;
    pddl->pred.pred[pred_id].neg_of = neg->id;

    return neg->id;
}

static int replaceNegPre(pddl_cond_t **c, void *_ids)
{
    int *ids = _ids;
    int pos = ids[0];
    int neg = ids[1];
    pddl_cond_atom_t *atom;

    if ((*c)->type == PDDL_COND_ATOM){
        atom = PDDL_COND_CAST(*c, atom);
        if (atom->pred == pos && atom->neg){
            atom->pred = neg;
            atom->neg = 0;
        }
    }

    return 0;
}

static int replaceNegEff(pddl_cond_t **c, void *_ids)
{
    int *ids = _ids;
    int pos = ids[0];
    int neg = ids[1];
    pddl_cond_t *c2;
    pddl_cond_atom_t *atom, *not_atom;
    pddl_cond_when_t *when;
    pddl_cond_part_t *and;

    if ((*c)->type == PDDL_COND_WHEN){
        when = PDDL_COND_CAST(*c, when);
        pddlCondRebuild(&when->pre, NULL, replaceNegPre, _ids);
        pddlCondRebuild(&when->eff, replaceNegEff, NULL, _ids);
        return -1;

    }else if ((*c)->type == PDDL_COND_ATOM){
        atom = PDDL_COND_CAST(*c, atom);
        if (atom->pred == pos){
            // Create new NOT atom and flip negation
            c2 = pddlCondClone(*c);
            not_atom = PDDL_COND_CAST(c2, atom);
            not_atom->pred = neg;
            not_atom->neg = !atom->neg;

            // Transorm atom to (and atom)
            *c = pddlCondAtomToAnd(*c);
            and = PDDL_COND_CAST(*c, part);
            pddlCondPartAdd(and, c2);

            // Prevent recursion
            return -1;
        }
    }

    return 0;
}

static void compileOutNegPreInAction(pddl_t *pddl, int pos, int neg,
                                     pddl_action_t *a)
{
    int ids[2] = { pos, neg };
    pddlCondRebuild(&a->pre, NULL, replaceNegPre, ids);
    pddlCondRebuild(&a->eff, replaceNegEff, NULL, ids);
    pddlActionNormalize(a, pddl);
}

static void compileOutNegPre(pddl_t *pddl, int pos, int neg)
{
    int i;

    for (i = 0; i < pddl->action.action_size; ++i)
        compileOutNegPreInAction(pddl, pos, neg, pddl->action.action + i);

    if (pddl->goal){
        int ids[2] = { pos, neg };
        pddlCondRebuild(&pddl->goal, NULL, replaceNegPre, ids);
    }
}

static int initHasFact(const pddl_t *pddl, int pred,
                       int arg_size, const pddl_obj_id_t *arg)
{
    bor_list_t *item;
    const pddl_cond_t *c;
    const pddl_cond_atom_t *a;
    int i;

    BOR_LIST_FOR_EACH(&pddl->init->part, item){
        c = BOR_LIST_ENTRY(item, const pddl_cond_t, conn);
        if (c->type != PDDL_COND_ATOM)
            continue;
        a = PDDL_COND_CAST(c, atom);
        if (a->pred != pred || a->arg_size != arg_size)
            continue;
        for (i = 0; i < arg_size; ++i){
            if (a->arg[i].obj != arg[i])
                break;
        }
        if (i == arg_size)
            return 1;
    }

    return 0;
}

static void addNotPredsToInitRec(pddl_t *pddl, int pos, int neg,
                                 int arg_size, pddl_obj_id_t *arg,
                                 const pddl_pred_t *pred, int argi)
{
    pddl_cond_atom_t *a;
    const pddl_obj_id_t *obj;
    int obj_size;

    if (argi == arg_size){
        if (!initHasFact(pddl, pos, arg_size, arg)){
            a = pddlCondCreateFactAtom(neg, arg_size, arg);
            pddlCondPartAdd(pddl->init, &a->cls);
        }

        return;
    }

    obj = pddlTypesObjsByType(&pddl->type, pred->param[argi], &obj_size);
    for (int i = 0; i < obj_size; ++i){
        arg[argi] = obj[i];
        addNotPredsToInitRec(pddl, pos, neg, arg_size, arg, pred, argi + 1);
    }
}

static void addNotPredsToInit(pddl_t *pddl, int pos, int neg)
{
    const pddl_pred_t *pos_pred = pddl->pred.pred + pos;
    pddl_obj_id_t arg[pos_pred->param_size];

    // Recursivelly try all possible objects for each argument
    addNotPredsToInitRec(pddl, pos, neg,
                         pos_pred->param_size, arg, pos_pred, 0);
}

/** Compile out negative preconditions if they are not static */
static void compileOutNonStaticNegPre(pddl_t *pddl)
{
    int size, *negpred;

    size = pddl->pred.pred_size;
    negpred = BOR_ALLOC_ARR(int, size);
    findNonStaticPredInNegPre(pddl, negpred);

    for (int i = 0; i < size; ++i){
        if (negpred[i]){
            int not = createNewNotPred(pddl, i);
            compileOutNegPre(pddl, i, not);
            addNotPredsToInit(pddl, i, not);
        }
    }
    BOR_FREE(negpred);
}

static int isFalsePre(const pddl_cond_t *c)
{
    if (c->type == PDDL_COND_BOOL){
        const pddl_cond_bool_t *b = PDDL_COND_CAST(c, bool);
        return !b->val;
    }
    return 0;
}

static void removeIrrelevantActions(pddl_t *pddl)
{
    for (int ai = 0; ai < pddl->action.action_size;){
        pddl_action_t *a = pddl->action.action + ai;
        a->pre = pddlCondDeconflictPre(a->pre, pddl, &a->param);
        a->eff = pddlCondDeconflictEff(a->eff, pddl, &a->param);

        if (isFalsePre(a->pre) || !pddlCondHasAtom(a->eff)){
            pddlActionFree(a);
            if (ai != pddl->action.action_size - 1)
                *a = pddl->action.action[pddl->action.action_size - 1];
            --pddl->action.action_size;
        }else{
            ++ai;
        }
    }
}

static int removeActionsWithUnsatisfiableArgs(pddl_t *pddl)
{
    int ret = 0;
    for (int ai = 0; ai < pddl->action.action_size;){
        pddl_action_t *a = pddl->action.action + ai;
        int remove = 0;
        for (int pi = 0; pi < a->param.param_size; ++pi){
            if (pddlTypeNumObjs(&pddl->type, a->param.param[pi].type) == 0){
                remove = 1;
                break;
            }
        }

        if (remove){
            pddlActionFree(a);
            if (ai != pddl->action.action_size - 1)
                *a = pddl->action.action[pddl->action.action_size - 1];
            --pddl->action.action_size;
            ret = 1;
        }else{
            ++ai;
        }
    }

    return ret;
}

static int isStaticPreUnreachable(const pddl_t *pddl, const pddl_cond_t *c)
{
    pddl_cond_const_it_atom_t it;
    const pddl_cond_atom_t *atom;
    PDDL_COND_FOR_EACH_ATOM(c, &it, atom){
        const pddl_pred_t *pred = pddl->pred.pred + atom->pred;
        if (pred->id != pddl->pred.eq_pred
                && pddlPredIsStatic(pred)
                && !pred->in_init){
            return 1;
        }
    }
    return 0;
}

static int isInequalityUnsatisfiable(const pddl_t *pddl,
                                     const pddl_action_t *action)
{
    pddl_cond_const_it_atom_t it;
    const pddl_cond_atom_t *atom;
    PDDL_COND_FOR_EACH_ATOM(action->pre, &it, atom){
        if (atom->neg && atom->pred == pddl->pred.eq_pred){
            int param1 = atom->arg[0].param;
            int obj1 = atom->arg[0].obj;
            int param2 = atom->arg[1].param;
            int obj2 = atom->arg[1].obj;
            if (param1 >= 0){
                int type = action->param.param[param1].type;
                if (pddlTypeNumObjs(&pddl->type, type) == 1)
                    obj1 = pddlTypeGetObj(&pddl->type, type, 0);
            }
            if (param2 >= 0){
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

static int removeUnreachableActions(pddl_t *pddl)
{
    int ret = 0;
    for (int ai = 0; ai < pddl->action.action_size;){
        pddl_action_t *a = pddl->action.action + ai;
        a->pre = pddlCondDeconflictPre(a->pre, pddl, &a->param);
        a->eff = pddlCondDeconflictEff(a->eff, pddl, &a->param);

        if (isStaticPreUnreachable(pddl, a->pre)
                || isInequalityUnsatisfiable(pddl, a)){
            pddlActionFree(a);
            if (ai != pddl->action.action_size - 1)
                *a = pddl->action.action[pddl->action.action_size - 1];
            --pddl->action.action_size;
            ret = 1;
        }else{
            ++ai;
        }
    }

    return ret;
}

static void pddlResetPredReadWrite(pddl_t *pddl)
{
    for (int i = 0; i < pddl->pred.pred_size; ++i)
        pddl->pred.pred[i].read = pddl->pred.pred[i].write = 0;
    for (int i = 0; i < pddl->action.action_size; ++i){
        const pddl_action_t *a = pddl->action.action + i;
        pddlCondSetPredRead(a->pre, &pddl->pred);
        pddlCondSetPredReadWriteEff(a->eff, &pddl->pred);
    }
}

void pddlNormalize(pddl_t *pddl)
{
    removeActionsWithUnsatisfiableArgs(pddl);

    for (int i = 0; i < pddl->action.action_size; ++i)
        pddlActionNormalize(pddl->action.action + i, pddl);

    for (int i = 0; i < pddl->action.action_size; ++i)
        pddlActionSplit(pddl->action.action + i, pddl);

    removeIrrelevantActions(pddl);

#ifdef PDDL_DEBUG
    for (int i = 0; i < pddl->action.action_size; ++i){
        pddlActionAssertPreConjuction(pddl->action.action + i);
    }
#endif

    if (pddl->goal)
        pddl->goal = pddlCondNormalize(pddl->goal, pddl, NULL);

    compileOutNonStaticNegPre(pddl);
    removeIrrelevantActions(pddl);
    do {
        pddlResetPredReadWrite(pddl);
    } while (removeUnreachableActions(pddl));
    pddl->normalized = 1;
}

static void compileAwayCondEff(pddl_t *pddl, int only_non_static)
{
    pddl_action_t *a, *new_a;
    pddl_cond_when_t *w;
    pddl_cond_t *neg_pre;
    int asize;
    int change;

    do {
        change = 0;
        pddlNormalize(pddl);

        asize = pddl->action.action_size;
        for (int ai = 0; ai < asize; ++ai){
            a = pddl->action.action + ai;
            if (only_non_static){
                w = pddlCondRemoveFirstNonStaticWhen(a->eff, pddl);
            }else{
                w = pddlCondRemoveFirstWhen(a->eff, pddl);
            }
            if (w != NULL){
                // Create a new action
                new_a = pddlActionsAddCopy(&pddl->action, ai);

                // Get the original action again, because pddlActionsAdd()
                // could realloc the array.
                a = pddl->action.action + ai;

                // The original takes additional precondition which is the
                // negation of w->pre
                if ((neg_pre = pddlCondNegate(w->pre, pddl)) == NULL){
                    // This shoud never fail, because we force
                    // normalization before this.
                    BOR_FATAL2("Fatal Error: Encountered problem in"
                               " the normalization.");
                }
                a->pre = pddlCondNewAnd2(a->pre, neg_pre);

                // The new action extends both pre and eff by w->pre and
                // w->eff.
                new_a->pre = pddlCondNewAnd2(new_a->pre, pddlCondClone(w->pre));
                new_a->eff = pddlCondNewAnd2(new_a->eff, pddlCondClone(w->eff));

                pddlCondDel(&w->cls);
                change = 1;
            }
        }
    } while (change);
    pddlResetPredReadWrite(pddl);
}

void pddlCompileAwayCondEff(pddl_t *pddl)
{
    compileAwayCondEff(pddl, 0);
}

void pddlCompileAwayNonStaticCondEff(pddl_t *pddl)
{
    compileAwayCondEff(pddl, 1);
}

int pddlPredFuncMaxParamSize(const pddl_t *pddl)
{
    int max = 0;

    for (int i = 0; i < pddl->pred.pred_size; ++i)
        max = BOR_MAX(max, pddl->pred.pred[i].param_size);
    for (int i = 0; i < pddl->func.pred_size; ++i)
        max = BOR_MAX(max, pddl->func.pred[i].param_size);

    return max;
}

void pddlCheckSizeTypes(const pddl_t *pddl)
{
    unsigned long max_size;

    max_size = (1ul << (sizeof(pddl_obj_size_t) * 8)) - 1;
    if (pddl->obj.obj_size > max_size){
        BOR_FATAL("The problem has %d objects, but pddl_obj_size_t can"
                  " hold only %lu.",
                  pddl->obj.obj_size,
                  sizeof(pddl_obj_size_t) * 8 - 1);
    }

    max_size = (1ul << (sizeof(pddl_action_param_size_t) * 8)) - 1;
    for (int ai = 0; ai < pddl->action.action_size; ++ai){
        int param_size = pddl->action.action[ai].param.param_size;
        if (param_size > max_size){
            BOR_FATAL("The action %s has %d parameters, but"
                      "pddl_action_param_size_t can hold only %lu.",
                      pddl->action.action[ai].name,
                      param_size,
                      sizeof(pddl_action_param_size_t) * 8 - 1);
        }
    }
}

void pddlAddObjectTypes(pddl_t *pddl)
{
    for (pddl_obj_id_t obj_id = 0; obj_id < pddl->obj.obj_size; ++obj_id){
        pddl_obj_t *obj = pddl->obj.obj + obj_id;
        ASSERT(obj->type >= 0);
        if (pddlTypeNumObjs(&pddl->type, obj->type) <= 1)
            continue;

        char *name = BOR_ALLOC_ARR(char, strlen(obj->name) + 8 + 1);
        sprintf(name, "%s-OBJTYPE", obj->name);
        int type_id = pddlTypesAdd(&pddl->type, name, obj->type);
        ASSERT(type_id == pddl->type.type_size - 1);
        pddlTypesAddObj(&pddl->type, obj_id, type_id);
        obj->type = type_id;
        BOR_FREE(name);
    }
    pddlTypesBuildObjTypeMap(&pddl->type, pddl->obj.obj_size);
}


static void removeObjsFromInit(pddl_t *pddl,
                               const pddl_obj_id_t *remap,
                               bor_err_t *err)
{
    pddl_cond_t *c = pddlCondNewEmptyAnd();
    pddl_cond_part_t *init = PDDL_COND_CAST(c, part);
    int rm_atom = 0;
    int rm_ass = 0;
    while (!borListEmpty(&pddl->init->part)){
        bor_list_t *item = borListNext(&pddl->init->part);
        borListDel(item);
        pddl_cond_t *c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type == PDDL_COND_ATOM){
            pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
            for (int i = 0; i < a->arg_size; ++i){
                if (a->arg[i].obj >= 0 && remap[a->arg[i].obj] == -1){
                    pddlCondDel(c);
                    c = NULL;
                    ++rm_atom;
                    break;

                }else if (a->arg[i].obj >= 0){
                    a->arg[i].obj = remap[a->arg[i].obj];
                }
            }

        }else if (c->type == PDDL_COND_ASSIGN){
            pddl_cond_func_op_t *ass = PDDL_COND_CAST(c, func_op);
            ASSERT(ass->fvalue == NULL);
            if (ass->lvalue != NULL){
                pddl_cond_atom_t *a = ass->lvalue;
                for (int i = 0; i < a->arg_size; ++i){
                    if (a->arg[i].obj >= 0 && remap[a->arg[i].obj] == -1){
                        pddlCondDel(c);
                        c = NULL;
                        ++rm_ass;
                        break;

                    }else if (a->arg[i].obj >= 0){
                        a->arg[i].obj = remap[a->arg[i].obj];
                    }
                }
            }
        }

        if (c != NULL)
            borListAppend(&init->part, &c->conn);
    }

    pddlCondDel(&pddl->init->cls);
    pddl->init = init;

    BOR_INFO(err, "Removed %d atoms and %d assignments from the initial state",
             rm_atom, rm_ass);
}

void pddlRemoveObjs(pddl_t *pddl, const bor_iset_t *rm_obj, bor_err_t *err)
{
    if (borISetSize(rm_obj) == 0)
        return;
    BOR_INFO_PREFIX_PUSH(err, "PDDL rm objs: ");
    BOR_INFO(err, "Removing %d objects", borISetSize(rm_obj));

    int obj_size = pddl->obj.obj_size;
    pddl_obj_id_t *remap = BOR_ALLOC_ARR(pddl_obj_id_t, obj_size);
    for (int i = 0, idx = 0, id = 0; i < obj_size; ++i){
        if (idx < borISetSize(rm_obj) && borISetGet(rm_obj, idx) == i){
            remap[i] = -1;
            ++idx;
        }else{
            remap[i] = id++;
        }
    }

    removeObjsFromInit(pddl, remap, err);
    pddlCondRemapObjs(pddl->goal, remap);
    pddlObjsRemap(&pddl->obj, remap);
    pddlTypesRemapObjs(&pddl->type, remap);
    pddlActionsRemapObjs(&pddl->action, remap);

    BOR_FREE(remap);
    BOR_INFO_PREFIX_POP(err);
}

void pddlRemoveEmptyTypes(pddl_t *pddl, bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Rm empty-types: ");
    int *type_remap = BOR_CALLOC_ARR(int, pddl->type.type_size);
    int *pred_remap = BOR_CALLOC_ARR(int, pddl->pred.pred_size);
    int *func_remap = BOR_CALLOC_ARR(int, pddl->func.pred_size);
    int type_size = pddl->type.type_size;
    int pred_size = pddl->pred.pred_size;
    int func_size = pddl->func.pred_size;
    int action_size = pddl->action.action_size;

    pddlTypesRemoveEmpty(&pddl->type, pddl->obj.obj_size, type_remap);
    BOR_INFO(err, "Removed %d empty types", type_size - pddl->type.type_size);
    if (type_size != pddl->type.type_size){
        pddlObjsRemapTypes(&pddl->obj, type_remap);
        pddlPredsRemapTypes(&pddl->pred, type_remap, pred_remap);
        BOR_INFO(err, "Removed %d predicates", pred_size - pddl->pred.pred_size);
        pddlPredsRemapTypes(&pddl->func, type_remap, func_remap);
        BOR_INFO(err, "Removed %d functions", func_size - pddl->func.pred_size);
        pddlActionsRemapTypesAndPreds(&pddl->action, type_remap,
                                      pred_remap, func_remap);
        BOR_INFO(err, "Removed %d actions",
                 action_size - pddl->action.action_size);

        if (pred_size != pddl->pred.pred_size
                || func_size != pddl->func.pred_size){

            if (pddlCondRemapPreds(&pddl->init->cls,
                                   pred_remap, func_remap) != 0){
                BOR_INFO2(err, "The task is unsolvable, because the initial"
                               " state is false");
                pddlCondDel(&pddl->init->cls);
                pddl_cond_t *c = pddlCondNewEmptyAnd();
                pddl->init = PDDL_COND_CAST(c, part);
                pddl_cond_bool_t *b = pddlCondNewBool(0);
                pddlCondPartAdd(pddl->init, &b->cls);
            }

            if (pddlCondRemapPreds(pddl->goal, pred_remap, func_remap) != 0){
                BOR_INFO2(err, "The task is unsolvable, because the goal"
                               " is false");
                pddlCondDel(pddl->goal);
                pddl_cond_bool_t *b = pddlCondNewBool(0);
                pddl->goal = &b->cls;
            }
        }
    }


    BOR_FREE(type_remap);
    BOR_FREE(pred_remap);
    BOR_FREE(func_remap);
    BOR_INFO_PREFIX_POP(err);
}


struct unify_subst {
    int var;
    int var_type;
    int is_fixed;
    pddl_obj_id_t obj;
};
typedef struct unify_subst unify_subst_t;

static void mapSet(unify_subst_t *map, int size, int from, int to, int to_type)
{
    for (int i = 0; i < size; ++i){
        if (map[i].var == from){
            map[i].var = to;
            map[i].var_type = to_type;
        }
    }
}

static void mapSetObj(unify_subst_t *map, int size, int from, pddl_obj_id_t to)
{
    for (int i = 0; i < size; ++i){
        if (map[i].var == from){
            map[i].var = -1;
            map[i].obj = to;
        }
    }
}

static int unifyAtoms(const pddl_t *pddl,
                      const pddl_params_t *pre_params,
                      const pddl_params_t *mgroup_params,
                      const pddl_cond_atom_t *a_pre,
                      const pddl_cond_atom_t *a_mgroup,
                      unify_subst_t *map,
                      int initial)
{
    if (a_pre->pred != a_mgroup->pred)
        return 0;

    int map_size = mgroup_params->param_size + pre_params->param_size;
    if (initial){
        for (int i = 0; i < pre_params->param_size; ++i){
            map[i].var = i;
            map[i].var_type = pre_params->param[i].type;
            map[i].obj = PDDL_OBJ_ID_UNDEF;
            map[i].is_fixed = 0;
        }
        int param_size = pre_params->param_size;
        for (int i = 0; i < mgroup_params->param_size; ++i){
            map[param_size + i].var = param_size + i;
            map[param_size + i].var_type = mgroup_params->param[i].type;
            map[param_size + i].obj = PDDL_OBJ_ID_UNDEF;
            map[param_size + i].is_fixed
                    = !mgroup_params->param[i].is_counted_var;
        }
    }

    for (int argi = 0; argi < a_pre->arg_size; ++argi){
        pddl_obj_id_t pre_obj = a_pre->arg[argi].obj;
        int pre_param = a_pre->arg[argi].param;
        int pre_type = -1;
        if (pre_param >= 0){
            pre_param = map[pre_param].var;
            pre_type = map[pre_param].var_type;
            pre_obj = map[pre_param].obj;
        }
        pddl_obj_id_t mg_obj = a_mgroup->arg[argi].obj;
        int mg_param = a_mgroup->arg[argi].param;
        int mg_type = -1;
        if (mg_param >= 0){
            mg_param += pre_params->param_size;
            mg_param = map[mg_param].var;
            mg_type = map[mg_param].var_type;
            mg_obj = map[mg_param].obj;
        }

        if (pre_param >= 0 && mg_param >= 0){
            int from = -1, to = -1, to_type = -1;
            if (pddlTypesIsSubset(&pddl->type, mg_type, pre_type)){
                from = pre_param;
                to = mg_param;
                to_type = mg_type;
            }else if (pddlTypesIsSubset(&pddl->type, pre_type, mg_type)){
                from = mg_param;
                to = pre_param;
                to_type = pre_type;
            }else{
                return 0;
            }
            // Variables with empty types are actually not unifiable
            if (pddlTypeNumObjs(&pddl->type, to_type) == 0)
                return 0;
            mapSet(map, map_size, from, to, to_type);

        }else if (pre_param >= 0){
            if (!pddlTypesObjHasType(&pddl->type, pre_type, mg_obj))
                return 0;
            mapSetObj(map, map_size, pre_param, mg_obj);

        }else if (mg_param >= 0){
            if (!pddlTypesObjHasType(&pddl->type, mg_type, pre_obj))
                return 0;
            mapSetObj(map, map_size, mg_param, pre_obj);

        }else{
            if (pre_obj != mg_obj)
                return 0;
        }
    }

    int param_size = pre_params->param_size;
    for (int i = 0; i < mgroup_params->param_size; ++i){
        if (!map[param_size + i].is_fixed)
            continue;
        int var = map[param_size + i].var;
        for (int j = 0; j < map_size; ++j){
            if (map[j].var == var)
                map[j].is_fixed = 1;
        }
    }

    return 1;
}

static int preAtomsAreEqual(const pddl_params_t *pre_param,
                            const pddl_cond_atom_t *pre1,
                            const pddl_cond_atom_t *pre2,
                            const unify_subst_t *map)
{
    if (pre1->pred != pre2->pred)
        return 0;
    for (int ai = 0; ai < pre1->arg_size; ++ai){
        if (pre1->arg[ai].param >= 0 && pre2->arg[ai].param >= 0){
            int p1 = pre1->arg[ai].param;
            int p2 = pre2->arg[ai].param;
            if (map[p1].var != map[p2].var || map[p1].obj != map[p2].obj)
                return 0;
            if (map[p1].var == map[p2].var && !map[p1].is_fixed)
                return 0;
        }else if (pre1->arg[ai].param < 0 && pre2->arg[ai].param < 0){
            if (pre1->arg[ai].obj != pre2->arg[ai].obj)
                return 0;
        }else{
            return 0;
        }
    }
    return 1;
}


static void condRestrictType(const pddl_t *pddl,
                             int var,
                             int type,
                             int parent_type,
                             pddl_cond_part_t *and)
{
    pddl_cond_t *_or = pddlCondNewEmptyOr();
    pddl_cond_part_t *or = PDDL_COND_CAST(_or, part);
    int obj_size;
    const pddl_obj_id_t *obj;
    obj = pddlTypesObjsByType(&pddl->type, parent_type, &obj_size);
    for (int i = 0; i < obj_size; ++i){
        pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
        eq->pred = pddl->pred.eq_pred;
        eq->arg[0].param = var;
        eq->arg[1].obj = obj[i];
        pddlCondPartAdd(or, &eq->cls);
    }
    if (pddlCondPartIsEmpty(or)){
        pddlCondDel(_or);
    }else{
        pddlCondPartAdd(and, _or);
    }
}

static void condRestrictTypeAndObj(const pddl_t *pddl,
                                   const pddl_params_t *param,
                                   const pddl_cond_atom_t *atom,
                                   const unify_subst_t *map,
                                   const int *found,
                                   pddl_cond_part_t *and)
{
    for (int i = 0; i < atom->arg_size; ++i){
        int p = atom->arg[i].param;
        if (p >= 0
                && !found[p]
                && map[p].var >= 0
                && map[p].var_type != param->param[p].type){
            condRestrictType(pddl, p, map[p].var_type, param->param[p].type, and);

        }else if (p >= 0 && map[p].var < 0){
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
            eq->pred = pddl->pred.eq_pred;
            eq->arg[0].param = p;
            eq->arg[1].obj = map[p].obj;
            pddlCondPartAdd(and, &eq->cls);
        }
    }
}

static void condRestrictEq(const pddl_t *pddl,
                           const pddl_params_t *param,
                           const pddl_cond_atom_t *a1,
                           const pddl_cond_atom_t *a2,
                           const unify_subst_t *map,
                           const int *found1,
                           const int *found2,
                           pddl_cond_part_t *and)
{
    pddl_cond_t *_or = pddlCondNewEmptyOr();
    pddl_cond_part_t *or = PDDL_COND_CAST(_or, part);

    for (int i = 0; i < a1->arg_size; ++i){
        int p1 = a1->arg[i].param;
        int p2 = a2->arg[i].param;
        if (p1 >= 0 && p2 >= 0){
            if (!found1[p1] && !found2[p2] && p1 != p2){
                pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
                eq->pred = pddl->pred.eq_pred;
                eq->arg[0].param = p1;
                eq->arg[1].param = p2;
                eq->neg = 1;
                pddlCondPartAdd(or, &eq->cls);
            }

        }else if (p1 >= 0){
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
            eq->pred = pddl->pred.eq_pred;
            eq->arg[0].param = p1;
            eq->arg[1].obj = a2->arg[i].obj;
            eq->neg = 1;
            pddlCondPartAdd(or, &eq->cls);

        }else if (p2 >= 0){
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
            eq->pred = pddl->pred.eq_pred;
            eq->arg[0].param = p2;
            eq->arg[1].obj = a1->arg[i].obj;
            eq->neg = 1;
            pddlCondPartAdd(or, &eq->cls);
        }
    }

    if (pddlCondPartIsEmpty(or)){
        pddlCondDel(_or);
    }else{
        pddlCondPartAdd(and, _or);
    }
}

static pddl_cond_t *condCompileMutex(const pddl_t *pddl,
                                     const pddl_params_t *pre_param,
                                     const pddl_cond_atom_t *pre1,
                                     const pddl_cond_atom_t *pre2,
                                     const unify_subst_t *map)
{
    int eq_pred = pddl->pred.eq_pred;
    int found1[pre1->arg_size];
    int found2[pre2->arg_size];
    bzero(found1, sizeof(int) * pre1->arg_size);
    bzero(found2, sizeof(int) * pre2->arg_size);

    pddl_cond_t *_and = pddlCondNewEmptyAnd();
    pddl_cond_part_t *and = PDDL_COND_CAST(_and, part);

    for (int i1 = 0; i1 < pre1->arg_size; ++i1){
        int p1 = pre1->arg[i1].param;
        if (p1 < 0 || map[p1].var < 0 || !map[p1].is_fixed)
            continue;

        for (int i2 = 0; i2 < pre2->arg_size; ++i2){
            int p2 = pre2->arg[i2].param;
            if (p2 >= 0 && map[p1].var == map[p2].var){
                ASSERT(map[p1].var_type == map[p2].var_type);
                found1[p1] = 1;
                found2[p2] = 1;

                if (p1 == p2)
                    continue;
                pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
                eq->pred = eq_pred;
                eq->arg[0].param = p1;
                eq->arg[1].param = p2;
                pddlCondPartAdd(and, &eq->cls);

                if (map[p1].var_type != pre_param->param[p1].type
                        && map[p2].var_type != pre_param->param[p2].type){
                    condRestrictType(pddl, p1, map[p1].var_type,
                                     pre_param->param[p1].type, and);
                    condRestrictType(pddl, p2, map[p2].var_type,
                                     pre_param->param[p2].type, and);
                }
            }
        }
    }

    condRestrictTypeAndObj(pddl, pre_param, pre1, map, found1, and);
    condRestrictTypeAndObj(pddl, pre_param, pre2, map, found2, and);

    if (pre1->pred == pre2->pred)
        condRestrictEq(pddl, pre_param, pre1, pre2, map, found1, found2, and);

    if (pddlCondPartIsEmpty(and)){
        pddlCondDel(_and);
        return &(pddlCondNewBool(0)->cls);
    }

    pddl_cond_t *ret = _and;
    ret = pddlCondNormalize(ret, pddl, pre_param);
    ret = pddlCondNegate(ret, pddl);
    ret = pddlCondNormalize(ret, pddl, pre_param);
    ret = pddlCondDeduplicate(ret, pddl);
    return ret;
}

static void preCompileInLiftedMGroups(pddl_t *pddl,
                                      const pddl_params_t *pre_param,
                                      const pddl_cond_t *pre,
                                      const pddl_lifted_mgroup_t *mgroup,
                                      bor_err_t *err)
{
    pddl_cond_const_it_atom_t it1, it2;
    const pddl_cond_atom_t *a1, *a2;
    int map_size = mgroup->param.param_size + pre_param->param_size;
    unify_subst_t *map = BOR_ALLOC_ARR(unify_subst_t, map_size);

    pddl_cond_arr_t carr = PDDL_COND_ARR_INIT;

    PDDL_COND_FOR_EACH_ATOM(pre, &it1, a1){
        if (a1->neg)
            continue;
        // TODO: (in)equality preconditions

        for (int mi1 = 0; mi1 < mgroup->cond.size; ++mi1){
            const pddl_cond_atom_t *ma1;
            ma1 = PDDL_COND_CAST(mgroup->cond.cond[mi1], atom);
            if (ma1->pred != a1->pred)
                continue;

            if (!unifyAtoms(pddl, pre_param, &mgroup->param, a1, ma1, map, 1))
                continue;

            it2 = it1;
            PDDL_COND_FOR_EACH_ATOM_CONT(&it2, a2){
                if (a2->neg)
                    continue;
                for (int mi2 = 0; mi2 < mgroup->cond.size; ++mi2){
                    const pddl_cond_atom_t *ma2;
                    ma2 = PDDL_COND_CAST(mgroup->cond.cond[mi2], atom);
                    if (ma2->pred != a2->pred)
                        continue;
                    unify_subst_t *map2 = BOR_ALLOC_ARR(unify_subst_t, map_size);
                    if (map2 != NULL)
                        memcpy(map2, map, sizeof(unify_subst_t) * map_size);
                    if (unifyAtoms(pddl, pre_param, &mgroup->param, a2, ma2, map2, 0)
                            && !preAtomsAreEqual(pre_param, a1, a2, map2)){
                        pddl_cond_t *c;
                        c = condCompileMutex(pddl, pre_param, a1, a2, map2);
                        pddlCondArrAdd(&carr, c);
                    }
                    if (map2 != NULL)
                        BOR_FREE(map2);
                }
            }
        }
    }

    for (int i = 0; i < carr.size; ++i){
        fprintf(stderr, "COND[%d]: ", i);
        pddlCondPrint(pddl, carr.cond[i], pre_param, stderr);
        fprintf(stderr, "\n");
        pddlCondDel((pddl_cond_t *)carr.cond[i]);
    }

    pddlCondArrFree(&carr);
    if (map != NULL)
        BOR_FREE(map);
}

static void actionCompileInLiftedMGroups(pddl_t *pddl,
                                         pddl_action_t *action,
                                         const pddl_lifted_mgroups_t *mgroups,
                                         bor_err_t *err)
{
    for (int mi = 0; mi < mgroups->mgroup_size; ++mi){
        const pddl_lifted_mgroup_t *mg = mgroups->mgroup + mi;
        fprintf(stderr, "lm:");
        pddlLiftedMGroupPrint(pddl, mg, stderr);
        preCompileInLiftedMGroups(pddl, &action->param, action->pre, mg, err);
    }
}

void pddlCompileInLiftedMGroups(pddl_t *pddl,
                                const pddl_lifted_mgroups_t *mgroups,
                                bor_err_t *err)
{
    for (int i = 0; i < pddl->action.action_size; ++i){
        fprintf(stderr, "a: %s\n", pddl->action.action[i].name);

        actionCompileInLiftedMGroups(pddl, pddl->action.action + i,
                                     mgroups, err);
    }
}

void pddlPrintPDDLDomain(const pddl_t *pddl, FILE *fout)
{
    fprintf(fout, "(define (domain %s)\n", pddl->domain_name);
    pddlRequirePrintPDDL(pddl->require, fout);
    pddlTypesPrintPDDL(&pddl->type, fout);
    pddlObjsPrintPDDLConstants(&pddl->obj, &pddl->type, fout);
    pddlPredsPrintPDDL(&pddl->pred, &pddl->type, fout);
    pddlFuncsPrintPDDL(&pddl->func, &pddl->type, fout);
    pddlActionsPrintPDDL(&pddl->action, pddl, fout);
    fprintf(fout, ")\n");
}

void pddlPrintPDDLProblem(const pddl_t *pddl, FILE *fout)
{
    bor_list_t *item;
    pddl_cond_t *c;
    pddl_params_t params;

    fprintf(fout, "(define (problem %s) (:domain %s)\n",
            pddl->problem_name, pddl->domain_name);

    pddlParamsInit(&params);
    fprintf(fout, "(:init\n");
    BOR_LIST_FOR_EACH(&pddl->init->part, item){
        c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        fprintf(fout, " ");
        pddlCondPrintPDDL(c, pddl, &params, fout);
    }
    fprintf(fout, ")\n");
    pddlParamsFree(&params);

    fprintf(fout, "(:goal ");
    pddlCondPrintPDDL(pddl->goal, pddl, NULL, fout);
    fprintf(fout, ")\n");

    if (pddl->metric)
        fprintf(fout, "(:metric minimize (total-cost))\n");

    fprintf(fout, ")\n");
}

static int initCondSize(const pddl_t *pddl, int type)
{
    bor_list_t *item;
    const pddl_cond_t *c;
    int size = 0;

    BOR_LIST_FOR_EACH(&pddl->init->part, item){
        c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type == type)
            ++size;
    }
    return size;
}

// TODO: Rename to pddlPrintDebug
void pddlPrintDebug(const pddl_t *pddl, FILE *fout)
{
    bor_list_t *item;
    pddl_cond_t *c;
    pddl_cond_atom_t *a;
    pddl_params_t params;

    fprintf(fout, "Domain: %s\n", pddl->domain_name);
    fprintf(fout, "Problem: %s\n", pddl->problem_name);
    fprintf(fout, "Require: %x\n", pddl->require);
    pddlTypesPrint(&pddl->type, fout);
    pddlObjsPrint(&pddl->obj, fout);
    pddlPredsPrint(&pddl->pred, "Predicate", fout);
    pddlPredsPrint(&pddl->func, "Function", fout);
    pddlActionsPrint(pddl, &pddl->action, fout);

    pddlParamsInit(&params);
    fprintf(fout, "Init[%d]:\n", initCondSize(pddl, PDDL_COND_ATOM));
    BOR_LIST_FOR_EACH(&pddl->init->part, item){
        c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type != PDDL_COND_ATOM)
            continue;
        a = PDDL_COND_CAST(c, atom);
        fprintf(fout, "  ");
        if (pddlPredIsStatic(&pddl->pred.pred[a->pred]))
            fprintf(fout, "S:");
        pddlCondPrintPDDL(c, pddl, &params, fout);
        fprintf(fout, "\n");
    }

    fprintf(fout, "Init[%d]:\n", initCondSize(pddl, PDDL_COND_ASSIGN));
    BOR_LIST_FOR_EACH(&pddl->init->part, item){
        c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type != PDDL_COND_ASSIGN)
            continue;
        fprintf(fout, "  ");
        pddlCondPrintPDDL(c, pddl, &params, fout);
        fprintf(fout, "\n");
    }
    pddlParamsFree(&params);

    fprintf(fout, "Goal: ");
    pddlCondPrint(pddl, pddl->goal, NULL, fout);
    fprintf(fout, "\n");

    fprintf(fout, "Metric: %d\n", pddl->metric);
}
