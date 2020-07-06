/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/abstr_cascading_table.h"

#define PRUNED -1
#define PDDL_ABSTR_CASCADING_TABLE_LEAF 0
#define PDDL_ABSTR_CASCADING_TABLE_MERGE 1

struct pddl_abstr_cascading_table {
    int type;
    int size;
    int *lookup_table;
};

struct pddl_abstr_cascading_table_leaf {
    pddl_abstr_cascading_table_t cascading_table;
    int id;
};
typedef struct pddl_abstr_cascading_table_leaf
            pddl_abstr_cascading_table_leaf_t;

struct pddl_abstr_cascading_table_merge {
    pddl_abstr_cascading_table_t cascading_table;
    pddl_abstr_cascading_table_t *left;
    pddl_abstr_cascading_table_t *right;
};
typedef struct pddl_abstr_cascading_table_merge
            pddl_abstr_cascading_table_merge_t;


#define LEAF(X, T) \
    pddl_abstr_cascading_table_leaf_t *X = \
        bor_container_of((T), pddl_abstr_cascading_table_leaf_t, \
                         cascading_table)
#define MERGE(X, T) \
    pddl_abstr_cascading_table_merge_t *X = \
        bor_container_of((T), pddl_abstr_cascading_table_merge_t, \
                         cascading_table)

#define MERGE_IDX(M, LEFT, RIGHT) ((LEFT) * (M)->left->size + (RIGHT))

static void delLeaf(pddl_abstr_cascading_table_t *t);
static void delMerge(pddl_abstr_cascading_table_t *t);

static pddl_abstr_cascading_table_t *
    cloneLeaf(const pddl_abstr_cascading_table_t *t);
static pddl_abstr_cascading_table_t *
    cloneMerge(const pddl_abstr_cascading_table_t *t);

static int valueFromStateLeaf(pddl_abstr_cascading_table_t *_t,
                              const int *state);
static int valueFromStateMerge(pddl_abstr_cascading_table_t *_t,
                               const int *state);


static void initTable(pddl_abstr_cascading_table_t *, int type, int size);
static void copyTable(pddl_abstr_cascading_table_t *t,
                      const pddl_abstr_cascading_table_t *src);
                      

struct methods {
    void (*delete)(pddl_abstr_cascading_table_t *t);
    pddl_abstr_cascading_table_t *
            (*clone)(const pddl_abstr_cascading_table_t *t);
    int (*value_from_state)(pddl_abstr_cascading_table_t *_t,
                            const int *state);
};

struct methods methods[2] = {
    {
        delLeaf, /* .delete */
        cloneLeaf, /* .clone */
        valueFromStateLeaf, /* .value_from_state */
    },
    {
        delMerge, /* .delete */
        cloneMerge, /* .clone */
        valueFromStateMerge, /* .value_from_state */
    }
};

void pddlAbstrCascadingTableDel(pddl_abstr_cascading_table_t *t)
{
    methods[t->type].delete(t);

    if (t->lookup_table != NULL)
        BOR_FREE(t->lookup_table);
}

static void delLeaf(pddl_abstr_cascading_table_t *_t)
{
    LEAF(t, _t);
    BOR_FREE(t);
}

static void delMerge(pddl_abstr_cascading_table_t *_t)
{
    MERGE(t, _t);
    pddlAbstrCascadingTableDel(t->left);
    pddlAbstrCascadingTableDel(t->right);
    BOR_FREE(t);
}


pddl_abstr_cascading_table_t *
    pddlAbstrCascadingTableClone(const pddl_abstr_cascading_table_t *t)
{
    return methods[t->type].clone(t);
}

static pddl_abstr_cascading_table_t *
    cloneLeaf(const pddl_abstr_cascading_table_t *_t)
{
    const LEAF(t, _t);
    pddl_abstr_cascading_table_leaf_t *out;
   
    out = BOR_ALLOC(pddl_abstr_cascading_table_leaf_t);
    bzero(out, sizeof(*out));
    copyTable(&out->cascading_table, &t->cascading_table);
    out->id = t->id;

    return &out->cascading_table;
}

static pddl_abstr_cascading_table_t *
    cloneMerge(const pddl_abstr_cascading_table_t *_t)
{
    MERGE(t, _t);
    pddl_abstr_cascading_table_merge_t *out;
   
    out = BOR_ALLOC(pddl_abstr_cascading_table_merge_t);
    bzero(out, sizeof(*out));
    copyTable(&out->cascading_table, &t->cascading_table);
    out->left = pddlAbstrCascadingTableClone(t->left);
    out->right = pddlAbstrCascadingTableClone(t->right);
    return &out->cascading_table;
}

pddl_abstr_cascading_table_t *pddlAbstrCascadingTableNewLeaf(int id, int size)
{
    pddl_abstr_cascading_table_leaf_t *t;
    t = BOR_ALLOC(pddl_abstr_cascading_table_leaf_t);
    bzero(t, sizeof(*t));
    initTable(&t->cascading_table, PDDL_ABSTR_CASCADING_TABLE_LEAF, size);
    t->id = id;

    return &t->cascading_table;
}

pddl_abstr_cascading_table_t *
    pddlAbstrCascadingTableMerge(pddl_abstr_cascading_table_t *t1,
                                 pddl_abstr_cascading_table_t *t2)
{
    pddl_abstr_cascading_table_merge_t *m;
    m = BOR_ALLOC(pddl_abstr_cascading_table_merge_t);
    bzero(m, sizeof(*m));
    int size = t1->size * t2->size;
    initTable(&m->cascading_table, PDDL_ABSTR_CASCADING_TABLE_MERGE, size);
    m->left = pddlAbstrCascadingTableClone(t1);
    m->right = pddlAbstrCascadingTableClone(t2);

    return &m->cascading_table;
}

void pddlAbstrCascadingTableAbstract(pddl_abstr_cascading_table_t *t,
                                     const bor_iarr_t *abstraction)
{
    int new_size = 0;
    int ab_size = borIArrSize(abstraction);
    for (int i = 0; i < t->size && i < ab_size; ++i){
        int new_val = borIArrGet(abstraction, i);
        if (t->lookup_table[i] != PRUNED){
            if (new_val < 0){
                t->lookup_table[i] = PRUNED;
            }else{
                t->lookup_table[i] = borIArrGet(abstraction, i);
                new_size = BOR_MAX(new_size, t->lookup_table[i]);
            }
        }
    }
    t->size = new_size;
}

int pddlAbstrCascadingTableValueFromState(pddl_abstr_cascading_table_t *t,
                                          const int *state)
{
    return methods[t->type].value_from_state(t, state);
}

static int valueFromStateLeaf(pddl_abstr_cascading_table_t *_t,
                              const int *state)
{
    LEAF(t, _t);
    return _t->lookup_table[state[t->id]];
}

static int valueFromStateMerge(pddl_abstr_cascading_table_t *_t,
                               const int *state)
{
    MERGE(t, _t);
    int vleft = pddlAbstrCascadingTableValueFromState(t->left, state);
    int vright = pddlAbstrCascadingTableValueFromState(t->right, state);
    return _t->lookup_table[MERGE_IDX(t, vleft, vright)];
}

static void initTable(pddl_abstr_cascading_table_t *t, int type, int size)
{
    t->type = type;
    t->size = size;
    t->lookup_table = BOR_CALLOC_ARR(int, size);
    for (int i = 0; i < t->size; ++i)
        t->lookup_table[i] = i;
}

static void copyTable(pddl_abstr_cascading_table_t *t,
                      const pddl_abstr_cascading_table_t *src)
{
    *t = *src;
    t->lookup_table = BOR_CALLOC_ARR(int, t->size);
    memcpy(t->lookup_table, src->lookup_table, sizeof(int) * t->size);
}
