/***
 * cpddl
 * -------
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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


#include "pddl/pddl_struct.h"
#include "pddl/unify.h"
#include "internal.h"


struct action_cond {
    const pddl_cond_t *pre;
    pddl_cond_arr_t cond;
};
typedef struct action_cond action_cond_t;

struct action_conds {
    action_cond_t *cond;
    int cond_size;
    int cond_alloc;
};
typedef struct action_conds action_conds_t;

static void actionCondsInit(action_conds_t *acs)
{
    ZEROIZE(acs);
}

static void actionCondsFree(action_conds_t *acs)
{
    for (int i = 0; i < acs->cond_size; ++i){
        for (int ci = 0; ci < acs->cond[i].cond.size; ++ci)
            pddlCondDel((pddl_cond_t *)acs->cond[i].cond.cond[ci]);
        pddlCondArrFree(&acs->cond[i].cond);
    }
    if (acs->cond != NULL)
        FREE(acs->cond);
}

static pddl_cond_arr_t *actionConds(action_conds_t *acs,
                                    const pddl_cond_t *pre)
{
    for (int i = 0; i < acs->cond_size; ++i){
        if (acs->cond[i].pre == pre)
            return &acs->cond[i].cond;
    }

    if (acs->cond_size == acs->cond_alloc){
        if (acs->cond_alloc == 0)
            acs->cond_alloc = 2;
        acs->cond_alloc *= 2;
        acs->cond = REALLOC_ARR(acs->cond, action_cond_t, acs->cond_alloc);
    }
    acs->cond[acs->cond_size].pre = pre;
    pddlCondArrInit(&acs->cond[acs->cond_size].cond);
    ++acs->cond_size;
    return &acs->cond[acs->cond_size - 1].cond;
}

static pddl_cond_t *actionCondsMerge(const action_conds_t *acs,
                                     const pddl_cond_t *pre,
                                     const pddl_t *pddl,
                                     const pddl_params_t *param)
{
    const pddl_cond_arr_t *conds = NULL;
    for (int i = 0; i < acs->cond_size; ++i){
        if (acs->cond[i].pre == pre)
            conds = &acs->cond[i].cond;
    }

    if (conds == NULL)
        return NULL;

    pddl_cond_t *out = pddlCondNewEmptyAnd();
    for (int i = 0; i < conds->size; ++i){
        pddl_cond_t *c = pddlCondNegate(conds->cond[i], pddl);
        c = pddlCondSimplify(c, pddl, param);
        c = pddlCondNormalize(c, pddl, param);
        c = pddlCondSimplify(c, pddl, param);
        pddlCondPartAdd(PDDL_COND_CAST(out, part), c);
        out = pddlCondSimplify(out, pddl, param);
        out = pddlCondNormalize(out, pddl, param);
        out = pddlCondSimplify(out, pddl, param);
        if (out->type != PDDL_COND_AND){
            pddl_cond_t *n = pddlCondNewEmptyAnd();
            pddlCondPartAdd(PDDL_COND_CAST(n, part), out);
            out = n;
        }
    }
    out = pddlCondSimplify(out, pddl, param);
    out = pddlCondNormalize(out, pddl, param);
    out = pddlCondSimplify(out, pddl, param);
    return out;
}

static int checkInequality(const pddl_unify_t *unify,
                           const pddl_t *pddl,
                           const pddl_action_t *action,
                           const pddl_cond_t *pre)
{
    int eq_pred = pddl->pred.eq_pred;
    const pddl_params_t *param = &action->param;

    return pddlUnifyCheckInequality(unify, param, eq_pred, action->pre)
            && (pre == NULL
                    || pddlUnifyCheckInequality(unify, param, eq_pred, pre));
}

static pddl_cond_t *condAtomsNotEqual(const pddl_t *pddl,
                                      const pddl_params_t *param,
                                      const pddl_cond_atom_t *a1,
                                      const pddl_cond_atom_t *a2,
                                      const pddl_cond_t *unifier_cond)
{
    if (a1->pred != a2->pred){
        return &pddlCondNewBool(1)->cls;
    }else if (a1->arg_size == 0){
        return &pddlCondNewBool(0)->cls;
    }

    pddl_cond_t *or = pddlCondNewEmptyOr();
    for (int i = 0; i < a1->arg_size; ++i){
        pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
        eq->pred = pddl->pred.eq_pred;
        if (a1->arg[i].param >= 0 && a2->arg[i].param >= 0){
            int type1 = param->param[a1->arg[i].param].type;
            int type2 = param->param[a2->arg[i].param].type;
            if (pddlTypesAreDisjunct(&pddl->type, type1, type2)){
                pddlCondDel(or);
                return &pddlCondNewBool(1)->cls;
            }

            if (a1->arg[i].param < a2->arg[i].param){
                eq->arg[0] = a1->arg[i];
                eq->arg[1] = a2->arg[i];
            }else{
                eq->arg[0] = a2->arg[i];
                eq->arg[1] = a1->arg[i];
            }

        }else if (a1->arg[i].param >= 0){
            int type = param->param[a1->arg[i].param].type;
            if (!pddlTypesObjHasType(&pddl->type, type, a2->arg[i].obj)){
                pddlCondDel(or);
                return &pddlCondNewBool(1)->cls;
            }
            eq->arg[0] = a1->arg[i];
            eq->arg[1] = a2->arg[i];

        }else if (a2->arg[i].param >= 0){
            int type = param->param[a2->arg[i].param].type;
            if (!pddlTypesObjHasType(&pddl->type, type, a1->arg[i].obj)){
                pddlCondDel(or);
                return &pddlCondNewBool(1)->cls;
            }
            eq->arg[0] = a2->arg[i];
            eq->arg[1] = a1->arg[i];

        }else{
            if (a1->arg[i].obj != a2->arg[i].obj){
                pddlCondDel(or);
                return &pddlCondNewBool(1)->cls;
            }
        }

        pddl_cond_const_it_atom_t it;
        const pddl_cond_atom_t *a;
        int unsat = 0;
        PDDL_COND_FOR_EACH_ATOM(unifier_cond, &it, a){
            if (pddlCondEq(&a->cls, &eq->cls)){
                unsat = 1;
                break;
            }
        }
        if (unsat)
            continue;

        eq->neg = 1;
        pddlCondPartAdd(PDDL_COND_CAST(or, part), &eq->cls);
    }

    if (pddlCondPartIsEmpty(PDDL_COND_CAST(or, part))){
        pddlCondDel(or);
        return &pddlCondNewBool(0)->cls;
    }
    return or;
}

static void mutexUnify2(const pddl_t *pddl,
                        const pddl_action_t *action,
                        const pddl_lifted_mgroup_t *mgroup,
                        const pddl_cond_t *pre,
                        const pddl_cond_t *pre2,
                        const pddl_cond_atom_t *pre_atom1,
                        const pddl_cond_atom_t *mg_atom1,
                        const pddl_unify_t *unify1,
                        const pddl_cond_atom_t *pre_atom2,
                        const pddl_cond_atom_t *mg_atom2,
                        action_conds_t *acs,
                        pddl_err_t *err)
{
    const pddl_params_t *pre_param = &action->param;
    int eq_pred = pddl->pred.eq_pred;
    pddl_unify_t unify;
    pddlUnifyInitCopy(&unify, unify1);
    if (pddlUnify(&unify, pre_atom2, mg_atom2) == 0
            && checkInequality(&unify, pddl, action, pre)
            && pddlUnifyAtomsDiffer(&unify, pre_param, pre_atom1,
                                            pre_param, pre_atom2)){

        pddl_cond_t *unifier_c = pddlUnifyToCond(&unify, eq_pred, pre_param);
        pddl_cond_t *ineq_c = condAtomsNotEqual(pddl, pre_param, pre_atom1,
                                                pre_atom2, unifier_c);
        pddl_cond_t *action_c = pddlCondNewAnd2(unifier_c, ineq_c);
        action_c = pddlCondSimplify(action_c, pddl, pre_param);

        pddl_cond_arr_t *action_cond = actionConds(acs, pre);
        for (int i = 0; i < action_cond->size; ++i){
            if (pddlCondEq(action_c, action_cond->cond[i])){
                pddlCondDel(action_c);
                action_c = NULL;
            }
        }
        if (action_c != NULL){
            LOG(err, "Found mutex condition for action '%{mutex.action}s'"
                " and mgroup %{mutex.mgroup}s: %{mutex.cond}s",
                action->name,
                F_LIFTED_MGROUP(pddl, mgroup),
                F_COND_PDDL(action_c, pddl, pre_param));
            pddlCondArrAdd(action_cond, action_c);
        }
    }
    pddlUnifyFree(&unify);
}

static void mutexUnify1(const pddl_t *pddl,
                        const pddl_action_t *action,
                        const pddl_lifted_mgroup_t *mgroup,
                        const pddl_cond_t *pre,
                        const pddl_cond_t *pre2,
                        pddl_cond_const_it_atom_t it,
                        const pddl_cond_atom_t *pre_atom1,
                        const pddl_cond_atom_t *mg_atom1,
                        action_conds_t *acs,
                        pddl_err_t *err)

{
    int eq_pred = pddl->pred.eq_pred;
    pddl_unify_t unify;
    pddlUnifyInit(&unify, &pddl->type, &action->param, &mgroup->param);
    pddlUnifyApplyEquality(&unify, &action->param, eq_pred, pre);
    if (pddlUnify(&unify, pre_atom1, mg_atom1) == 0
            && checkInequality(&unify, pddl, action, pre)){
        const pddl_cond_atom_t *pre_atom2, *mg_atom2;
        PDDL_COND_FOR_EACH_ATOM_CONT(&it, pre_atom2){
            if (pre_atom2->neg)
                continue;
            PDDL_COND_ARR_FOR_EACH_ATOM(&mgroup->cond, mg_atom2){
                if (mg_atom2 == mg_atom1 || pre_atom2 == pre_atom1)
                    continue;
                if (pre_atom2->pred != mg_atom2->pred)
                    continue;
                mutexUnify2(pddl, action, mgroup, pre, pre2,
                            pre_atom1, mg_atom1, &unify,
                            pre_atom2, mg_atom2, acs, err);
            }
        }
        if (pre2 != NULL){
            pddl_cond_const_it_atom_t it;
            const pddl_cond_atom_t *pre_atom2, *mg_atom2;
            PDDL_COND_FOR_EACH_ATOM(pre2, &it, pre_atom2){
                if (pre_atom2->neg)
                    continue;
                PDDL_COND_ARR_FOR_EACH_ATOM(&mgroup->cond, mg_atom2){
                    if (mg_atom2 == mg_atom1 || pre_atom2 == pre_atom1)
                        continue;
                    if (pre_atom2->pred != mg_atom2->pred)
                        continue;
                    mutexUnify2(pddl, action, mgroup, pre, pre2,
                                pre_atom1, mg_atom1, &unify,
                                pre_atom2, mg_atom2, acs, err);
                }
            }
        }
    }
    pddlUnifyFree(&unify);
}

static void mutex(const pddl_t *pddl,
                  const pddl_action_t *action,
                  const pddl_lifted_mgroup_t *mgroup,
                  const pddl_cond_t *pre,
                  const pddl_cond_t *pre2,
                  action_conds_t *acs,
                  pddl_err_t *err)
{
    pddl_cond_const_it_atom_t it;
    const pddl_cond_atom_t *pre_atom1, *mg_atom1;
    PDDL_COND_FOR_EACH_ATOM(pre, &it, pre_atom1){
        if (pre_atom1->neg)
            continue;
        PDDL_COND_ARR_FOR_EACH_ATOM(&mgroup->cond, mg_atom1){
            if (pre_atom1->pred != mg_atom1->pred)
                continue;
            mutexUnify1(pddl, action, mgroup, pre, pre2,
                        it, pre_atom1, mg_atom1, acs, err);
        }
    }
}


static void compileInMutex(const pddl_t *pddl,
                           const pddl_action_t *action,
                           const pddl_lifted_mgroup_t *mgroup_in,
                           action_conds_t *acs,
                           pddl_err_t *err)
{
    pddl_lifted_mgroup_t mgroup;
    pddlLiftedMGroupInitCopy(&mgroup, mgroup_in);
    pddlLiftedMGroupDoubleCounted(&mgroup);

    mutex(pddl, action, &mgroup, action->pre, NULL, acs, err);

    pddl_cond_const_it_when_t wit;
    const pddl_cond_when_t *when;
    PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when){
        mutex(pddl, action, &mgroup, when->pre, action->pre, acs, err);
    }


    pddlLiftedMGroupFree(&mgroup);
}


static int deadEndCollectNegConds(const pddl_t *pddl,
                                  const pddl_action_t *action,
                                  const pddl_lifted_mgroup_t *mgroup,
                                  const pddl_cond_t *pre,
                                  const pddl_cond_t *eff,
                                  const pddl_unify_t *unify_goal_del_pre,
                                  const pddl_cond_atom_t *pre_atom,
                                  const pddl_cond_atom_t *del_atom,
                                  const pddl_cond_atom_t *mg_del_atom,
                                  const pddl_cond_t *cond_del,
                                  pddl_cond_part_t *cond,
                                  pddl_err_t *err)
{
    int eq_pred = pddl->pred.eq_pred;
    pddl_cond_const_it_t itadd;
    const pddl_cond_atom_t *add;
    PDDL_COND_FOR_EACH_ATOM(eff, &itadd, add){
        if (add->neg)
            continue;
        const pddl_cond_atom_t *mg_atom;
        PDDL_COND_ARR_FOR_EACH_ATOM(&mgroup->cond, mg_atom){
            if (add->pred != mg_atom->pred)
                continue;
            if (mg_atom == mg_del_atom)
                continue;
            pddl_unify_t unify;
            pddlUnifyInitCopy(&unify, unify_goal_del_pre);
            if (pddlUnify(&unify, add, mg_atom) == 0
                    && checkInequality(&unify, pddl, action, pre)){
                if (pddlUnifyEq(&unify, unify_goal_del_pre)){
                    return -1;

                }else{
                    pddl_cond_t *c;
                    c = pddlUnifyToCond(&unify, eq_pred, &action->param);
                    c = pddlCondSimplify(c, pddl, &action->param);
                    if (pddlCondIsEntailed(c, cond_del, pddl, &action->param)){
                        pddlCondDel(c);
                        return -1;
                    }

                    c = pddlCondNegate(c, pddl);
                    c = pddlCondSimplify(c, pddl, &action->param);
                    pddlCondPartAdd(cond, c);
                }
            }
            pddlUnifyFree(&unify);
        }
    }

    return 0;
}

static void deadEndAdd(const pddl_t *pddl,
                       const pddl_action_t *action,
                       const pddl_lifted_mgroup_t *mgroup,
                       const pddl_cond_t *pre,
                       const pddl_cond_t *eff,
                       const pddl_unify_t *unify_goal_del_pre,
                       const pddl_cond_atom_t *del_atom,
                       const pddl_cond_atom_t *pre_atom,
                       const pddl_cond_atom_t *mg_del_atom,
                       action_conds_t *acs,
                       pddl_err_t *err)
{
    int eq_pred = pddl->pred.eq_pred;
    pddl_cond_t *cond_del, *cond;

    cond_del = pddlUnifyToCond(unify_goal_del_pre, eq_pred, &action->param);
    cond_del = pddlCondSimplify(cond_del, pddl, &action->param);
    cond = pddlCondNewEmptyAnd();

    if (deadEndCollectNegConds(pddl, action, mgroup, pre, eff,
                               unify_goal_del_pre, pre_atom, del_atom, mg_del_atom,
                               cond_del, PDDL_COND_CAST(cond, part), err) == 0){
        pddlCondPartAdd(PDDL_COND_CAST(cond, part), cond_del);
        cond = pddlCondSimplify(cond, pddl, &action->param);
        pddl_cond_arr_t *action_cond = actionConds(acs, pre);
        for (int i = 0; i < action_cond->size; ++i){
            if (pddlCondEq(cond, action_cond->cond[i])){
                pddlCondDel(cond);
                cond = NULL;
            }
        }
        if (cond != NULL){
            LOG(err, "Found dead-end condition for action '%{dead_end.action}s'"
                " and mgroup %{dead_end.mgroup}s: %{dead_end.cond}s",
                action->name,
                F_LIFTED_MGROUP(pddl, mgroup),
                F_COND_PDDL_BUFSIZE(cond, pddl, &action->param, 10000));
            pddlCondArrAdd(action_cond, cond);
        }
    }else{
        pddlCondDel(cond_del);
        pddlCondDel(cond);
    }
}

static void deadEndPre(const pddl_t *pddl,
                       const pddl_action_t *action,
                       const pddl_lifted_mgroup_t *mgroup,
                       const pddl_cond_t *pre,
                       const pddl_cond_t *eff,
                       const pddl_unify_t *unify_del,
                       const pddl_cond_atom_t *del_atom,
                       const pddl_cond_atom_t *mg_atom,
                       action_conds_t *acs,
                       pddl_err_t *err)
{
    const pddl_cond_t *pres[2] = {action->pre, pre};
    for (int i = 0; i < 2; ++i){
        const pddl_cond_t *pre = pres[i];
        if (pre == NULL)
            continue;
        pddl_cond_const_it_t itpre;
        const pddl_cond_atom_t *pre_atom;
        PDDL_COND_FOR_EACH_ATOM(pre, &itpre, pre_atom){
            if (pre_atom->neg)
                continue;
            if (pre_atom->pred != mg_atom->pred)
                continue;
            pddl_unify_t unify;
            pddlUnifyInitCopy(&unify, unify_del);
            if (pddlUnify(&unify, pre_atom, mg_atom) == 0
                    && checkInequality(&unify, pddl, action, pre)){
                deadEndAdd(pddl, action, mgroup, pre, eff, &unify,
                           del_atom, pre_atom, mg_atom, acs, err);
            }
            pddlUnifyFree(&unify);
        }
    }
}

static void deadEndDel(const pddl_t *pddl,
                       const pddl_action_t *action,
                       const pddl_lifted_mgroup_t *mgroup,
                       const pddl_cond_t *pre,
                       const pddl_cond_t *eff,
                       const pddl_unify_t *unify_goal,
                       action_conds_t *acs,
                       pddl_err_t *err)
{
    pddl_cond_const_it_t itdel;
    const pddl_cond_atom_t *del_atom;
    PDDL_COND_FOR_EACH_ATOM(eff, &itdel, del_atom){
        if (!del_atom->neg)
            continue;
        const pddl_cond_atom_t *mg_atom;
        PDDL_COND_ARR_FOR_EACH_ATOM(&mgroup->cond, mg_atom){
            if (del_atom->pred != mg_atom->pred)
                continue;
            pddl_unify_t unify;
            pddlUnifyInitCopy(&unify, unify_goal);
            if (pddlUnify(&unify, del_atom, mg_atom) == 0
                    && checkInequality(&unify, pddl, action, pre)){
                deadEndPre(pddl, action, mgroup, pre, eff, &unify,
                           del_atom, mg_atom, acs, err);
            }
            pddlUnifyFree(&unify);
        }
    }
}

static void deadEndGoal(const pddl_t *pddl,
                        const pddl_action_t *action,
                        const pddl_lifted_mgroup_t *mgroup,
                        const pddl_cond_t *pre,
                        const pddl_cond_t *eff,
                        action_conds_t *acs,
                        pddl_err_t *err)
{
    pddl_cond_const_it_atom_t it;
    const pddl_cond_atom_t *goal_atom, *mg_atom;
    PDDL_COND_FOR_EACH_ATOM(pddl->goal, &it, goal_atom){
        PDDL_COND_ARR_FOR_EACH_ATOM(&mgroup->cond, mg_atom){
            if (goal_atom->pred != mg_atom->pred)
                continue;
            pddl_unify_t unify;
            pddlUnifyInit(&unify, &pddl->type, &action->param, &mgroup->param);
            if (pddlUnify(&unify, goal_atom, mg_atom) == 0){
                pddlUnifyResetCountedVars(&unify);
                deadEndDel(pddl, action, mgroup, pre, eff, &unify, acs, err);
            }
            pddlUnifyFree(&unify);
        }
    }
}

static void deadEnd(const pddl_t *pddl,
                    const pddl_action_t *action,
                    const pddl_lifted_mgroup_t *mgroup,
                    const pddl_cond_t *pre,
                    const pddl_cond_t *eff,
                    action_conds_t *acs,
                    pddl_err_t *err)
{
    deadEndGoal(pddl, action, mgroup, pre, eff, acs, err);
}

static void compileInDeadEnd(const pddl_t *pddl,
                             const pddl_action_t *action,
                             const pddl_lifted_mgroup_t *mgroup_in,
                             action_conds_t *acs,
                             pddl_err_t *err)
{
    pddl_lifted_mgroup_t mgroup;
    pddlLiftedMGroupInitCopy(&mgroup, mgroup_in);
    pddlLiftedMGroupDoubleCounted(&mgroup);

    deadEnd(pddl, action, &mgroup, action->pre, action->eff, acs, err);

    /*
    pddl_cond_const_it_when_t wit;
    const pddl_cond_when_t *when;
    PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when){
        mutex(pddl, action, &mgroup, &action->param,
              when->pre, action->pre, acs, err);
    }
    */

    pddlLiftedMGroupFree(&mgroup);
}

int pddlCompileInLiftedMGroups(pddl_t *pddl,
                               const pddl_lifted_mgroups_t *mgroups,
                               pddl_err_t *err)
{
    int changed = 0;

    CTX(err, "compile_in_lmg", "Compile-in LMG");
    LOG(err, "actions: %{in.actions}d, lifted mgroups: %{in.lmgs}d",
        pddl->action.action_size, mgroups->mgroup_size);
    if (mgroups->mgroup_size == 0){
        LOG2(err, "No lifted mutex groups.");
        LOG(err, "DONE. actions: %{out.actions}d, lifted mgroups: %{out.lmgs}d",
            pddl->action.action_size, mgroups->mgroup_size);
        CTXEND(err);
        return 0;
    }

    for (int ai = 0; ai < pddl->action.action_size; ++ai){
        pddl_action_t *action = pddl->action.action + ai;

        action_conds_t acs;
        actionCondsInit(&acs);
        for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
            compileInMutex(pddl, action, mgroups->mgroup + mgi, &acs, err);
            compileInDeadEnd(pddl, action, mgroups->mgroup + mgi, &acs, err);
        }

        pddl_cond_t *c;
        c = actionCondsMerge(&acs, action->pre, pddl, &action->param);
        if (c != NULL){
            LOG(err, "Precondition of action '%s' extended with %s",
                action->name,
                F_COND_PDDL_BUFSIZE(c, pddl, &action->param, 10000));
            action->pre = pddlCondNewAnd2(action->pre, c);
            changed = 1;
        }

        pddl_cond_const_it_when_t wit;
        const pddl_cond_when_t *when;
        PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when){
            pddl_cond_t *c;
            c = actionCondsMerge(&acs, when->pre, pddl, &action->param);
            if (c != NULL){
                LOG(err, "Precondition of a conditional effect of"
                    " action '%s' extended with %s",
                    action->name,
                    F_COND_PDDL_BUFSIZE(c, pddl, &action->param, 10000));
                pddl_cond_when_t *wwhen = (pddl_cond_when_t *)when;
                wwhen->pre = pddlCondNewAnd2(when->pre, c);
                changed = 1;
            }
        }
        actionCondsFree(&acs);
    }

    if (changed)
        pddlNormalize(pddl);
    LOG(err, "DONE. actions: %{out.actions}d", pddl->action.action_size);
    CTXEND(err);
    return changed;
}
