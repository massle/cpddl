/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
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

#include <boruvka/err.h>
#include <boruvka/rand-mt.h>
#include "pddl/homomorphism.h"
#include "pddl/endomorphism.h"
#include "assert.h"

struct fix_action {
    pddl_t *pddl;
    pddl_action_t *action;
    const int *affected_types;
};

static int _removeAffectedNegativeAtoms(pddl_cond_t **c, void *_data)
{
    if ((*c)->type == PDDL_COND_ATOM){
        const struct fix_action *data = _data;
        const pddl_param_t *params = data->action->param.param;
        pddl_cond_atom_t *atom = PDDL_COND_CAST(*c, atom);
        if (atom->neg){
            for (int pi = 0; pi < atom->arg_size; ++pi){
                int param = atom->arg[pi].param;
                if (param >= 0 && data->affected_types[params[param].type]){
                    pddlCondDel(*c);
                    *c = NULL;
                    break;
                }
            }
        }

    }else if ((*c)->type == PDDL_COND_WHEN){
        pddl_cond_when_t *w = PDDL_COND_CAST(*c, when);
        if (w->pre == NULL)
            w->pre = &pddlCondNewBool(1)->cls;
        if (w->eff == NULL){
            pddlCondDel(*c);
            *c = NULL;
        }
    }
    return 0;
}

static void fixAction(pddl_t *pddl,
                      pddl_action_t *action,
                      const int *affected_types,
                      bor_err_t *err)
{
    struct fix_action data;
    data.pddl = pddl;
    data.action = action;
    data.affected_types = affected_types;
    pddlCondRebuild(&action->pre, NULL, _removeAffectedNegativeAtoms, &data);
    pddlCondRebuild(&action->eff, NULL, _removeAffectedNegativeAtoms, &data);
    if (action->pre == NULL)
        action->pre = &pddlCondNewBool(1)->cls;
}

static void fixActions(pddl_t *pddl,
                       int repr,
                       const int *collapse_map,
                       bor_err_t *err)
{
    ASSERT(repr >= 0);
    int *affected_types = BOR_CALLOC_ARR(int, pddl->type.type_size);
    for (int ti = 0; ti < pddl->type.type_size; ++ti){
        if (pddlTypesObjHasType(&pddl->type, ti, repr))
            affected_types[ti] = 1;
    }
    for (int ai = 0; ai < pddl->action.action_size; ++ai)
        fixAction(pddl, pddl->action.action + ai, affected_types, err);
    BOR_FREE(affected_types);
}

static int _collectGoalObjs(pddl_cond_t *c, void *_goal_objs)
{
    bor_iset_t *goal_objs = _goal_objs;
    if (c->type == PDDL_COND_ATOM){
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        for (int i = 0; i < atom->arg_size; ++i){
            if (atom->arg[i].obj >= 0)
                borISetAdd(goal_objs, atom->arg[i].obj);
        }
    }
    return 0;
}

static void collectGoalObjs(const pddl_t *pddl, bor_iset_t *goal_objs)
{
    pddlCondTraverse(pddl->goal, NULL, _collectGoalObjs, goal_objs);
}

static int collapseObjs(pddl_t *pddl,
                        int *collapse_map,
                        pddl_obj_id_t *obj_map,
                        int obj_size,
                        bor_err_t *err)
{
    int repr = -1;
    for (int i = 0; i < pddl->obj.obj_size; ++i){
        if (collapse_map[i]){
            repr = i;
            break;
        }
    }

    pddl_obj_id_t *remap = BOR_CALLOC_ARR(pddl_obj_id_t, pddl->obj.obj_size);
    for (int i = 0, idx = 0; i < pddl->obj.obj_size; ++i){
        if (collapse_map[i] && i != repr){
            remap[i] = repr;
        }else{
            remap[i] = idx++;
        }
    }

    if (obj_map != NULL){
        for (int i = 0; i < obj_size; ++i)
            obj_map[i] = remap[obj_map[i]];
    }

    pddlCondRemapObjs(&pddl->init->cls, remap);
    pddlCondRemapObjs(pddl->goal, remap);
    fixActions(pddl, repr, collapse_map, err);
    pddlActionsRemapObjs(&pddl->action, remap);

    for (int i = 0; i < pddl->obj.obj_size; ++i){
        if (collapse_map[i] && i != repr)
            remap[i] = -1;
    }
    pddlTypesRemapObjs(&pddl->type, remap);
    pddlObjsRemap(&pddl->obj, remap);
    // TODO
    //pddlNormalize(pddl);

    BOR_FREE(remap);
    return 0;
}

static int collapseEndomorphism(pddl_t *pddl,
                                const pddl_homomorphism_config_t *cfg,
                                bor_rand_mt_t *rnd,
                                pddl_obj_id_t *obj_map,
                                int obj_size,
                                bor_err_t *err)
{
    BOR_INFO2(err, "Collapse with endomorphisms");
    // TODO
    pddl_endomorphism_config_t ecfg = PDDL_ENDOMORPHISM_CONFIG_INIT;
    BOR_ISET(redundant);
    pddl_obj_id_t *map = BOR_ALLOC_ARR(pddl_obj_id_t, pddl->obj.obj_size);
    int ret = pddlEndomorphismRelaxedLifted(pddl, &ecfg, &redundant, map, err);
    if (ret == 0 && borISetSize(&redundant) > 0){
        pddl_obj_id_t *remap = BOR_CALLOC_ARR(pddl_obj_id_t, pddl->obj.obj_size);
        int oid;
        BOR_ISET_FOR_EACH(&redundant, oid)
            remap[oid] = PDDL_OBJ_ID_UNDEF;
        for (int i = 0, idx = 0; i < pddl->obj.obj_size; ++i){
            if (remap[i] != PDDL_OBJ_ID_UNDEF)
                remap[i] = idx++;
        }
        BOR_ISET_FOR_EACH(&redundant, oid){
            remap[oid] = remap[map[oid]];
            ASSERT_RUNTIME(remap[oid] >= 0);
        }
        pddlRemapObjs(pddl, remap);
        pddlNormalize(pddl);

        if (obj_map != NULL){
            for (int i = 0; i < obj_size; ++i)
                obj_map[i] = remap[obj_map[i]];
        }

        if (remap != NULL)
            BOR_FREE(remap);
    }
    borISetFree(&redundant);
    if (map != NULL)
        BOR_FREE(map);
    BOR_INFO(err, "Collapse with endomorphisms. DONE. ret: %d", ret);
    return ret;
}

static int collapseRandomPairTypeObj(pddl_t *pddl,
                                     const pddl_homomorphism_config_t *cfg,
                                     bor_rand_mt_t *rnd,
                                     pddl_obj_id_t *obj_map,
                                     int obj_size,
                                     bor_err_t *err)
{
    int choose_types[pddl->type.type_size];
    int type_size = 0;
    for (int type = 0; type < pddl->type.type_size; ++type){
        if (pddlTypesIsMinimal(&pddl->type, type)
                && pddlTypeNumObjs(&pddl->type, type) > 1){
            choose_types[type_size++] = type;
        }
    }

    if (type_size == 0)
        return -1;

    int choice = borRandMT(rnd, 0, type_size);
    int type = choose_types[choice];
    int num_objs;
    const pddl_obj_id_t *objs;
    objs = pddlTypesObjsByType(&pddl->type, type, &num_objs);
    int obj1 = objs[(int)borRandMT(rnd, 0, num_objs)];
    int obj2 = obj1;
    while (obj1 == obj2)
        obj2 = objs[(int)borRandMT(rnd, 0, num_objs)];

    int *collapse_map = BOR_CALLOC_ARR(int, pddl->obj.obj_size);
    collapse_map[obj1] = collapse_map[obj2] = 1;
    int ret = collapseObjs(pddl, collapse_map, obj_map, obj_size, err);
    BOR_FREE(collapse_map);
    return ret;
}

static int collapseRandomPairObj(pddl_t *pddl,
                                 const pddl_homomorphism_config_t *cfg,
                                 bor_rand_mt_t *rnd,
                                 pddl_obj_id_t *obj_map,
                                 int obj_size,
                                 bor_err_t *err)
{
    BOR_ISET(goal_objs);
    if (cfg->keep_goal_objs){
        collectGoalObjs(pddl, &goal_objs);
        //BOR_INFO(err, "Collected %d goal objects", borISetSize(&goal_objs));
    }

    int *choose_types = BOR_CALLOC_ARR(int, pddl->obj.obj_size);
    int *choose_objs = BOR_CALLOC_ARR(int, pddl->obj.obj_size);
    int objs_size = 0;
    for (int type = 0; type < pddl->type.type_size; ++type){
        if (pddlTypesIsMinimal(&pddl->type, type)
                && pddlTypeNumObjs(&pddl->type, type) > 1){
            int num_objs;
            const pddl_obj_id_t *objs;
            objs = pddlTypesObjsByType(&pddl->type, type, &num_objs);
            for (int i = 0; i < num_objs; ++i){
                if (!borISetIn(objs[i], &goal_objs)){
                    choose_objs[objs_size] = objs[i];
                    choose_types[objs_size++] = type;
                }
            }
        }
    }

    if (objs_size == 0){
        BOR_FREE(choose_types);
        BOR_FREE(choose_objs);
        borISetFree(&goal_objs);
        return -1;
    }

    int choice = borRandMT(rnd, 0, objs_size);
    int obj1 = choose_objs[choice];
    int type = choose_types[choice];
    int num_objs;
    const pddl_obj_id_t *objs;
    objs = pddlTypesObjsByType(&pddl->type, type, &num_objs);
    int obj2 = obj1;
    while (obj1 == obj2)
        obj2 = objs[(int)borRandMT(rnd, 0, num_objs)];

    int *collapse_map = BOR_CALLOC_ARR(int, pddl->obj.obj_size);
    collapse_map[obj1] = collapse_map[obj2] = 1;
    int ret = collapseObjs(pddl, collapse_map, obj_map, obj_size, err);
    BOR_FREE(collapse_map);
    BOR_FREE(choose_types);
    BOR_FREE(choose_objs);
    borISetFree(&goal_objs);
    return ret;
}

static int collapseType(pddl_t *pddl,
                        int type,
                        pddl_obj_id_t *obj_map,
                        int obj_size,
                        bor_err_t *err)
{
    pddl_types_t *types = &pddl->type;
    if (!pddlTypesIsMinimal(types, type)){
        BOR_ERR_RET(err, -1, "Type %d (%s) is not minimal!",
                    type, types->type[type].name);
    }
    if (pddlTypeNumObjs(types, type) <= 1){
        BOR_INFO(err, "Type %d (%s) has no more than one object:"
                      " nothing to collapse",
                 type, types->type[type].name);
        return 0;
    }

    int init_num_objs = pddl->obj.obj_size;
    int *collapse_map = BOR_CALLOC_ARR(int, pddl->obj.obj_size);
    int objs_size;
    const pddl_obj_id_t *objs = pddlTypesObjsByType(types, type, &objs_size);
    for (int i = 0; i < objs_size; ++i)
        collapse_map[objs[i]] = 1;
    int ret = collapseObjs(pddl, collapse_map, obj_map, obj_size, err);
    BOR_FREE(collapse_map);

    if (ret == 0){
        BOR_INFO(err, "Type %d (%s) collapsed. Num objs: %d -> %d",
                      type, types->type[type].name,
                      init_num_objs, pddl->obj.obj_size);
        return 0;
    }else{
        BOR_TRACE_RET(err, -1);
    }
}


static void _deduplicateCostsPart(pddl_cond_part_t *p)
{
    bor_list_t *item = borListNext(&p->part);
    while (item != &p->part){
        pddl_cond_t *c1 = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c1->type != PDDL_COND_ASSIGN){
            item = borListNext(item);
            continue;
        }
        pddl_cond_func_op_t *ass1 = PDDL_COND_CAST(c1, func_op);
        ASSERT_RUNTIME(ass1->lvalue != NULL);
        ASSERT_RUNTIME(ass1->fvalue == NULL);
        int min_value = ass1->value;

        bor_list_t *item2 = borListNext(item);
        while (item2 != &p->part){
            pddl_cond_t *c2 = BOR_LIST_ENTRY(item2, pddl_cond_t, conn);
            if (c2->type == PDDL_COND_ASSIGN){
                pddl_cond_func_op_t *ass2 = PDDL_COND_CAST(c2, func_op);
                ASSERT_RUNTIME(ass2->lvalue != NULL);
                ASSERT_RUNTIME(ass2->fvalue == NULL);
                if (pddlCondAtomCmp(ass1->lvalue, ass2->lvalue) == 0){
                    min_value = BOR_MIN(min_value, ass2->value);
                    bor_list_t *item_del = item2;
                    item2 = borListNext(item2);
                    borListDel(item_del);
                    pddlCondDel(c2);

                }else{
                    item2 = borListNext(item2);
                }

            }else{
                item2 = borListNext(item2);
            }
        }

        ass1->value = min_value;
        item = borListNext(item);
    }
}

static int _deduplicateCosts(pddl_cond_t **c, void *data)
{
    if ((*c)->type == PDDL_COND_AND || (*c)->type == PDDL_COND_OR)
        _deduplicateCostsPart(PDDL_COND_CAST(*c, part));
    return 0;
}

static pddl_cond_t *deduplicateCosts(pddl_cond_t *c)
{
    pddlCondRebuild(&c, NULL, _deduplicateCosts, NULL);
    return c;
}

static void deduplicate(pddl_t *pddl)
{
    pddl_cond_t *init = pddlCondDeduplicateAtoms(&pddl->init->cls, pddl);
    init = deduplicateCosts(init);
    pddl->init = PDDL_COND_CAST(init, part);
    pddl->goal = pddlCondDeduplicateAtoms(pddl->goal, pddl);
}

int pddlHomomorphism(pddl_t *pddl,
                     const pddl_t *src,
                     const pddl_homomorphism_config_t *cfg,
                     pddl_obj_id_t *obj_map,
                     bor_err_t *err)
{
    unsigned base_type = (cfg->type & 0x0fu);
    unsigned use_endomorph = (cfg->type & PDDL_HOMOMORPHISM_ENDOMORPHISM);

    ASSERT_RUNTIME_M(cfg->type != 0u, "Invalid configuration");
    if (base_type == PDDL_HOMOMORPHISM_TYPES
            && borISetSize(&cfg->collapse_types) == 0){
        BOR_ERR_RET2(err, -1, "Nothing to do!");
    }

    BOR_INFO_PREFIX_PUSH(err, "Homomorphism: ");
    BOR_INFO(err, "Computing homomorphism (objs: %d).", src->obj.obj_size);
    if (obj_map != NULL){
        for (int i = 0; i < src->obj.obj_size; ++i)
            obj_map[i] = i;
    }

    pddlInitCopy(pddl, src);
    if (base_type == PDDL_HOMOMORPHISM_TYPES){
        int type;
        BOR_ISET_FOR_EACH(&cfg->collapse_types, type){
            if (collapseType(pddl, type, obj_map, src->obj.obj_size, err) != 0)
                BOR_TRACE_RET(err, -1);
        }

    }else if (base_type == PDDL_HOMOMORPHISM_RAND_OBJS
                || base_type == PDDL_HOMOMORPHISM_RAND_TYPE_OBJS){
        int (*fn[2])(pddl_t *pddl,
                  const pddl_homomorphism_config_t *cfg,
                  bor_rand_mt_t *rnd,
                  pddl_obj_id_t *obj_map,
                  int obj_size,
                  bor_err_t *err) = { NULL, NULL };
        if (base_type == PDDL_HOMOMORPHISM_RAND_OBJS)
            fn[0] = fn[1] = collapseRandomPairObj;
        if (base_type == PDDL_HOMOMORPHISM_RAND_TYPE_OBJS)
            fn[0] = fn[1] = collapseRandomPairTypeObj;
        ASSERT_RUNTIME(fn[0] != NULL && fn[1] != NULL);
        int obj_size = src->obj.obj_size;
        bor_rand_mt_t *rnd = borRandMTNew(cfg->random_seed);
        int target = pddl->obj.obj_size * (1.f - cfg->rm_ratio);
        BOR_INFO(err, "Target number of objects: %d", target);

        if (use_endomorph)
            fn[0] = collapseEndomorphism;

        int fni = 0;
        while (pddl->obj.obj_size >= 1
                && pddl->obj.obj_size > target){
            if (fn[fni](pddl, cfg, rnd, obj_map, obj_size, err) != 0){
                if (fn[fni] == collapseEndomorphism){
                    BOR_INFO2(err, "Endomorphism failed -- disabling...");
                    fn[fni] = fn[(fni + 1) % 2];
                }else{
                    break;
                }
            }
            fni = (fni + 1) % 2;
        }
        borRandMTDel(rnd);
    }

    deduplicate(pddl);
    pddlNormalize(pddl);
    BOR_INFO(err, "Homomorphism computed (objs: %d, from objs: %d).",
             pddl->obj.obj_size,
             src->obj.obj_size);
    BOR_INFO_PREFIX_POP(err);
    return 0;
}
