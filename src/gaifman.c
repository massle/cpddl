/***
 * Copyright (c)2023 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "pddl/gaifman.h"
#include "internal.h"

void pddlGaifmanInit(pddl_gaifman_t *g, int obj_size)
{
    ZEROIZE(g);
    g->obj_size = obj_size;
    g->obj_relate_to = ZALLOC_ARR(pddl_iset_t, g->obj_size);
    g->distance = ALLOC_ARR(int, g->obj_size * g->obj_size);
    g->distance_dirty = 1;
}

void pddlGaifmanFree(pddl_gaifman_t *g)
{
    for (int i = 0; i < g->obj_size; ++i)
        pddlISetFree(g->obj_relate_to + i);
    if (g->obj_relate_to != NULL)
        FREE(g->obj_relate_to);
    if (g->distance != NULL)
        FREE(g->distance);
}

void pddlGaifmanAddRelation(pddl_gaifman_t *g, int obj1, int obj2)
{
    pddlISetAdd(g->obj_relate_to + obj1, obj2);
    pddlISetAdd(g->obj_relate_to + obj2, obj1);
    g->distance_dirty = 1;
}

void pddlGaifmanAddRelationsFromAtom(pddl_gaifman_t *g,
                                     const pddl_fm_atom_t *atom)
{
    pddlGaifmanAddRelationsFromFm(g, &atom->fm);
}

static int addRelationFromFm(pddl_fm_t *fm, void *_g)
{
    pddl_gaifman_t *g = _g;
    if (pddlFmIsAtom(fm)){
        pddl_fm_atom_t *a = pddlFmToAtom(fm);
        for (int ai = 0; ai < a->arg_size; ++ai){
            if (a->arg[ai].param >= 0)
                continue;
            for (int ai2 = ai + 1; ai2 < a->arg_size; ++ai2){
                if (a->arg[ai2].param >= 0)
                    continue;
                pddlGaifmanAddRelation(g, a->arg[ai].obj, a->arg[ai2].obj);
            }
        }
    }
    return 0;
}

void pddlGaifmanAddRelationsFromFm(pddl_gaifman_t *g, const pddl_fm_t *fm)
{
    pddlFmTraverse((pddl_fm_t *)fm, NULL, addRelationFromFm, g);
}

static void pddlGaifmanComputeDistances(pddl_gaifman_t *g)
{
    int arr_size = g->obj_size * g->obj_size;
    for (int i = 0; i < arr_size; ++i)
        g->distance[i] = INT_MAX / 2;

    for (int obj1 = 0; obj1 < g->obj_size; ++obj1){
        g->distance[obj1 * g->obj_size + obj1] = 0;

        int obj2;
        PDDL_ISET_FOR_EACH(g->obj_relate_to + obj1, obj2)
            g->distance[obj1 * g->obj_size + obj2] = 1;
    }

    for (int obj1 = 0; obj1 < g->obj_size; ++obj1){
        for (int obj2 = 0; obj2 < g->obj_size; ++obj2){
            for (int obj3 = 0; obj3 < g->obj_size; ++obj3){
                int d23 = g->distance[obj2 * g->obj_size + obj3];
                int d21 = g->distance[obj2 * g->obj_size + obj1];
                int d13 = g->distance[obj1 * g->obj_size + obj3];
                g->distance[obj2 * g->obj_size + obj3] = PDDL_MIN(d23, d21 + d13);
            }
        }
    }
    for (int i = 0; i < arr_size; ++i){
        if (g->distance[i] >= INT_MAX / 2)
            g->distance[i] = -1;
    }
    g->distance_dirty = 0;
}

int pddlGaifmanDistance(pddl_gaifman_t *g, int obj1, int obj2)
{
    if (g->distance_dirty)
        pddlGaifmanComputeDistances(g);
    return g->distance[obj1 * g->obj_size + obj2];
}

int pddlGaifmanDiameter(pddl_gaifman_t *g)
{
    if (g->distance_dirty)
        pddlGaifmanComputeDistances(g);

    int diameter = 0;
    for (int obj1 = 0; obj1 < g->obj_size; ++obj1){
        for (int obj2 = 0; obj2 < g->obj_size; ++obj2){
            if (g->distance[obj1 * g->obj_size + obj2] < 0)
                return -1;
            diameter = PDDL_MAX(g->distance[obj1 * g->obj_size + obj2], diameter);
        }
    }

    return diameter;
}

struct collect_objs_params {
    pddl_iset_t *objs;
    pddl_iset_t *params;
};

static int fmCollectObjsAndParams(pddl_fm_t *fm, void *_col)
{
    struct collect_objs_params *col = _col;
    pddl_iset_t *objs = col->objs;
    pddl_iset_t *params = col->params;
    if (pddlFmIsAtom(fm)){
        pddl_fm_atom_t *a = pddlFmToAtom(fm);
        for (int ai = 0; ai < a->arg_size; ++ai){
            if (a->arg[ai].param >= 0){
                pddlISetAdd(params, a->arg[ai].param);
            }else{
                pddlISetAdd(objs, a->arg[ai].obj);
            }
        }
    }
    return 0;
}

struct param_obj_maps {
    pddl_gaifman_t *g;
    int *param_map;
    int *obj_map;
};

static int fmAddRelationsParamObj(pddl_fm_t *fm, void *_maps)
{
    struct param_obj_maps *maps = _maps;
    pddl_gaifman_t *g = maps->g;
    const int *param_map = maps->param_map;
    const int *obj_map = maps->obj_map;
    if (pddlFmIsAtom(fm)){
        pddl_fm_atom_t *a = pddlFmToAtom(fm);
        for (int ai = 0; ai < a->arg_size; ++ai){
            int id1 = 0;
            if (a->arg[ai].param >= 0){
                id1 = param_map[a->arg[ai].param];
            }else{
                id1 = obj_map[a->arg[ai].obj];
            }

            for (int ai2 = ai + 1; ai2 < a->arg_size; ++ai2){
                int id2 = 0;
                if (a->arg[ai].param >= 0){
                    id2 = param_map[a->arg[ai2].param];
                }else{
                    id2 = obj_map[a->arg[ai2].obj];
                }
                pddlGaifmanAddRelation(g, id1, id2);
            }
        }
    }
    return 0;
}

int pddlGaifmanActionPreDiameter(const pddl_action_t *a)
{
    PDDL_ISET(objs);
    PDDL_ISET(params);
    struct collect_objs_params col = { &objs, &params };
    pddlFmTraverse((pddl_fm_t *)a->pre, NULL, fmCollectObjsAndParams, &col);

    if (pddlISetSize(&objs) + pddlISetSize(&params) == 0){
        pddlISetFree(&objs);
        pddlISetFree(&params);
        return 0;
    }

    int *param_map = NULL;
    if (pddlISetSize(&params) > 0){
        int size = pddlISetSize(&params);
        param_map = ALLOC_ARR(int, pddlISetGet(&params, size - 1) + 1);
        int idx = 0;
        int param_id;
        PDDL_ISET_FOR_EACH(&params, param_id)
            param_map[param_id] = idx++;
    }

    int *obj_map = NULL;
    if (pddlISetSize(&objs) > 0){
        int size = pddlISetSize(&objs);
        obj_map = ALLOC_ARR(int, pddlISetGet(&objs, size - 1) + 1);
        int idx = pddlISetSize(&params);
        int obj_id;
        PDDL_ISET_FOR_EACH(&objs, obj_id)
            obj_map[obj_id] = idx++;
    }

    pddl_gaifman_t g;
    pddlGaifmanInit(&g, pddlISetSize(&params) + pddlISetSize(&objs));

    struct param_obj_maps maps = { &g, param_map, obj_map };
    pddlFmTraverse((pddl_fm_t *)a->pre, NULL, fmAddRelationsParamObj, &maps);

    int diameter = pddlGaifmanDiameter(&g);
    pddlGaifmanFree(&g);

    if (obj_map != NULL)
        FREE(obj_map);
    if (param_map != NULL)
        FREE(param_map);
    pddlISetFree(&objs);
    pddlISetFree(&params);

    return diameter;
}
