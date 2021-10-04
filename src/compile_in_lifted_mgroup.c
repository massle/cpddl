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
#include "fmt.h"
#include "assert.h"


struct unify_subst {
    int var;
    int var_type;
    int is_fixed;
    pddl_obj_id_t obj;
};
typedef struct unify_subst unify_subst_t;

struct unify_map {
    unify_subst_t *map;
    int map_size;
    int mg_offset;
    const pddl_params_t *action_param;
    const pddl_params_t *mg_param;
};
typedef struct unify_map unify_map_t;

struct unify {
    const pddl_t *pddl;
    const pddl_cond_t *ineq;
    const pddl_cond_t *ineq2;
    unify_map_t map;
};
typedef struct unify unify_t;

static void _addEq(const pddl_t *pddl,
                   int param1,
                   int type1,
                   pddl_obj_id_t obj1,
                   int param2,
                   int type2,
                   pddl_obj_id_t obj2,
                   int neg,
                   pddl_cond_t *c)
{
    pddl_cond_part_t *p = PDDL_COND_CAST(c, part);

    if (param1 >= 0 && param2 >= 0){
        if (pddlTypesAreDisjunct(&pddl->type, type1, type2)){
            pddl_cond_bool_t *b = pddlCondNewBool(0);
            pddlCondPartAdd(p, &b->cls);

        }else if (param1 == param2 && neg){
            pddl_cond_bool_t *b = pddlCondNewBool(0);
            pddlCondPartAdd(p, &b->cls);

        }else if (param1 == param2 && !neg){
            pddl_cond_bool_t *b = pddlCondNewBool(1);
            pddlCondPartAdd(p, &b->cls);

        }else{
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
            eq->pred = pddl->pred.eq_pred;
            if (param1 < param2){
                eq->arg[0].param = param1;
                eq->arg[1].param = param2;
            }else{
                eq->arg[0].param = param2;
                eq->arg[1].param = param1;
            }
            pddlCondPartAdd(p, &eq->cls);
        }

    }else if (param1 >= 0){
        if (pddlTypesObjHasType(&pddl->type, type1, obj2)){
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
            eq->pred = pddl->pred.eq_pred;
            eq->arg[0].param = param1;
            eq->arg[1].obj = obj2;
            pddlCondPartAdd(p, &eq->cls);
        }else{
            pddl_cond_bool_t *b = pddlCondNewBool(0);
            pddlCondPartAdd(p, &b->cls);
        }

    }else if (param2 >= 0){
        if (pddlTypesObjHasType(&pddl->type, type2, obj1)){
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(1);
            eq->pred = pddl->pred.eq_pred;
            eq->arg[0].param = param2;
            eq->arg[2].obj = obj1;
            pddlCondPartAdd(p, &eq->cls);
        }else{
            pddl_cond_bool_t *b = pddlCondNewBool(0);
            pddlCondPartAdd(p, &b->cls);
        }

    }else{
        pddl_cond_bool_t *b = NULL;
        if (obj1 == obj2){
            if (neg){
                b = pddlCondNewBool(0);
            }else{
                b = pddlCondNewBool(1);
            }
        }else{
            if (neg){
                b = pddlCondNewBool(1);
            }else{
                b = pddlCondNewBool(0);
            }
        }
        pddlCondPartAdd(p, &b->cls);
    }
}

static void addEqParamObj(const pddl_t *pddl,
                          const pddl_params_t *param,
                          int p,
                          pddl_obj_id_t obj,
                          pddl_cond_t *c)
{
    _addEq(pddl, p, param->param[p].type, PDDL_OBJ_ID_UNDEF, -1, -1, obj, 0, c);
}

static void addEqParamObj2(const pddl_t *pddl,
                           int p,
                           int p_type,
                           pddl_obj_id_t obj,
                           pddl_cond_t *c)
{
    _addEq(pddl, p, p_type, PDDL_OBJ_ID_UNDEF, -1, -1, obj, 0, c);
}

static void addIneqParamObj(const pddl_t *pddl,
                            const pddl_params_t *param,
                            int p,
                            pddl_obj_id_t obj,
                            pddl_cond_t *c)
{
    _addEq(pddl, p, param->param[p].type, PDDL_OBJ_ID_UNDEF, -1, -1, obj, 1, c);
}

static void addIneqParamObj2(const pddl_t *pddl,
                             int p,
                             int p_type,
                             pddl_obj_id_t obj,
                             pddl_cond_t *c)
{
    _addEq(pddl, p, p_type, PDDL_OBJ_ID_UNDEF, -1, -1, obj, 1, c);
}

static void addEqParam(const pddl_t *pddl,
                       const pddl_params_t *param,
                       int p1,
                       int p2,
                       pddl_cond_t *c)
{
    _addEq(pddl, p1, param->param[p1].type, PDDL_OBJ_ID_UNDEF,
                 p2, param->param[p2].type, PDDL_OBJ_ID_UNDEF,
                 0, c);
}


static void addIneqParam(const pddl_t *pddl,
                         const pddl_params_t *param,
                         int p1,
                         int p2,
                         pddl_cond_t *c)
{
    _addEq(pddl, p1, param->param[p1].type, PDDL_OBJ_ID_UNDEF,
                 p2, param->param[p2].type, PDDL_OBJ_ID_UNDEF,
                 1, c);
}

static void addIneqParam2(const pddl_t *pddl,
                          int p1,
                          int p1_type,
                          int p2,
                          int p2_type,
                          pddl_cond_t *c)
{
    _addEq(pddl, p1, p1_type, PDDL_OBJ_ID_UNDEF,
                 p2, p2_type, PDDL_OBJ_ID_UNDEF,
                 1, c);
}

static void unifyMapReinit(unify_map_t *map)
{
    for (int i = 0; i < map->action_param->param_size; ++i){
        map->map[i].var = i;
        map->map[i].var_type = map->action_param->param[i].type;
        map->map[i].obj = PDDL_OBJ_ID_UNDEF;
        map->map[i].is_fixed = 0;
    }

    int off = map->mg_offset;
    for (int i = 0; i < map->mg_param->param_size; ++i){
        map->map[off + i].var = off + i;
        map->map[off + i].var_type = map->mg_param->param[i].type;
        map->map[off + i].obj = PDDL_OBJ_ID_UNDEF;
        map->map[off + i].is_fixed = !map->mg_param->param[i].is_counted_var;
    }
}

static void unifyMapInit(unify_map_t *map,
                         const pddl_params_t *action_param,
                         const pddl_params_t *mg_param)
{
    map->action_param = action_param;
    map->mg_param = mg_param;
    map->map_size = action_param->param_size + mg_param->param_size;
    map->map = BOR_CALLOC_ARR(unify_subst_t, map->map_size);
    map->mg_offset = action_param->param_size;
    unifyMapReinit(map);
}

static void unifyMapCopy(unify_map_t *map, const unify_map_t *src)
{
    ASSERT(map->map_size == src->map_size);
    memcpy(map->map, src->map, sizeof(unify_subst_t) * map->map_size);
}

static void unifyMapInitCopy(unify_map_t *map, const unify_map_t *src)
{
    *map = *src;
    map->map = BOR_ALLOC_ARR(unify_subst_t, map->map_size);
    unifyMapCopy(map, src);
}

static void unifyMapFree(unify_map_t *map)
{
    if (map->map != NULL)
        BOR_FREE(map->map);
}

static void unifyMapSet(unify_map_t *map, int from, int to)
{
    int is_fixed = 0;
    for (int i = 0; i < map->map_size; ++i){
        if (map->map[i].var == from){
            map->map[i].var = to;
            map->map[i].var_type = map->map[to].var_type;
        }
        if (map->map[i].var == to && map->map[i].is_fixed)
            is_fixed = 1;
    }
    if (is_fixed){
        for (int i = 0; i < map->map_size; ++i){
            if (map->map[i].var == to)
                map->map[i].is_fixed = is_fixed;
        }
    }
}

static void unifyMapSetObj(unify_map_t *map, int from, pddl_obj_id_t to)
{
    for (int i = 0; i < map->map_size; ++i){
        if (map->map[i].var == from){
            map->map[i].var = -1;
            map->map[i].var_type = -1;
            map->map[i].obj = to;
        }
    }
}

static void unifyInit(unify_t *u,
                      const pddl_t *pddl,
                      const pddl_params_t *action_param,
                      const pddl_params_t *mg_param,
                      const pddl_cond_t *ineq,
                      const pddl_cond_t *ineq2)
{
    bzero(u, sizeof(*u));
    u->pddl = pddl;
    u->ineq = ineq;
    u->ineq2 = ineq2;
    unifyMapInit(&u->map, action_param, mg_param);
}

static void unifyInitCopy(unify_t *u, const unify_t *src)
{
    *u = *src;
    unifyMapInitCopy(&u->map, &src->map);
}

static void unifyFree(unify_t *u)
{
    unifyMapFree(&u->map);
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

static int unify(unify_t *u,
                 const pddl_cond_atom_t *a,
                 const pddl_cond_atom_t *a2)
{
    if (a->pred != a2->pred)
        return 0;

    for (int argi = 0; argi < a->arg_size; ++argi){
        pddl_obj_id_t obj1 = a->arg[argi].obj;
        int param1 = a->arg[argi].param;
        int type1 = -1;
        if (param1 >= 0){
            type1 = u->map.map[param1].var_type;
            obj1 = u->map.map[param1].obj;
            param1 = u->map.map[param1].var;
        }
        pddl_obj_id_t obj2 = a2->arg[argi].obj;
        int param2 = a2->arg[argi].param;
        int type2 = -1;
        if (param2 >= 0){
            param2 += u->map.mg_offset;
            type2 = u->map.map[param2].var_type;
            obj2 = u->map.map[param2].obj;
            param2 = u->map.map[param2].var;
        }

        if (param1 >= 0 && param2 >= 0){
            int from = -1, to = -1, to_type = -1;
            if (pddlTypesIsSubset(&u->pddl->type, type2, type1)){
                from = param1;
                to = param2;
            }else if (pddlTypesIsSubset(&u->pddl->type, type1, type2)){
                from = param2;
                to = param1;
            }else{
                return 0;
            }
            // Variables with empty types are actually not unifiable
            if (pddlTypeNumObjs(&u->pddl->type, to_type) == 0)
                return 0;
            unifyMapSet(&u->map, from, to);

        }else if (param1 >= 0){
            if (!pddlTypesObjHasType(&u->pddl->type, type1, obj2))
                return 0;
            unifyMapSetObj(&u->map, param1, obj2);

        }else if (param2 >= 0){
            if (!pddlTypesObjHasType(&u->pddl->type, type2, obj1))
                return 0;
            unifyMapSetObj(&u->map, param2, obj1);

        }else{
            if (obj1 != obj2)
                return 0;
        }
    }

    return checkIneq(u->pddl, u->ineq, u->map.map)
                && checkIneq(u->pddl, u->ineq2, u->map.map);
}

static int atomsEq(const unify_t *u,
                   const pddl_cond_atom_t *a1,
                   const pddl_cond_atom_t *a2)
{
    if (a1->pred != a2->pred)
        return 0;
    for (int ai = 0; ai < a1->arg_size; ++ai){
        if (a1->arg[ai].param >= 0 && a2->arg[ai].param >= 0){
            int p1 = a1->arg[ai].param;
            int p2 = a2->arg[ai].param;
            if (p1 == p2)
                continue;

            if (u->map.map[p1].var != u->map.map[p2].var
                    || u->map.map[p1].obj != u->map.map[p2].obj){
                return 0;
            }
            if (u->map.map[p1].var == u->map.map[p2].var
                    && !u->map.map[p1].is_fixed){
                return 0;
            }

        }else if (a1->arg[ai].param >= 0){
            int p1 = a1->arg[ai].param;
            pddl_obj_id_t o2 = a2->arg[ai].obj;
            if (u->map.map[p1].var >= 0){
                return 0;
            }else if (u->map.map[p1].obj != o2){
                return 0;
            }

        }else if (a2->arg[ai].param >= 0){
            pddl_obj_id_t o1 = a1->arg[ai].obj;
            int p2 = a2->arg[ai].param;
            if (u->map.map[p2].var >= 0){
                return 0;
            }else if (u->map.map[p2].obj != o1){
                return 0;
            }

        }else{
            if (a1->arg[ai].obj != a2->arg[ai].obj)
                return 0;
        }
    }
    return 1;
}

static int isOnlyObj(const pddl_t *pddl, int type, pddl_obj_id_t obj)
{
    return pddlTypesObjHasType(&pddl->type, type, obj)
                && pddlTypeNumObjs(&pddl->type, type) == 1;
}

static pddl_cond_t *unifyToCond(const unify_t *u)
{
    const unify_subst_t *map = u->map.map;
    const pddl_params_t *param = u->map.action_param;
    pddl_cond_t *cand = pddlCondNewEmptyAnd();

    for (int pi = 0; pi < param->param_size; ++pi){
        if (map[pi].var < 0){
            ASSERT(map[pi].obj >= 0);
            if (!isOnlyObj(u->pddl, param->param[pi].type, map[pi].obj))
                addEqParamObj(u->pddl, param, pi, map[pi].obj, cand);

        }else{
            for (int qi = pi + 1; qi < param->param_size; ++qi){
                if (map[pi].var == map[qi].var && map[pi].is_fixed){
                    ASSERT(map[pi].var_type == map[qi].var_type);
                    ASSERT(map[pi].is_fixed == map[qi].is_fixed);
                    addEqParam(u->pddl, param, pi, qi, cand);
                }
            }
        }

        if (map[pi].var >= 0 && map[pi].var_type != param->param[pi].type){
            BOR_FATAL2("Different types (2) are not supported yet!");
        }
    }

    if (pddlCondPartIsEmpty(PDDL_COND_CAST(cand, part))){
        pddlCondDel(cand);
        return &(pddlCondNewBool(1)->cls);
    }

    return cand;
}

static pddl_cond_t *differentiateAtoms(const unify_t *u,
                                       const pddl_cond_atom_t *a1,
                                       const pddl_cond_atom_t *a2)
{
    pddl_cond_t *ret = pddlCondNewEmptyOr();

    ASSERT(a1->pred == a2->pred);
    for (int ai = 0; ai < a1->arg_size; ++ai){
        pddl_obj_id_t obj1 = a1->arg[ai].obj;
        int param1 = a1->arg[ai].param;
        int var_type1 = -1;
        if (param1 >= 0){
            obj1 = u->map.map[param1].obj;
            var_type1 = u->map.map[param1].var_type;
            param1 = u->map.map[param1].var;
        }
        pddl_obj_id_t obj2 = a2->arg[ai].obj;
        int param2 = a2->arg[ai].param;
        int var_type2 = -1;
        if (param2 >= 0){
            obj2 = u->map.map[param2].obj;
            var_type2 = u->map.map[param2].var_type;
            param2 = u->map.map[param2].var;
        }

        if (param1 >= 0 && param2 >= 0){
            if (param1 == param2)
                continue;
            if (!pddlTypesAreDisjunct(&u->pddl->type, var_type1, var_type2)){
                pddlCondDel(ret);
                return NULL;
            }
            addIneqParam2(u->pddl, param1, var_type1, param2, var_type2, ret);

        }else if (param1 >= 0){
            if (!pddlTypesObjHasType(&u->pddl->type, var_type1, obj2)){
                pddlCondDel(ret);
                return NULL;
            }
            addIneqParamObj2(u->pddl, param1, var_type1, obj2, ret);

        }else if (param2 >= 0){
            if (!pddlTypesObjHasType(&u->pddl->type, var_type2, obj1)){
                pddlCondDel(ret);
                return NULL;
            }
            addIneqParamObj2(u->pddl, param2, var_type2, obj2, ret);

        }else{
            if (obj1 != obj2){
                pddlCondDel(ret);
                return NULL;
            }
        }
    }

    return ret;
}


#define FOR_EACH_POS(PRE, IT, A, MA) \
    pddl_cond_const_it_atom_t IT; \
    const pddl_cond_atom_t *A, *MA; \
    PDDL_COND_FOR_EACH_ATOM((PRE), &IT, A) \
        if (!A->neg) \
            for (int _i = 0; _i < mgroup->cond.size \
                        && (MA = PDDL_COND_CAST(mgroup->cond.cond[_i], atom));\
                    ++_i) \
                if (MA->pred == A->pred)

#define FOR_EACH_POS_CONT(IT, A, MA) \
    pddl_cond_const_it_atom_t __it = IT; \
    const pddl_cond_atom_t *A, *MA; \
    PDDL_COND_FOR_EACH_ATOM_CONT(&__it, A) \
        if (!A->neg) \
            for (int _i = 0; _i < mgroup->cond.size \
                        && (MA = PDDL_COND_CAST(mgroup->cond.cond[_i], atom));\
                    ++_i) \
                if (MA->pred == A->pred)

#define FOR_EACH_NEG(PRE, IT, A, MA) \
    pddl_cond_const_it_atom_t IT; \
    const pddl_cond_atom_t *A, *MA; \
    PDDL_COND_FOR_EACH_ATOM((PRE), &IT, A) \
        if (A->neg) \
            for (int _i = 0; _i < mgroup->cond.size \
                        && (MA = PDDL_COND_CAST(mgroup->cond.cond[_i], atom));\
                    ++_i) \
                if (MA->pred == A->pred)

static pddl_cond_t *findMutex2(const unify_t *u,
                               const pddl_cond_atom_t *a1,
                               const pddl_cond_atom_t *a2,
                               const pddl_cond_atom_t *ma)
{
    pddl_cond_t *ret = NULL;
    unify_t u2;
    unifyInitCopy(&u2, u);
    if (unify(&u2, a2, ma) && !atomsEq(&u2, a1, a2)){
        ret = unifyToCond(&u2);
        if (a1->pred == a2->pred){
            pddl_cond_t *c = differentiateAtoms(u, a1, a2);
            if (c != NULL){
                pddl_cond_t *_and = pddlCondNewEmptyAnd();
                pddl_cond_part_t *and = PDDL_COND_CAST(_and, part);
                pddlCondPartAdd(and, ret);
                pddlCondPartAdd(and, c);
                ret = _and;
            }
        }
    }
    unifyFree(&u2);
    return ret;
}

static pddl_cond_t *findMutex(const pddl_t *pddl,
                              const pddl_params_t *action_param,
                              const pddl_cond_t *pre,
                              const pddl_cond_t *pre2,
                              const pddl_lifted_mgroup_t *mgroup)
{
    pddlLiftedMGroupPrint(pddl, mgroup, stderr);
    pddl_cond_t *ret = pddlCondNewEmptyOr();
    pddl_cond_part_t *or = PDDL_COND_CAST(ret, part);

    FOR_EACH_POS(pre, itpre, a1, ma1){
        unify_t u;
        unifyInit(&u, pddl, action_param, &mgroup->param, pre, pre2);
        if (unify(&u, a1, ma1)){
            if (pre2 == NULL){
                FOR_EACH_POS_CONT(itpre, a2, ma2){
                    pddl_cond_t *c = findMutex2(&u, a1, a2, ma2);
                    if (c != NULL)
                        pddlCondPartAdd(or, c);
                }

            }else{
                FOR_EACH_POS(pre2, itpre2, a2, ma2){
                    pddl_cond_t *c = findMutex2(&u, a1, a2, ma2);
                    if (c != NULL)
                        pddlCondPartAdd(or, c);
                }
            }
        }
        unifyFree(&u);
    }

    if (pddlCondPartIsEmpty(or)){
        pddlCondDel(ret);
        ret = NULL;

    }else{
        fprintf(stderr, "X %s\n", F_COND_PDDL(ret, pddl, action_param));
        ret = pddlCondNormalize(ret, pddl, action_param);
        pddl_cond_t *r = pddlCondNegate(ret, pddl);
        pddlCondDel(ret);
        ret = r;

        ret = pddlCondNormalize(ret, pddl, action_param);
        ret = pddlCondDeconflictPre(ret, pddl, action_param);
    }
    return ret;
}

static pddl_cond_t *findDeadEnd(const pddl_t *pddl,
                                const pddl_params_t *action_param,
                                const pddl_cond_t *pre,
                                const pddl_cond_t *pre2,
                                const pddl_cond_t *eff,
                                const pddl_lifted_mgroup_t *mgroup)
{
    fprintf(stderr, "findDeadEnd: ");
    pddlLiftedMGroupPrint(pddl, mgroup, stderr);
    FOR_EACH_POS(pddl->goal, itgoal, agoal, magoal){
        unify_t ug;
        unifyInit(&ug, pddl, action_param, &mgroup->param, pre, pre2);
        if (!unify(&ug, agoal, magoal)){
            unifyFree(&ug);
            continue;
        }

        FOR_EACH_NEG(eff, itdel, adel, madel){
            unify_t u;
            unifyInitCopy(&u, &ug);
            if (!unify(&u, adel, madel)){
                unifyFree(&u);
                continue;
            }
            pddl_cond_const_it_atom_t itpre;
            const pddl_cond_atom_t *apre;
            PDDL_COND_FOR_EACH_ATOM(pre, &itpre, apre){
                if (apre->neg || apre->pred != adel->pred)
                    continue;
                unify_t u2;
                unifyInitCopy(&u2, &u);
                if (!unify(&u2, apre, madel)){
                    unifyFree(&u2);
                    continue;
                }
                pddl_cond_t *_and = pddlCondNewEmptyAnd();
                pddl_cond_part_t *and = PDDL_COND_CAST(_and, part);

                pddl_cond_t *c = unifyToCond(&u2);
                fprintf(stderr, "Pre-del: %s\n",
                        F_COND_PDDL(c, pddl, action_param));
                pddlCondPartAdd(and, c);

                FOR_EACH_POS(eff, itadd, aadd, maadd){
                    unify_t u3;
                    unifyInitCopy(&u3, &u2);
                    if (unify(&u3, aadd, maadd)){
                        pddl_cond_t *c = unifyToCond(&u3);
                        pddl_cond_t *n = pddlCondNegate(c, pddl);
                        fprintf(stderr, "add: %s\n",
                                F_COND_PDDL(n, pddl, action_param));
                        pddlCondPartAdd(and, n);
                    }
                    unifyFree(&u3);
                }

                _and = pddlCondNormalize(_and, pddl, action_param);
                _and = pddlCondDeconflictPre(_and, pddl, action_param);
                fprintf(stderr, "=== COND: %s\n",
                        F_COND_PDDL(_and, pddl, action_param));
                pddlCondDel(_and);
                unifyFree(&u2);
            }
            unifyFree(&u);
        }
        unifyFree(&ug);
    }
    return NULL;
}

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
            pre_type = map[pre_param].var_type;
            pre_obj = map[pre_param].obj;
            pre_param = map[pre_param].var;
        }
        pddl_obj_id_t mg_obj = a_mgroup->arg[argi].obj;
        int mg_param = a_mgroup->arg[argi].param;
        int mg_type = -1;
        if (mg_param >= 0){
            mg_param += pre_params->param_size;
            mg_type = map[mg_param].var_type;
            mg_obj = map[mg_param].obj;
            mg_param = map[mg_param].var;
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
    pddl_cond_t *or = pddlCondNewEmptyOr();
    int obj_size;
    const pddl_obj_id_t *obj;
    obj = pddlTypesObjsByType(&pddl->type, parent_type, &obj_size);
    for (int i = 0; i < obj_size; ++i){
        addEqParamObj2(pddl, var, type, obj[i], or);
    }
    if (pddlCondPartIsEmpty(PDDL_COND_CAST(or, part))){
        pddlCondDel(or);
    }else{
        pddlCondPartAdd(and, or);
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
            addEqParamObj(pddl, param, p, map[p].obj, &and->cls);
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
            if (!found1[p1] && !found2[p2] && p1 != p2)
                addIneqParam(pddl, param, p1, p2, _or);

        }else if (p1 >= 0){
            addIneqParamObj(pddl, param, p1, a2->arg[i].obj, _or);

        }else if (p2 >= 0){
            addIneqParamObj(pddl, param, p2, a1->arg[i].obj, _or);
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

static int checkPreDel(const pddl_cond_atom_t *apre,
                       const pddl_cond_atom_t *adel,
                       const unify_subst_t *map)
{
    ASSERT(apre->pred == adel->pred);
    for (int ai = 0; ai < apre->arg_size; ++ai){
        if (apre->arg[ai].param == adel->arg[ai].param
                && apre->arg[ai].obj == adel->arg[ai].obj)
            continue;
        pddl_obj_id_t pre_obj = apre->arg[ai].obj;
        pddl_obj_id_t del_obj = adel->arg[ai].obj;
        int pre_param = apre->arg[ai].param;
        int is_fixed = 0;
        if (pre_param >= 0){
            pre_param = map[pre_param].var;
            is_fixed = map[pre_param].is_fixed;
            pre_obj = map[pre_param].obj;
        }
        int del_param = adel->arg[ai].param;
        if (del_param >= 0){
            del_param = map[del_param].var;
            del_obj = map[del_param].obj;
        }
        if (pre_param != del_param || pre_obj != del_obj)
            return 0;
        if (pre_param >= 0 && !is_fixed)
            return 0;
    }
    return 1;
}

static pddl_cond_t *findDeadEndCondAdd(const pddl_t *pddl,
                                       const pddl_params_t *param,
                                       const pddl_cond_t *eff,
                                       const pddl_cond_t *pre,
                                       const pddl_lifted_mgroup_t *mgroup,
                                       const unify_subst_t *_map)
{
    int map_size = mgroup->param.param_size + param->param_size;
    unify_subst_t *map = BOR_ALLOC_ARR(unify_subst_t, map_size);

    pddl_cond_const_it_atom_t itadd;
    const pddl_cond_atom_t *aadd;
    PDDL_COND_FOR_EACH_ATOM(eff, &itadd, aadd){
        if (aadd->neg)
            continue;

        for (int mi = 0; mi < mgroup->cond.size; ++mi){
            const pddl_cond_atom_t *ma;
            ma = PDDL_COND_CAST(mgroup->cond.cond[mi], atom);
            if (ma->pred != aadd->pred)
                continue;
            memcpy(map, _map, sizeof(unify_subst_t) * map_size);
            if (unifyAtoms(pddl, pre, NULL, param, &mgroup->param,
                           aadd, ma, map, 0)){
                if (map != NULL)
                    BOR_FREE(map);
                return NULL;
            }
        }
    }
    fprintf(stderr, "FOUND\n");

    if (map != NULL)
        BOR_FREE(map);

    return NULL;
}

static pddl_cond_t *findDeadEndCondPre(const pddl_t *pddl,
                                       const pddl_params_t *param,
                                       const pddl_cond_t *eff,
                                       const pddl_cond_t *pre,
                                       const pddl_lifted_mgroup_t *mgroup,
                                       const pddl_cond_atom_t *adel,
                                       const pddl_cond_atom_t *amg,
                                       const unify_subst_t *_map)
{
    int map_size = mgroup->param.param_size + param->param_size;
    unify_subst_t *map = BOR_ALLOC_ARR(unify_subst_t, map_size);

    pddl_cond_const_it_atom_t itpre;
    const pddl_cond_atom_t *apre;
    PDDL_COND_FOR_EACH_ATOM(pre, &itpre, apre){
        if (apre->neg || apre->pred != adel->pred)
            continue;
        memcpy(map, _map, sizeof(unify_subst_t) * map_size);
        if (!unifyAtoms(pddl, pre, NULL, param, &mgroup->param,
                        apre, amg, map, 0)){
            continue;
        }
        if (!checkPreDel(apre, adel, map))
            continue;

        // TODO
        findDeadEndCondAdd(pddl, param, eff, pre, mgroup, map);
    }

    if (map != NULL)
        BOR_FREE(map);

    return NULL;
}

static pddl_cond_t *findDeadEndCond(const pddl_t *pddl,
                                    const pddl_params_t *param,
                                    const pddl_cond_t *eff,
                                    const pddl_cond_t *pre,
                                    const pddl_lifted_mgroup_t *mgroup)
{
    int map_size = mgroup->param.param_size + param->param_size;
    unify_subst_t *map = BOR_ALLOC_ARR(unify_subst_t, map_size);

    pddl_cond_const_it_atom_t itdel;
    const pddl_cond_atom_t *adel;
    PDDL_COND_FOR_EACH_ATOM(eff, &itdel, adel){
        if (!adel->neg)
            continue;

        for (int mi = 0; mi < mgroup->cond.size; ++mi){
            const pddl_cond_atom_t *ma;
            ma = PDDL_COND_CAST(mgroup->cond.cond[mi], atom);
            if (ma->pred != adel->pred)
                continue;

            if (!unifyAtoms(pddl, pre, NULL, param, &mgroup->param,
                            adel, ma, map, 1)){
                continue;
            }

            // TODO
            findDeadEndCondPre(pddl, param, eff, pre, mgroup, adel, ma, map);
        }
    }
    if (map != NULL)
        BOR_FREE(map);
    return NULL;
}

static void actionCompileInLiftedMGroup(pddl_t *pddl,
                                        pddl_action_t *action,
                                        pddl_cond_arr_t *ce,
                                        const pddl_lifted_mgroup_t *mg,
                                        pddl_cond_t **ext,
                                        pddl_cond_t **ce_ext)
{
    fprintf(stderr, "Action %s\n", action->name);
    pddl_cond_t *e;
    e = findMutex(pddl, &action->param, action->pre, NULL, mg);
    if (e != NULL){
    fprintf(stderr, "Updated pre of action %s by '%s'\n",
                    action->name, F_COND(e, pddl, &action->param));
    }
    e = findDeadEnd(pddl, &action->param, action->pre, NULL, action->eff, mg);
    e = preMutexLiftedMGroups(pddl, &action->param, action->pre, NULL, mg);
    if (e != NULL){
        if (*ext == NULL)
            *ext = pddlCondNewEmptyAnd();
        pddlCondPartAdd(PDDL_COND_CAST(*ext, part), e);
    }
    findDeadEndCond(pddl, &action->param, action->eff, action->pre, mg);

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
        BOR_INFO(err, "Updated pre of action %s by '%s'",
                 action->name, F_COND(ext, pddl, &action->param));
    }

    for (int wi = 0; wi < ce.size; ++wi){
        if (ce_ext[wi] != NULL){
            pddl_cond_when_t *w = (pddl_cond_when_t *)ce.cond[wi];
            ce_ext[wi] = pddlCondNormalize(ce_ext[wi], pddl, &action->param);
            w->pre = pddlCondNewAnd2(w->pre, ce_ext[wi]);
            BOR_INFO(err, "Updated pre of a conditional effect of action %s"
                          " by '%s'", action->name,
                          F_COND(ce_ext[wi], pddl, &action->param));
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
