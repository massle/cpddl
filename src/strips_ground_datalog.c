/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
 * Saarland University, and
 * Czech Technical University in Prague.
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
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include <boruvka/sort.h>
#include "pddl/strips_ground_datalog.h"
#include "pddl/prep_action.h"
#include "pddl/ground_atom.h"
#include "pddl/strips_maker.h"
#include "pddl/datalog.h"
#include "assert.h"

struct action {
    int id;
    unsigned app_dlpred;
};
typedef struct action action_t;

struct ground {
    const pddl_t *pddl;
    pddl_prep_actions_t prep_action;
    pddl_strips_maker_t strips_maker;

    pddl_datalog_t *dl;
    unsigned *type_to_dlpred;
    unsigned *pred_to_dlpred;
    unsigned *obj_to_dlconst;
    action_t *action;
    unsigned *dlvar;
    int dlvar_size;
};
typedef struct ground ground_t;

static int maxVarSize(const pddl_t *pddl,
                      const pddl_prep_actions_t *prep)
{
    int max_var_size = 0;
    for (int i = 0; i < pddl->pred.pred_size; ++i)
        max_var_size = BOR_MAX(max_var_size, pddl->pred.pred[i].param_size);
    for (int i = 0; i < prep->action_size; ++i)
        max_var_size = BOR_MAX(max_var_size, prep->action[i].param_size);
    return max_var_size;
}

static void addTypeFacts(ground_t *g)
{
    for (int ti = 0; ti < g->pddl->type.type_size; ++ti){
        int size;
        const pddl_obj_id_t *objs;
        objs = pddlTypesObjsByType(&g->pddl->type, ti, &size);
        for (int i = 0; i < size; ++i){
            pddl_datalog_atom_t atom;
            pddl_datalog_rule_t rule;
            pddlDatalogRuleInit(g->dl, &rule);
            pddlDatalogAtomInit(g->dl, &atom, g->type_to_dlpred[ti]);
            pddlDatalogAtomSetArg(g->dl, &atom, 0, g->obj_to_dlconst[objs[i]]);
            pddlDatalogRuleSetHead(g->dl, &rule, &atom);
            pddlDatalogAtomFree(g->dl, &atom);
            pddlDatalogAddRule(g->dl, &rule);
            pddlDatalogRuleFree(g->dl, &rule);
        }
    }
}

static void addInitFacts(ground_t *g)
{
    const pddl_cond_atom_t *a;
    pddl_cond_const_it_atom_t it;
    PDDL_COND_FOR_EACH_ATOM(&g->pddl->init->cls, &it, a){
        pddl_datalog_atom_t atom;
        pddl_datalog_rule_t rule;
        pddlDatalogRuleInit(g->dl, &rule);
        pddlDatalogAtomInit(g->dl, &atom, g->pred_to_dlpred[a->pred]);
        for (int i = 0; i < a->arg_size; ++i){
            int obj = a->arg[i].obj;
            ASSERT(obj >= 0);
            pddlDatalogAtomSetArg(g->dl, &atom, i, g->obj_to_dlconst[obj]);
        }
        pddlDatalogRuleSetHead(g->dl, &rule, &atom);
        pddlDatalogAtomFree(g->dl, &atom);
        pddlDatalogAddRule(g->dl, &rule);
        pddlDatalogRuleFree(g->dl, &rule);
    }
}

static void addEqFacts(ground_t *g)
{
    int eqp = g->pddl->pred.eq_pred;
    for (int i = 0; i < g->pddl->obj.obj_size; ++i){
        pddl_datalog_atom_t atom;
        pddl_datalog_rule_t rule;
        pddlDatalogRuleInit(g->dl, &rule);
        pddlDatalogAtomInit(g->dl, &atom, g->pred_to_dlpred[eqp]);
        pddlDatalogAtomSetArg(g->dl, &atom, 0, g->obj_to_dlconst[i]);
        pddlDatalogAtomSetArg(g->dl, &atom, 1, g->obj_to_dlconst[i]);
        pddlDatalogRuleSetHead(g->dl, &rule, &atom);
        pddlDatalogAtomFree(g->dl, &atom);
        pddlDatalogAddRule(g->dl, &rule);
        pddlDatalogRuleFree(g->dl, &rule);
    }
}

static void atomToDLAtom(const ground_t *g,
                         const pddl_cond_atom_t *atom,
                         pddl_datalog_atom_t *dlatom,
                         bor_iset_t *used_param)
{
    pddlDatalogAtomInit(g->dl, dlatom, g->pred_to_dlpred[atom->pred]);
    for (int i = 0; i < atom->arg_size; ++i){
        if (atom->arg[i].obj >= 0){
            pddlDatalogAtomSetArg(g->dl, dlatom, i,
                                  g->obj_to_dlconst[atom->arg[i].obj]);
        }else{
            int param = atom->arg[i].param;
            pddlDatalogAtomSetArg(g->dl, dlatom, i, g->dlvar[param]);
            if (!atom->neg && used_param != NULL)
                borISetAdd(used_param, param);
        }
    }
}

static void actionToDLAtom(const ground_t *g,
                           unsigned dlpred,
                           int action_arity,
                           pddl_datalog_atom_t *dlatom)
{
    pddlDatalogAtomInit(g->dl, dlatom, dlpred);
    for (int i = 0; i < action_arity; ++i)
        pddlDatalogAtomSetArg(g->dl, dlatom, i, g->dlvar[i]);
}

static unsigned addActionRule(ground_t *g,
                              int action_id,
                              const pddl_cond_t *pre,
                              const pddl_cond_t *eff,
                              unsigned app_parent_dlpred,
                              int cei)
{
    pddl_datalog_atom_t atom;
    pddl_datalog_rule_t rule;
    const pddl_action_t *action = g->pddl->action.action + action_id;
    int action_arity = action->param.param_size;

    char name[128];
    if (cei == -1){
        snprintf(name, 128, "app-%s", action->name);
    }else{
        snprintf(name, 128, "app-%s-ce-%d", action->name, cei);
    }
    unsigned app_dlpred = pddlDatalogAddPred(g->dl, action_arity, name);
    if (cei < 0)
        pddlDatalogSetUserId(g->dl, app_dlpred, action_id);

    pddlDatalogRuleInit(g->dl, &rule);
    actionToDLAtom(g, app_dlpred, action_arity, &atom);
    pddlDatalogRuleSetHead(g->dl, &rule, &atom);
    pddlDatalogAtomFree(g->dl, &atom);

    if (cei >= 0){
        actionToDLAtom(g, app_parent_dlpred, action_arity, &atom);
        pddlDatalogRuleAddBody(g->dl, &rule, &atom);
        pddlDatalogAtomFree(g->dl, &atom);
    }

    BOR_ISET(used_param);
    const pddl_cond_atom_t *catom;
    pddl_cond_const_it_atom_t it;
    PDDL_COND_FOR_EACH_ATOM(pre, &it, catom){
        atomToDLAtom(g, catom, &atom, &used_param);
        if (catom->neg){
            pddlDatalogRuleAddNegStaticBody(g->dl, &rule, &atom);
        }else{
            pddlDatalogRuleAddBody(g->dl, &rule, &atom);
        }
        pddlDatalogAtomFree(g->dl, &atom);
    }
    if (cei < 0){
        for (int i = 0; i < action->param.param_size; ++i){
            int type = action->param.param[i].type;
            if (type != 0 || !borISetIn(i, &used_param)){
                pddlDatalogAtomInit(g->dl, &atom, g->type_to_dlpred[type]);
                pddlDatalogAtomSetArg(g->dl, &atom, 0, g->dlvar[i]);
                pddlDatalogRuleAddBody(g->dl, &rule, &atom);
                pddlDatalogAtomFree(g->dl, &atom);
            }
        }
    }
    borISetFree(&used_param);

    pddlDatalogAddRule(g->dl, &rule);
    pddlDatalogRuleFree(g->dl, &rule);


    // add-effect :- app-action
    PDDL_COND_FOR_EACH_ATOM(eff, &it, catom){
        if (catom->neg)
            continue;

        pddlDatalogRuleInit(g->dl, &rule);
        atomToDLAtom(g, catom, &atom, NULL);
        pddlDatalogRuleSetHead(g->dl, &rule, &atom);
        pddlDatalogAtomFree(g->dl, &atom);

        actionToDLAtom(g, app_dlpred, action_arity, &atom);
        pddlDatalogRuleAddBody(g->dl, &rule, &atom);
        pddlDatalogAtomFree(g->dl, &atom);

        pddlDatalogAddRule(g->dl, &rule);
        pddlDatalogRuleFree(g->dl, &rule);
    }

    return app_dlpred;
}

static void addActionRules(ground_t *g, int action_id)
{
    const pddl_action_t *action = g->pddl->action.action + action_id;

    action_t *a = g->action + action_id;
    a->id = action_id;
    a->app_dlpred = addActionRule(g, action_id, action->pre, action->eff, 0, -1);

    // Conditional effects
    pddl_cond_const_it_when_t wit;
    const pddl_cond_when_t *when;
    int wi = 0;
    PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when){
        addActionRule(g, action_id, when->pre, when->eff, a->app_dlpred, wi);
        ++wi;
    }
}

static void addActionsRules(ground_t *g)
{
    for (int i = 0; i < g->pddl->action.action_size; ++i)
        addActionRules(g, i);
}

static int groundInit(ground_t *g,
                      const pddl_t *pddl,
                      const pddl_ground_config_t *cfg,
                      bor_err_t *err)
{
    bzero(g, sizeof(*g));
    g->pddl = pddl;
    pddlPrepActionsInit(g->pddl, &g->prep_action, err);

    pddlStripsMakerInit(&g->strips_maker, g->pddl);

    g->dl = pddlDatalogNew();
    g->type_to_dlpred = BOR_ALLOC_ARR(unsigned, g->pddl->type.type_size);
    g->pred_to_dlpred = BOR_ALLOC_ARR(unsigned, g->pddl->pred.pred_size);
    g->obj_to_dlconst = BOR_ALLOC_ARR(unsigned, g->pddl->obj.obj_size);
    g->action = BOR_CALLOC_ARR(action_t, g->pddl->action.action_size);

    g->dlvar_size = maxVarSize(pddl, &g->prep_action);
    g->dlvar = BOR_ALLOC_ARR(unsigned, g->dlvar_size);
    for (int i = 0; i < g->dlvar_size; ++i)
        g->dlvar[i] = pddlDatalogAddVar(g->dl, NULL);

    for (int i = 0; i < g->pddl->type.type_size; ++i){
        const pddl_type_t *type = g->pddl->type.type + i;
        g->type_to_dlpred[i] = pddlDatalogAddPred(g->dl, 1, type->name);
    }

    for (int i = 0; i < g->pddl->pred.pred_size; ++i){
        const pddl_pred_t *pred = g->pddl->pred.pred + i;
        g->pred_to_dlpred[i]
                = pddlDatalogAddPred(g->dl, pred->param_size, pred->name);
        pddlDatalogSetUserId(g->dl, g->pred_to_dlpred[i], i);
    }

    for (int i = 0; i < g->pddl->obj.obj_size; ++i){
        const pddl_obj_t *obj = g->pddl->obj.obj + i;
        g->obj_to_dlconst[i] = pddlDatalogAddConst(g->dl, obj->name);
        pddlDatalogSetUserId(g->dl, g->obj_to_dlconst[i], i);
    }

    addTypeFacts(g);
    addInitFacts(g);
    addEqFacts(g);
    addActionsRules(g);

    return 0;
}

static void groundFree(ground_t *g)
{
    pddlPrepActionsFree(&g->prep_action);
    pddlStripsMakerFree(&g->strips_maker);
    pddlDatalogDel(g->dl);
    BOR_FREE(g->type_to_dlpred);
    BOR_FREE(g->pred_to_dlpred);
    BOR_FREE(g->obj_to_dlconst);
    BOR_FREE(g->action);
    BOR_FREE(g->dlvar);
}

static void insertAtom(int pred, int arity, const pddl_obj_id_t *arg, void *ud)
{
    ground_t *g = ud;

    if (pddlPredIsStatic(&g->pddl->pred.pred[pred])){
        pddlStripsMakerAddStaticAtomPred(&g->strips_maker,
                                         pred, arg, arity, NULL);
    }else{
        pddlStripsMakerAddAtomPred(&g->strips_maker, pred, arg, arity, NULL);
    }
}

static void insertAction(int pred,
                         int arity,
                         const pddl_obj_id_t *arg,
                         void *ud)
{
    ground_t *g = ud;
    pddlStripsMakerAddAction(&g->strips_maker, pred, 0, arg, NULL);
}

int pddlStripsGroundDatalog(pddl_strips_t *strips,
                            const pddl_t *pddl,
                            const pddl_ground_config_t *cfg,
                            bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Ground DL: ");
    BOR_INFO2(err, "Grounding using datalog ...");


    ground_t ground;
    groundInit(&ground, pddl, cfg, err);
    pddlDatalogToNormalForm(ground.dl, err);
    //pddlDatalogPrint(ground.dl, stderr);
    pddlDatalogCanonicalModel(ground.dl, err);
    pddlStripsMakerAddInit(&ground.strips_maker, ground.pddl);
    for (int p = 0; p < ground.pddl->pred.pred_size; ++p){
        if (p == ground.pddl->pred.eq_pred)
            continue;
        pddlDatalogFactsFromCanonicalModel(ground.dl,
                                           ground.pred_to_dlpred[p],
                                           insertAtom,
                                           &ground);
    }
    for (int a = 0; a < ground.pddl->action.action_size; ++a){
        pddlDatalogFactsFromCanonicalModel(ground.dl,
                                           ground.action[a].app_dlpred,
                                           insertAction,
                                           &ground);
    }

    BOR_INFO(err, "Grounding finished: %d actions, %d facts,"
                  " %d static facts, %d functions",
             ground.strips_maker.num_action_args,
             ground.strips_maker.ground_atom.atom_size,
             ground.strips_maker.ground_atom_static.atom_size,
             ground.strips_maker.ground_func.atom_size);

    int ret = pddlStripsMakerMakeStrips(&ground.strips_maker, ground.pddl, cfg,
                                        strips, err);

    groundFree(&ground);
    if (ret != 0){
        BOR_INFO_PREFIX_POP(err);
        BOR_TRACE_RET(err, ret);
    }

    BOR_INFO(err, "Number of Strips Operators: %d", strips->op.op_size);
    BOR_INFO(err, "Number of Strips Facts: %d", strips->fact.fact_size);
    BOR_INFO(err, "Goal is unreachable: %d", strips->goal_is_unreachable);
    BOR_INFO(err, "Has Conditional Effects: %d", strips->has_cond_eff);
    int count = 0;
    for (int i = 0; i < strips->op.op_size; ++i){
        if (strips->op.op[i]->cond_eff_size > 0)
            ++count;
    }
    BOR_INFO(err, "Number of Strips Operators with Conditional Effects: %d",
             count);

    BOR_INFO2(err, "Grounding finished.");
    BOR_INFO_PREFIX_POP(err);
    return 0;
}
