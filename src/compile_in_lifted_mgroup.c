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

static void addTypeRestrict(const pddl_t *pddl,
                            int param,
                            int type,
                            pddl_cond_t *c)
{
    int size;
    const pddl_obj_id_t *objs;
    objs = pddlTypesObjsByType(&pddl->type, type, &size);

    pddl_cond_part_t *p = PDDL_COND_CAST(c, part);
    if (size == 0){
        pddl_cond_bool_t *b = pddlCondNewBool(0);
        pddlCondPartAdd(p, &b->cls);

    }else if (size == 1){
        pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
        eq->pred = pddl->pred.eq_pred;
        eq->arg[0].param = param;
        eq->arg[1].obj = objs[0];
        pddlCondPartAdd(p, &eq->cls);

    }else{
        pddl_cond_t *or = pddlCondNewEmptyOr();
        for (int i = 0; i < size; ++i){
            pddl_cond_atom_t *eq = pddlCondNewEmptyAtom(2);
            eq->pred = pddl->pred.eq_pred;
            eq->arg[0].param = param;
            eq->arg[1].obj = objs[i];
            pddlCondPartAdd(PDDL_COND_CAST(or, part), &eq->cls);
        }
        pddlCondPartAdd(p, or);
    }
}

static void unifyMapReinitCounted(unify_map_t *map)
{
    int resolved[map->mg_param->param_size];
    bzero(resolved, sizeof(int) * map->mg_param->param_size);
    for (int i = 0; i < map->action_param->param_size; ++i){
        if (map->map[i].var >= map->mg_offset){
            int from = map->map[i].var;
            int mg_var = from - map->mg_offset;
            if (!resolved[mg_var]
                    && map->mg_param->param[mg_var].is_counted_var){
                int to = i;
                resolved[mg_var] = 1;
                for (int j = i; j < map->action_param->param_size; ++j){
                    if (map->map[i].var == from)
                        map->map[i].var = to;
                }
            }
        }
    }

    int off = map->mg_offset;
    for (int i = 0; i < map->mg_param->param_size; ++i){
        if (map->mg_param->param[i].is_counted_var){
            map->map[off + i].var = off + i;
            map->map[off + i].var_type = map->mg_param->param[i].type;
            map->map[off + i].obj = PDDL_OBJ_ID_UNDEF;
            map->map[off + i].is_fixed = 0;
        }
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

static void unifyMapSet(unify_map_t *map, int from, int to, int to_type)
{
    int is_fixed = 0;
    for (int i = 0; i < map->map_size; ++i){
        if (map->map[i].var == from){
            map->map[i].var = to;
            map->map[i].var_type = to_type;
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

static int unifyEq(const unify_t *u1, const unify_t *u2)
{
    return memcmp(u1->map.map, u2->map.map,
                  sizeof(unify_subst_t) * u1->map.map_size) == 0;
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

    fprintf(stderr, "Unify (%s", u->pddl->pred.pred[a->pred].name);
    for (int i = 0; i < a->arg_size; ++i){
        if (a->arg[i].param >= 0){
            fprintf(stderr, " %d/t:%d", a->arg[i].param,
                    u->map.action_param->param[a->arg[i].param].type);
        }else{
            fprintf(stderr, " %d/%s", a->arg[i].obj, u->pddl->obj.obj[a->arg[i].obj].name);
        }
    }
    fprintf(stderr, ") : (%s", u->pddl->pred.pred[a2->pred].name);
    for (int i = 0; i < a2->arg_size; ++i){
        if (a2->arg[i].param >= 0){
            fprintf(stderr, " %s%d/t:%d",
                    (u->map.mg_param->param[a2->arg[i].param].is_counted_var ?
                        "c" : "v"),
                    a2->arg[i].param,
                    u->map.mg_param->param[a2->arg[i].param].type);
        }else{
            fprintf(stderr, " %s", u->pddl->obj.obj[a2->arg[i].obj].name);
        }
    }
    fprintf(stderr, ")\n");
    for (int i = 0; i < u->map.map_size; ++i){
        fprintf(stderr, "  %s%d -> v: %d, t: %d, o: %d, f: %d\n",
                (i >= u->map.mg_offset ? "M" : " "),
                i, u->map.map[i].var, u->map.map[i].var_type,
                (int)u->map.map[i].obj, u->map.map[i].is_fixed);
    }
    for (int argi = 0; argi < a->arg_size; ++argi){
        fprintf(stderr, "argi: %d\n", argi);
        pddl_obj_id_t obj1 = a->arg[argi].obj;
        int param1 = a->arg[argi].param;
        int type1 = -1;
        if (param1 >= 0){
            fprintf(stderr, "a: %d -> %d (t: %d/%d, o: %d/%d)\n",
                    param1, u->map.map[param1].var,
                    u->map.map[param1].var_type,
                    (u->map.map[param1].var >= 0 ?
                        u->map.map[u->map.map[param1].var].var_type : -1),
                    (int)(u->map.map[param1].obj),
                    (u->map.map[param1].var >= 0 ?
                        (int)u->map.map[u->map.map[param1].var].obj : -1)
                    );
            obj1 = u->map.map[param1].obj;
            type1 = u->map.map[param1].var_type;
            param1 = u->map.map[param1].var;
        }
        pddl_obj_id_t obj2 = a2->arg[argi].obj;
        int param2 = a2->arg[argi].param;
        int type2 = -1;
        if (param2 >= 0){
            param2 += u->map.mg_offset;
            fprintf(stderr, "ma: %d -> %d (t: %d/%d, o: %d/%d)\n",
                    param2, u->map.map[param2].var,
                    u->map.map[param2].var_type,
                    (u->map.map[param2].var >= 0 ?
                        u->map.map[u->map.map[param2].var].var_type : -1),
                    (int)(u->map.map[param2].obj),
                    (u->map.map[param2].var >= 0 ?
                        (int)(u->map.map[u->map.map[param2].var].obj) : -1)
                    );
            type2 = u->map.map[param2].var_type;
            obj2 = u->map.map[param2].obj;
            param2 = u->map.map[param2].var;
        }

        if (param1 >= 0 && param2 >= 0){
            int to_type = -1;
            if (pddlTypesIsSubset(&u->pddl->type, type2, type1)){
                to_type = type2;
            }else if (pddlTypesIsSubset(&u->pddl->type, type1, type2)){
                to_type = type1;
            }else{
                return 0;
            }
            // Variables with empty types are actually not unifiable
            if (pddlTypeNumObjs(&u->pddl->type, to_type) == 0)
                return 0;
            unifyMapSet(&u->map, param1, param2, to_type);

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

    unifyMapReinitCounted(&u->map);
    fprintf(stderr, "Result:\n");
    for (int i = 0; i < u->map.map_size; ++i){
        fprintf(stderr, "  %s%d -> v: %d, t: %d, o: %d, f: %d\n",
                (i >= u->map.mg_offset ? "M" : " "),
                i, u->map.map[i].var, u->map.map[i].var_type,
                (int)u->map.map[i].obj, u->map.map[i].is_fixed);
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

static pddl_cond_t *unifyDiffToCond(const unify_t *u, const unify_t *ubase)
{
    const unify_subst_t *map = u->map.map;
    const pddl_params_t *param = u->map.action_param;
    pddl_cond_t *cand = pddlCondNewEmptyAnd();

    const unify_subst_t *base = NULL;
    if (ubase != NULL){
        ASSERT_RUNTIME(u->map.action_param == ubase->map.action_param);
        base = ubase->map.map;
    }

    for (int pi = 0; pi < param->param_size; ++pi){
        if (base != NULL && memcmp(&map[pi], &base[pi], sizeof(map[pi])) == 0)
            continue;
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
            ASSERT(pddlTypesIsSubset(&u->pddl->type, map[pi].var_type,
                                     param->param[pi].type));
            addTypeRestrict(u->pddl, pi, map[pi].var_type, cand);
        }
    }

    // TODO: Check (in)equality preconditions

    if (pddlCondPartIsEmpty(PDDL_COND_CAST(cand, part))){
        pddlCondDel(cand);
        return &(pddlCondNewBool(1)->cls);
    }

    return cand;
}

static pddl_cond_t *unifyToCond(const unify_t *u)
{
    return unifyDiffToCond(u, NULL);
}

static pddl_cond_t *atomsEqCond(const pddl_t *pddl,
                                const pddl_params_t *param,
                                const pddl_cond_atom_t *a1,
                                const pddl_cond_atom_t *a2)
{
    if (a1->pred != a2->pred)
        return &pddlCondNewBool(0)->cls;

    pddl_cond_t *c = pddlCondNewEmptyAnd();

    for (int ai = 0; ai < a1->arg_size; ++ai){
        pddl_obj_id_t obj1 = a1->arg[ai].obj;
        int param1 = a1->arg[ai].param;
        int var_type1 = -1;
        if (param1 >= 0)
            var_type1 = param->param[param1].type;

        pddl_obj_id_t obj2 = a2->arg[ai].obj;
        int param2 = a2->arg[ai].param;
        int var_type2 = -1;
        if (param2 >= 0)
            var_type2 = param->param[param2].type;

        _addEq(pddl, param1, var_type1, obj1, param2, var_type2, obj2, 0, c);
    }

    c = pddlCondSimplify(c, pddl, param);
    c = pddlCondNormalize(c, pddl, param);
    c = pddlCondSimplify(c, pddl, param);
    // TODO: Check (in)equality preconditions
    return c;
}

static pddl_cond_t *atomsIneqCond(const pddl_t *pddl,
                                  const pddl_params_t *param,
                                  const pddl_cond_atom_t *a1,
                                  const pddl_cond_atom_t *a2)
{
    pddl_cond_t *c = atomsEqCond(pddl, param, a1, a2);
    pddl_cond_t *neg = pddlCondNegate(c, pddl);
    pddlCondDel(c);

    neg = pddlCondSimplify(neg, pddl, param);
    neg = pddlCondNormalize(neg, pddl, param);
    neg = pddlCondSimplify(neg, pddl, param);
    return neg;
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
        // TODO: ret = (and unifyToCond() atomsIneqCond())
        ret = unifyToCond(&u2);
        if (a1->pred == a2->pred){
            pddl_cond_t *c = atomsIneqCond(u->pddl, u->map.action_param, a1, a2);
            pddl_cond_t *_and = pddlCondNewEmptyAnd();
            pddl_cond_part_t *and = PDDL_COND_CAST(_and, part);
            pddlCondPartAdd(and, ret);
            pddlCondPartAdd(and, c);
            ret = _and;
        }
    }
    unifyFree(&u2);
    return ret;
}

typedef void (*find_cond_fn)(const pddl_t *pddl,
                             const pddl_params_t *action_param,
                             const pddl_cond_t *pre,
                             const pddl_cond_t *pre2,
                             const pddl_cond_t *eff,
                             const pddl_lifted_mgroup_t *mgroup,
                             pddl_cond_arr_t *carr);

static void condArrAddUnique(pddl_cond_arr_t *carr, const pddl_cond_t *c)
{
    int found = 0;
    for (int i = 0; i < carr->size; ++i){
        if (pddlCondEq(carr->cond[i], c)){
            found = 1;
            break;
        }
    }
    if (!found)
        pddlCondArrAdd(carr, c);
}

static void findMutex(const pddl_t *pddl,
                      const pddl_params_t *action_param,
                      const pddl_cond_t *pre,
                      const pddl_cond_t *pre2,
                      const pddl_cond_t *eff,
                      const pddl_lifted_mgroup_t *mgroup,
                      pddl_cond_arr_t *carr)
{
    FOR_EACH_POS(pre, itpre, a1, ma1){
        unify_t u;
        unifyInit(&u, pddl, action_param, &mgroup->param, pre, pre2);
        if (unify(&u, a1, ma1)){
            if (pre2 == NULL){
                FOR_EACH_POS_CONT(itpre, a2, ma2){
                    pddl_cond_t *c = findMutex2(&u, a1, a2, ma2);
                    if (c != NULL){
                        c = pddlCondSimplify(c, pddl, action_param);
                        if (!pddlCondIsFalse(c))
                            condArrAddUnique(carr, c);
                    }
                }

            }else{
                FOR_EACH_POS(pre2, itpre2, a2, ma2){
                    pddl_cond_t *c = findMutex2(&u, a1, a2, ma2);
                    if (c != NULL){
                        c = pddlCondSimplify(c, pddl, action_param);
                        if (!pddlCondIsFalse(c))
                            condArrAddUnique(carr, c);
                    }
                }
            }
        }
        unifyFree(&u);
    }
}

static void findDeadEnd(const pddl_t *pddl,
                        const pddl_params_t *action_param,
                        const pddl_cond_t *pre,
                        const pddl_cond_t *pre2,
                        const pddl_cond_t *eff,
                        const pddl_lifted_mgroup_t *mgroup,
                        pddl_cond_arr_t *carr)
{
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
                int is_false = 0;
                pddl_cond_t *_and = pddlCondNewEmptyAnd();
                pddl_cond_part_t *and = PDDL_COND_CAST(_and, part);

                pddl_cond_t *c = unifyToCond(&u2);
                pddlCondPartAdd(and, c);

                c = atomsEqCond(pddl, action_param, adel, apre);
                pddlCondPartAdd(and, c);

                fprintf(stderr, "MG: %s\n", F_LIFTED_MGROUP(pddl, mgroup));
                fprintf(stderr, "C: %s\n", F_COND_PDDL(c, pddl, action_param));
                FOR_EACH_POS(eff, itadd, aadd, maadd){
                    unify_t u3;
                    unifyInitCopy(&u3, &u2);
                    if (unify(&u3, aadd, maadd)){
                        if (unifyEq(&u2, &u3)){
                            fprintf(stderr, "Is-false\n");
                            pddlCondPartAdd(and, &pddlCondNewBool(0)->cls);
                            is_false = 1;
                            break;
                        }else{
                            pddl_cond_t *c = unifyDiffToCond(&u3, &u2);
                            pddl_cond_t *n = pddlCondNegate(c, pddl);
                            fprintf(stderr, "N: %s\n", F_COND_PDDL(n, pddl, action_param));
                            pddlCondDel(c);
                            pddlCondPartAdd(and, n);
                            is_false = 1;
                            break;
                        }
                    }
                    unifyFree(&u3);
                }

                if (is_false){
                    pddlCondDel(_and);
                }else{
                    fprintf(stderr, "X %s\n", F_COND_PDDL(c, pddl, action_param));
                    pddl_cond_t *c = pddlCondSimplify(_and, pddl, action_param);
                    fprintf(stderr, "DONE %s\n", F_COND_PDDL(c, pddl, action_param));
                    if (!pddlCondIsFalse(c))
                        condArrAddUnique(carr, c);
                }
                unifyFree(&u2);
            }
            unifyFree(&u);
        }
        unifyFree(&ug);
    }
}

static pddl_cond_t *negate(const pddl_t *pddl,
                           const pddl_params_t *action_param,
                           pddl_cond_t *c)
{
    c = pddlCondNormalize(c, pddl, action_param);
    pddl_cond_t *ret = pddlCondNegate(c, pddl);
    pddlCondDel(c);

    ret = pddlCondSimplify(ret, pddl, action_param);
    ret = pddlCondNormalize(ret, pddl, action_param);
    ret = pddlCondSimplify(ret, pddl, action_param);
    return ret;
}

static void actionCompileInLiftedMGroup(pddl_t *pddl,
                                        pddl_action_t *action,
                                        pddl_cond_arr_t *ce,
                                        const pddl_lifted_mgroup_t *mg,
                                        pddl_cond_arr_t *ext,
                                        pddl_cond_arr_t *ce_ext,
                                        find_cond_fn fn,
                                        const char *fn_name,
                                        bor_err_t *err)
{
    fn(pddl, &action->param, action->pre, NULL, action->eff, mg, ext);

    // Conditional effects
    for (int wi = 0; wi < ce->size; ++wi){
        const pddl_cond_when_t *when = PDDL_COND_CAST(ce->cond[wi], when);
        fn(pddl, &action->param, when->pre, NULL, when->eff, mg, ce_ext + wi);
        fn(pddl, &action->param, action->pre, when->pre, when->eff, mg, ce_ext + wi);
    }
}

static pddl_cond_t *constructPreCond(pddl_cond_arr_t *carr,
                                     const pddl_t *pddl,
                                     const pddl_params_t *param)
{
    if (carr->size == 0)
        return NULL;

    pddl_cond_t *pre = negate(pddl, param, (pddl_cond_t *)carr->cond[0]);
    for (int i = 1; i < carr->size; ++i){
        pddl_cond_t *c = (pddl_cond_t *)carr->cond[i];
        pddl_cond_t *and = pddlCondNewAnd2(pre, negate(pddl, param, c));
        pre = pddlCondSimplify(and, pddl, param);
        pre = pddlCondNormalize(pre, pddl, param);
        pre = pddlCondSimplify(pre, pddl, param);
    }
    return pre;
}

static void logConds(pddl_cond_arr_t *carr,
                     const pddl_t *pddl,
                     const pddl_action_t *a,
                     const char *fn_name,
                     bor_err_t *err)
{
    for (int i = 0; i < carr->size; ++i){
        BOR_INFO(err, "Action %s: Found %s condition: %s",
                 a->name, fn_name,
                 F_COND_PDDL(carr->cond[i], pddl, &a->param));
    }
}

static int actionCompileInLiftedMGroups(pddl_t *pddl,
                                        pddl_action_t *action,
                                        const pddl_lifted_mgroups_t *mgroups,
                                        find_cond_fn fn,
                                        const char *fn_name,
                                        bor_err_t *err)
{
    int change = 0;

    // Find conditional effects
    pddl_cond_arr_t ce = PDDL_COND_ARR_INIT;
    pddl_cond_const_it_when_t wit;
    const pddl_cond_when_t *when;
    PDDL_COND_FOR_EACH_WHEN(action->eff, &wit, when)
        pddlCondArrAdd(&ce, &when->cls);

    // Prepare formulas for the action and its conditional effects
    pddl_cond_arr_t *ce_ext = NULL;
    if (ce.size > 0){
        ce_ext = BOR_CALLOC_ARR(pddl_cond_arr_t, ce.size);
        for (int i = 0; i < ce.size; ++i)
            pddlCondArrInit(ce_ext + i);
    }
    pddl_cond_arr_t ext = PDDL_COND_ARR_INIT;

    // Find conditions for each mutex group
    for (int mi = 0; mi < mgroups->mgroup_size; ++mi){
        const pddl_lifted_mgroup_t *mg = mgroups->mgroup + mi;
        actionCompileInLiftedMGroup(pddl, action, &ce, mg, &ext, ce_ext,
                                    fn, fn_name, err);
    }

    // Extend preconditions if we found any new condition
    if (ext.size > 0){
        logConds(&ext, pddl, action, fn_name, err);
        BOR_INFO(err, "Action %s: Constructing precondition ...",
                 action->name);
        pddl_cond_t *pre = constructPreCond(&ext, pddl, &action->param);
        BOR_INFO(err, "Action %s: Updated pre with '%s'",
                 action->name, F_COND_PDDL(pre, pddl, &action->param));
        action->pre = pddlCondNewAnd2(action->pre, pre);
        change = 1;
    }

    for (int wi = 0; wi < ce.size; ++wi){
        if (ce_ext[wi].size > 0){
            logConds(ce_ext + wi, pddl, action, fn_name, err);
            pddl_cond_when_t *w = PDDL_COND_CAST(ce.cond[wi], when);
            BOR_INFO(err, "Action %s: Constructing precondition for cond-eff ...",
                     action->name);
            pddl_cond_t *pre = constructPreCond(&ce_ext[wi], pddl, &action->param);
            BOR_INFO(err, "Action %s: Updated pre of a cond-eff with %s",
                     action->name, F_COND_PDDL(pre, pddl, &action->param));
            w->pre = pddlCondNewAnd2(w->pre, pre);
            change = 1;
        }
    }

    if (ce_ext != NULL){
        for (int i = 0; i < ce.size; ++i)
            pddlCondArrFree(ce_ext + i);
        BOR_FREE(ce_ext);
    }
    pddlCondArrFree(&ext);
    pddlCondArrFree(&ce);

    return change;
}


void pddlCompileInLiftedMGroupsMutex(pddl_t *pddl,
                                     const pddl_lifted_mgroups_t *mgroups,
                                     bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Compile-in LMG Mutex: ");

    int ch = 0;
    BOR_INFO2(err, "Looking for mutexes...");
    for (int i = 0; i < pddl->action.action_size; ++i){
        ch |= actionCompileInLiftedMGroups(pddl, pddl->action.action + i,
                                           mgroups, findMutex, "mutex", err);
    }
    BOR_INFO2(err, "Looking for mutexes DONE");

    if (ch){
        BOR_INFO(err, "Normalizing... (actions: %d)", pddl->action.action_size);
        pddlNormalize(pddl);
        BOR_INFO(err, "Normalized: (actions: %d)", pddl->action.action_size);
    }
    BOR_INFO2(err, "DONE");
    BOR_INFO_PREFIX_POP(err);
}

void pddlCompileInLiftedMGroupsDeadEnd(pddl_t *pddl,
                                       const pddl_lifted_mgroups_t *mgroups,
                                       bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Compile-in LMG Dead-End: ");

    int ch = 0;
    BOR_INFO2(err, "Looking for dead-ends...");
    for (int i = 0; i < pddl->action.action_size; ++i){
        ch |= actionCompileInLiftedMGroups(pddl, pddl->action.action + i,
                                           mgroups, findDeadEnd, "dead-end", err);
    }
    BOR_INFO2(err, "Looking for dead-ends DONE");
    if (ch){
        BOR_INFO(err, "Normalizing... (actions: %d)", pddl->action.action_size);
        pddlNormalize(pddl);
        BOR_INFO(err, "Normalized: (actions: %d)", pddl->action.action_size);
    }
    BOR_INFO2(err, "DONE");
    BOR_INFO_PREFIX_POP(err);
}

void pddlCompileInLiftedMGroups(pddl_t *pddl,
                                const pddl_lifted_mgroups_t *mgroups,
                                bor_err_t *err)
{
    pddlCompileInLiftedMGroupsMutex(pddl, mgroups, err);
    pddlCompileInLiftedMGroupsDeadEnd(pddl, mgroups, err);
}
