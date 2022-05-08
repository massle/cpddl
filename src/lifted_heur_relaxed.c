/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/lifted_heur_relaxed.h"
#include "datalog_pddl.h"
#include "internal.h"



static void actionToDLAtom(const pddl_lifted_heur_relaxed_t *h,
                           unsigned dlpred,
                           int action_arity,
                           pddl_datalog_atom_t *dlatom)
{
    pddlDatalogAtomInit(h->dl, dlatom, dlpred);
    for (int i = 0; i < action_arity; ++i)
        pddlDatalogAtomSetArg(h->dl, dlatom, i, h->dlvar[i]);
}

static unsigned addActionRule(pddl_lifted_heur_relaxed_t *h,
                              int action_id,
                              const pddl_cond_t *pre,
                              const pddl_cond_t *eff,
                              unsigned app_parent_dlpred,
                              int cei)
{
    pddl_datalog_atom_t atom;
    pddl_datalog_rule_t rule;
    const pddl_action_t *action = h->pddl->action.action + action_id;
    int action_arity = action->param.param_size;

    char name[128];
    if (cei == -1){
        snprintf(name, 128, "app-%s", action->name);
    }else{
        snprintf(name, 128, "app-%s-ce-%d", action->name, cei);
    }
    unsigned app_dlpred = pddlDatalogAddPred(h->dl, action_arity, name);
    if (cei < 0)
        pddlDatalogSetUserId(h->dl, app_dlpred, action_id);

    pddlDatalogRuleInit(h->dl, &rule);
    actionToDLAtom(h, app_dlpred, action_arity, &atom);
    pddlDatalogRuleSetHead(h->dl, &rule, &atom);
    pddlDatalogAtomFree(h->dl, &atom);

    if (cei >= 0){
        actionToDLAtom(h, app_parent_dlpred, action_arity, &atom);
        pddlDatalogRuleAddBody(h->dl, &rule, &atom);
        pddlDatalogAtomFree(h->dl, &atom);
    }

    const pddl_cond_atom_t *catom;
    pddl_cond_const_it_atom_t it;
    PDDL_COND_FOR_EACH_ATOM(pre, &it, catom){
        pddlDatalogPddlAtomToDLAtom(h->dl, &atom, catom, h->pred_to_dlpred,
                                    h->obj_to_dlconst, h->dlvar);
        if (catom->neg){
            pddlDatalogRuleAddNegStaticBody(h->dl, &rule, &atom);
        }else{
            pddlDatalogRuleAddBody(h->dl, &rule, &atom);
        }
        pddlDatalogAtomFree(h->dl, &atom);
    }
    if (cei < 0){
        pddlDatalogPddlSetActionTypeBody(h->dl, &rule, h->pddl, &action->param,
                                         pre, h->type_to_dlpred, h->dlvar);
    }

    pddlDatalogAddRule(h->dl, &rule);
    pddlDatalogRuleFree(h->dl, &rule);


    // add-effect :- app-action
    PDDL_COND_FOR_EACH_ATOM(eff, &it, catom){
        if (catom->neg)
            continue;

        pddlDatalogRuleInit(h->dl, &rule);
        pddlDatalogPddlAtomToDLAtom(h->dl, &atom, catom, h->pred_to_dlpred,
                                    h->obj_to_dlconst, h->dlvar);
        pddlDatalogRuleSetHead(h->dl, &rule, &atom);
        pddlDatalogAtomFree(h->dl, &atom);

        actionToDLAtom(h, app_dlpred, action_arity, &atom);
        pddlDatalogRuleAddBody(h->dl, &rule, &atom);
        pddlDatalogAtomFree(h->dl, &atom);

        // TODO: Set costs
        if (!h->pddl->metric){
            pddl_cost_t w;
            pddlCostSetOp(&w, 1);
            pddlDatalogRuleSetWeight(h->dl, &rule, &w);
        }
        pddlDatalogAddRule(h->dl, &rule);
        pddlDatalogRuleFree(h->dl, &rule);
    }

    return app_dlpred;
}

static void addActionRules(pddl_lifted_heur_relaxed_t *h, int action_id)
{
    const pddl_action_t *action = h->pddl->action.action + action_id;

    unsigned app_dlpred = addActionRule(h, action_id, action->pre, action->eff, 0, -1);

    // Conditional effects
    pddl_cond_const_it_when_t wit;
    const pddl_cond_when_t *when;
    int wi = 0;
    PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when){
        addActionRule(h, action_id, when->pre, when->eff, app_dlpred, wi);
        ++wi;
    }
}

static void addActionsRules(pddl_lifted_heur_relaxed_t *h)
{
    for (int i = 0; i < h->pddl->action.action_size; ++i)
        addActionRules(h, i);
}

static void addGoal(pddl_lifted_heur_relaxed_t *h)
{
    pddl_datalog_rule_t rule;
    pddlDatalogRuleInit(h->dl, &rule);

    h->goal_dlpred = pddlDatalogAddGoalPred(h->dl, "GOAL");
    pddl_datalog_atom_t atom;
    pddlDatalogAtomInit(h->dl, &atom, h->goal_dlpred);
    pddlDatalogRuleSetHead(h->dl, &rule, &atom);
    pddlDatalogAtomFree(h->dl, &atom);

    const pddl_cond_atom_t *a;
    pddl_cond_const_it_atom_t it;
    PDDL_COND_FOR_EACH_ATOM(h->pddl->goal, &it, a){
        pddl_datalog_atom_t atom;
        pddlDatalogAtomInit(h->dl, &atom, h->pred_to_dlpred[a->pred]);
        for (int i = 0; i < a->arg_size; ++i){
            int obj = a->arg[i].obj;
            ASSERT(obj >= 0);
            pddlDatalogAtomSetArg(h->dl, &atom, i, h->obj_to_dlconst[obj]);
        }
        pddlDatalogRuleAddBody(h->dl, &rule, &atom);
        pddlDatalogAtomFree(h->dl, &atom);
    }
    pddlDatalogAddRule(h->dl, &rule);
    pddlDatalogRuleFree(h->dl, &rule);
}

static int addFacts(pddl_lifted_heur_relaxed_t *h,
                    const pddl_iset_t *facts,
                    const pddl_ground_atoms_t *gatoms)
{
    int num_rules = 0;
    int fact;
    PDDL_ISET_FOR_EACH(facts, fact){
        const pddl_ground_atom_t *ga = gatoms->atom[fact];
        if (pddlPredIsStatic(h->pddl->pred.pred + ga->pred))
            continue;

        pddl_datalog_rule_t rule;
        pddlDatalogRuleInit(h->dl, &rule);

        pddl_datalog_atom_t atom;
        pddlDatalogAtomInit(h->dl, &atom, h->pred_to_dlpred[ga->pred]);
        for (int i = 0; i < ga->arg_size; ++i){
            int obj = ga->arg[i];
            ASSERT(obj >= 0);
            pddlDatalogAtomSetArg(h->dl, &atom, i, h->obj_to_dlconst[obj]);
        }
        pddlDatalogRuleSetHead(h->dl, &rule, &atom);
        pddlDatalogAtomFree(h->dl, &atom);
        pddlDatalogAddRule(h->dl, &rule);
        pddlDatalogRuleFree(h->dl, &rule);
        ++num_rules;
    }

    return num_rules;
}

static void addInitStaticFacts(pddl_lifted_heur_relaxed_t *h)
{
    const pddl_cond_atom_t *a;
    pddl_cond_const_it_atom_t it;
    PDDL_COND_FOR_EACH_ATOM(&h->pddl->init->cls, &it, a){
        if (!pddlPredIsStatic(h->pddl->pred.pred + a->pred))
            continue;

        pddl_datalog_atom_t atom;
        pddl_datalog_rule_t rule;
        pddlDatalogRuleInit(h->dl, &rule);
        pddlDatalogAtomInit(h->dl, &atom, h->pred_to_dlpred[a->pred]);
        for (int i = 0; i < a->arg_size; ++i){
            int obj = a->arg[i].obj;
            ASSERT(obj >= 0);
            pddlDatalogAtomSetArg(h->dl, &atom, i, h->obj_to_dlconst[obj]);
        }
        pddlDatalogRuleSetHead(h->dl, &rule, &atom);
        pddlDatalogAtomFree(h->dl, &atom);
        pddlDatalogAddRule(h->dl, &rule);
        pddlDatalogRuleFree(h->dl, &rule);
    }
}

static void pddlLiftedHeurRelaxedInit(pddl_lifted_heur_relaxed_t *h,
                                      const pddl_t *pddl,
                                      pddl_err_t *err)
{
    CTX(err, "lifted_relax_heur", "lifted-relax-heur");
    bzero(h, sizeof(*h));
    h->pddl = pddl;
    pddlPrepActionsInit(h->pddl, &h->prep_action, err);
    h->dl = pddlDatalogNew();
    h->type_to_dlpred = ALLOC_ARR(unsigned, h->pddl->type.type_size);
    h->pred_to_dlpred = ALLOC_ARR(unsigned, h->pddl->pred.pred_size);
    h->obj_to_dlconst = ALLOC_ARR(unsigned, h->pddl->obj.obj_size);

    h->dlvar_size = pddlDatalogPddlMaxVarSize(pddl, &h->prep_action);
    h->dlvar = ALLOC_ARR(unsigned, h->dlvar_size);
    for (int i = 0; i < h->dlvar_size; ++i)
        h->dlvar[i] = pddlDatalogAddVar(h->dl, NULL);

    for (int i = 0; i < h->pddl->type.type_size; ++i){
        const pddl_type_t *type = h->pddl->type.type + i;
        h->type_to_dlpred[i] = pddlDatalogAddPred(h->dl, 1, type->name);
    }

    for (int i = 0; i < h->pddl->pred.pred_size; ++i){
        const pddl_pred_t *pred = h->pddl->pred.pred + i;
        h->pred_to_dlpred[i]
            = pddlDatalogAddPred(h->dl, pred->param_size, pred->name);
        pddlDatalogSetUserId(h->dl, h->pred_to_dlpred[i], i);
    }

    for (int i = 0; i < h->pddl->obj.obj_size; ++i){
        const pddl_obj_t *obj = h->pddl->obj.obj + i;
        h->obj_to_dlconst[i] = pddlDatalogAddConst(h->dl, obj->name);
        pddlDatalogSetUserId(h->dl, h->obj_to_dlconst[i], i);
    }

    pddlDatalogPddlAddEqRules(h->dl, h->pddl, h->pred_to_dlpred,
                              h->obj_to_dlconst);
    addInitStaticFacts(h);
    addActionsRules(h);
    pddlDatalogPddlAddTypeRules(h->dl, h->pddl, h->type_to_dlpred,
                                h->obj_to_dlconst);
    addGoal(h);

    pddlDatalogToNormalForm(h->dl, err);
    CTXEND(err);
}

static void pddlLiftedHeurRelaxedFree(pddl_lifted_heur_relaxed_t *h)
{
    pddlPrepActionsFree(&h->prep_action);
    pddlDatalogDel(h->dl);
    FREE(h->type_to_dlpred);
    FREE(h->pred_to_dlpred);
    FREE(h->obj_to_dlconst);
    FREE(h->dlvar);
    // TODO
}

pddl_cost_t pddlLiftedHeurRelaxed(pddl_lifted_hmax_t *h,
                                  const pddl_iset_t *state,
                                  const pddl_ground_atoms_t *gatoms,
                                  int (*eval)(pddl_datalog_t *,
                                              pddl_cost_t *,
                                              pddl_err_t *))
{
    pddlDatalogClear(h->dl);
    int new_rules = addFacts(h, state, gatoms);

    pddl_cost_t w = pddl_cost_zero;
    if (eval(h->dl, &w, NULL) != 0)
        w = pddl_cost_dead_end;

    pddlDatalogRmLastRules(h->dl, new_rules);
    return w;
}

void pddlLiftedHMaxInit(pddl_lifted_hmax_t *h,
                        const pddl_t *pddl,
                        pddl_err_t *err)
{
    pddlLiftedHeurRelaxedInit(h, pddl, err);
}

void pddlLiftedHMaxFree(pddl_lifted_hmax_t *h)
{
    pddlLiftedHeurRelaxedFree(h);
}

pddl_cost_t pddlLiftedHMax(pddl_lifted_hmax_t *h,
                           const pddl_iset_t *state,
                           const pddl_ground_atoms_t *gatoms)
{
    return pddlLiftedHeurRelaxed(h, state, gatoms,
                                 pddlDatalogWeightedCanonicalModelMax);
}

void pddlLiftedHAddInit(pddl_lifted_hadd_t *h,
                        const pddl_t *pddl,
                        pddl_err_t *err)
{
    pddlLiftedHeurRelaxedInit(h, pddl, err);
}

void pddlLiftedHAddFree(pddl_lifted_hadd_t *h)
{
    pddlLiftedHeurRelaxedFree(h);
}

pddl_cost_t pddlLiftedHAdd(pddl_lifted_hadd_t *h,
                           const pddl_iset_t *state,
                           const pddl_ground_atoms_t *gatoms)
{
    return pddlLiftedHeurRelaxed(h, state, gatoms,
                                 pddlDatalogWeightedCanonicalModelAdd);
}
