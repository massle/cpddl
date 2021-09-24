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

static int checkIneq(const pddl_t *pddl,
                     const pddl_cond_t *pre,
                     const unify_subst_t *map)
{
    if (pre == NULL)
        return 1;

    pddl_cond_const_it_atom_t it;
    const pddl_cond_atom_t *ineq;
    PDDL_COND_FOR_EACH_ATOM(pre, &it, ineq){
        if (ineq->neg && ineq->pred == pddl->pred.eq_pred){
            int param0 = ineq->arg[0].param;
            int param1 = ineq->arg[1].param;
            if (param0 >= 0 && param1 >= 0){
                if (map[param0].var != map[param1].var
                        || map[param0].obj != map[param1].obj)
                    return 0;

            }else if (param0 >= 0){
                if (map[param0].var < 0
                       && map[param0].obj != ineq->arg[1].obj)
                    return 0;

            }else if (param1 >= 0){
                if (map[param1].var < 0
                       && map[param1].obj != ineq->arg[1].obj)
                    return 0;
            }
        }
    }
    return 1;
}

static int unifyAtoms(const pddl_t *pddl,
                      const pddl_cond_t *pre,
                      const pddl_cond_t *pre2,
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

    return checkIneq(pddl, pre, map) && checkIneq(pddl, pre2, map);
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
    int found1[pre_param->param_size];
    int found2[pre_param->param_size];
    bzero(found1, sizeof(int) * pre_param->param_size);
    bzero(found2, sizeof(int) * pre_param->param_size);

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

    _and = pddlCondNormalize(_and, pddl, pre_param);
    pddl_cond_t *ret = pddlCondNegate(_and, pddl);
    ret = pddlCondNormalize(ret, pddl, pre_param);
    ret = pddlCondDeduplicate(ret, pddl);
    pddlCondDel(_and);
    return ret;
}

static void prePairMutexLiftedMGroup(const pddl_t *pddl,
                                     const pddl_params_t *pre_param,
                                     const pddl_cond_t *pre,
                                     const pddl_cond_t *pre2,
                                     const pddl_lifted_mgroup_t *mgroup,
                                     const pddl_cond_atom_t *p1,
                                     const pddl_cond_atom_t *p2,
                                     const pddl_cond_atom_t *m2,
                                     const unify_subst_t *map,
                                     int map_size,
                                     pddl_cond_part_t *and)
{
    unify_subst_t *map2 = BOR_ALLOC_ARR(unify_subst_t, map_size);
    if (map2 != NULL)
        memcpy(map2, map, sizeof(unify_subst_t) * map_size);
    if (unifyAtoms(pddl, pre, pre2, pre_param, &mgroup->param, p2, m2, map2, 0)
            && !preAtomsAreEqual(pre_param, p1, p2, map2)){
        pddl_cond_t *c;
        c = condCompileMutex(pddl, pre_param, p1, p2, map2);
        pddlCondPartAdd(and, c);
    }
    if (map2 != NULL)
        BOR_FREE(map2);
}

static pddl_cond_t *preMutexLiftedMGroups(const pddl_t *pddl,
                                          const pddl_params_t *pre_param,
                                          const pddl_cond_t *pre,
                                          const pddl_cond_t *pre2,
                                          const pddl_lifted_mgroup_t *mgroup)
{
    pddl_cond_const_it_atom_t it1, it2;
    const pddl_cond_atom_t *a1, *a2;
    int map_size = mgroup->param.param_size + pre_param->param_size;
    unify_subst_t *map = BOR_ALLOC_ARR(unify_subst_t, map_size);

    pddl_cond_part_t *and = pddlCondToAnd(pddlCondNewEmptyAnd());

    PDDL_COND_FOR_EACH_ATOM(pre, &it1, a1){
        if (a1->neg)
            continue;

        for (int mi1 = 0; mi1 < mgroup->cond.size; ++mi1){
            const pddl_cond_atom_t *ma1;
            ma1 = PDDL_COND_CAST(mgroup->cond.cond[mi1], atom);
            if (ma1->pred != a1->pred)
                continue;

            if (!unifyAtoms(pddl, pre, pre2, pre_param, &mgroup->param,
                            a1, ma1, map, 1)){
                continue;
            }

            if (pre2 == NULL){
                it2 = it1;
                PDDL_COND_FOR_EACH_ATOM_CONT(&it2, a2){
                    if (a2->neg)
                        continue;
                    for (int mi2 = 0; mi2 < mgroup->cond.size; ++mi2){
                        const pddl_cond_atom_t *ma2;
                        ma2 = PDDL_COND_CAST(mgroup->cond.cond[mi2], atom);
                        if (ma2->pred != a2->pred)
                            continue;
                        prePairMutexLiftedMGroup(pddl, pre_param, pre, pre2,
                                                 mgroup, a1, a2, ma2,
                                                 map, map_size, and);
                    }
                }
            }else{
                PDDL_COND_FOR_EACH_ATOM(pre2, &it2, a2){
                    if (a2->neg)
                        continue;
                    for (int mi2 = 0; mi2 < mgroup->cond.size; ++mi2){
                        const pddl_cond_atom_t *ma2;
                        ma2 = PDDL_COND_CAST(mgroup->cond.cond[mi2], atom);
                        if (ma2->pred != a2->pred)
                            continue;
                        prePairMutexLiftedMGroup(pddl, pre_param, pre, pre2,
                                                 mgroup, a1, a2, ma2,
                                                 map, map_size, and);
                    }
                }
            }
        }
    }

    if (map != NULL)
        BOR_FREE(map);

    if (pddlCondPartIsEmpty(and)){
        pddlCondDel(&and->cls);
        return NULL;
    }
    return &and->cls;
}

static void actionCompileInLiftedMGroup(pddl_t *pddl,
                                        pddl_action_t *action,
                                        pddl_cond_arr_t *ce,
                                        const pddl_lifted_mgroup_t *mg,
                                        pddl_cond_t **ext,
                                        pddl_cond_t **ce_ext)
{
    pddl_cond_t *e;
    e = preMutexLiftedMGroups(pddl, &action->param, action->pre, NULL, mg);
    if (e != NULL){
        if (*ext == NULL)
            *ext = pddlCondNewEmptyAnd();
        pddlCondPartAdd(PDDL_COND_CAST(*ext, part), e);
    }

    // Conditional effects
    for (int wi = 0; wi < ce->size; ++wi){
        const pddl_cond_when_t *when = PDDL_COND_CAST(ce->cond[wi], when);
        pddl_cond_t *c;
        c = preMutexLiftedMGroups(pddl, &action->param, when->pre, NULL, mg);
        if (c != NULL){
            if (ce_ext[wi] == NULL)
                ce_ext[wi] = pddlCondNewEmptyAnd();
            pddlCondPartAdd(PDDL_COND_CAST(ce_ext[wi], part), c);
        }
        c = preMutexLiftedMGroups(pddl, &action->param,
                action->pre, when->pre, mg);
        if (c != NULL){
            if (ce_ext[wi] == NULL)
                ce_ext[wi] = pddlCondNewEmptyAnd();
            pddlCondPartAdd(PDDL_COND_CAST(ce_ext[wi], part), c);
        }
    }
}

static void actionCompileInLiftedMGroups(pddl_t *pddl,
                                         pddl_action_t *action,
                                         const pddl_lifted_mgroups_t *mgroups,
                                         bor_err_t *err)
{
    pddl_cond_arr_t ce = PDDL_COND_ARR_INIT;
    pddl_cond_const_it_when_t wit;
    const pddl_cond_when_t *when;
    PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when)
        pddlCondArrAdd(&ce, &when->cls);

    pddl_cond_t **ce_ext = NULL;
    if (ce.size > 0)
        ce_ext = BOR_CALLOC_ARR(pddl_cond_t *, ce.size);
    pddl_cond_t *ext = NULL;

    for (int mi = 0; mi < mgroups->mgroup_size; ++mi){
        const pddl_lifted_mgroup_t *mg = mgroups->mgroup + mi;
        actionCompileInLiftedMGroup(pddl, action, &ce, mg, &ext, ce_ext);
    }

    if (ext != NULL){
        ext = pddlCondNormalize(ext, pddl, &action->param);
        action->pre = pddlCondNewAnd2(action->pre, ext);
        char *spre = pddlCondFormatIntoStr(pddl, ext, &action->param, NULL);
        BOR_INFO(err, "Updated pre of action %s by '%s'", action->name, spre);
        free(spre);
    }

    for (int wi = 0; wi < ce.size; ++wi){
        if (ce_ext[wi] != NULL){
            pddl_cond_when_t *w = (pddl_cond_when_t *)ce.cond[wi];
            ce_ext[wi] = pddlCondNormalize(ce_ext[wi], pddl, &action->param);
            w->pre = pddlCondNewAnd2(w->pre, ce_ext[wi]);
            char *spre = pddlCondFormatIntoStr(pddl, ce_ext[wi], &action->param, NULL);
            BOR_INFO(err, "Updated pre of a conditional effect of action %s"
                          " by '%s'", action->name, spre);
            free(spre);
        }
    }

    pddlCondArrFree(&ce);
    if (ce_ext != NULL)
        BOR_FREE(ce_ext);
}

void pddlCompileInLiftedMGroups(pddl_t *pddl,
                                const pddl_lifted_mgroups_t *mgroups,
                                bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Compile-in LMG: ");
    BOR_INFO2(err, "start");
    for (int i = 0; i < pddl->action.action_size; ++i){
        actionCompileInLiftedMGroups(pddl, pddl->action.action + i,
                                     mgroups, err);
    }
    BOR_INFO2(err, "Normalizing ...");
    pddlNormalize(pddl);
    BOR_INFO2(err, "DONE");
    BOR_INFO_PREFIX_POP(err);
}
