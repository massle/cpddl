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

#include <stdio.h>
#include <boruvka/alloc.h>
#include <boruvka/hfunc.h>
#include "pddl/label.h"

static bor_htable_key_t htableHash(const bor_list_t *key, void *ud)
{
    pddl_label_set_t *s = BOR_LIST_ENTRY(key, pddl_label_set_t, htable);
    return s->key;
}

static int htableEq(const bor_list_t *key1, const bor_list_t *key2, void *ud)
{
    pddl_label_set_t *s1 = BOR_LIST_ENTRY(key1, pddl_label_set_t, htable);
    pddl_label_set_t *s2 = BOR_LIST_ENTRY(key2, pddl_label_set_t, htable);
    return borISetEq(&s1->label, &s2->label);
}


pddl_label_set_t *pddlLabelSetNew(const bor_iset_t *s)
{
    pddl_label_set_t *ls;

    ls = BOR_ALLOC(pddl_label_set_t);
    bzero(ls, sizeof(*ls));
    borISetUnion(&ls->label, s);
    ls->ref = 1;
    ls->key = borFastHash_64(s->s, sizeof(int) * s->size, 7583);
    borListInit(&ls->htable);
    return ls;
}

void pddlLabelSetDel(pddl_label_set_t *s)
{
    borISetFree(&s->label);
    BOR_FREE(s);
}

void pddlLabelsInitFromStripsOps(pddl_labels_t *lbs,
                                 const pddl_strips_ops_t *ops)
{
    lbs->label_size = lbs->label_alloc = ops->op_size;
    lbs->label = BOR_ALLOC_ARR(pddl_label_t, lbs->label_alloc);
    for (int op_id = 0; op_id < ops->op_size; ++op_id){
        lbs->label[op_id].op_id = op_id;
        lbs->label[op_id].cost = ops->op[op_id]->cost;
    }

    lbs->label_set = borHTableNew(htableHash, htableEq, lbs);
}

void pddlLabelsFree(pddl_labels_t *lbs)
{
    if (lbs->label != NULL)
        BOR_FREE(lbs->label);

    bor_list_t list, *item;
    borListInit(&list);
    borHTableGather(lbs->label_set, &list);
    while (!borListEmpty(&list)){
        item = borListNext(&list);
        borListDel(item);
        pddl_label_set_t *s
            = BOR_LIST_ENTRY(item, pddl_label_set_t, htable);
        pddlLabelSetDel(s);
    }

    borHTableDel(lbs->label_set);
}

pddl_label_set_t *pddlLabelsAddSet(pddl_labels_t *lbs,
                                   const bor_iset_t *labels)
{
    pddl_label_set_t *ls;
    ls = pddlLabelSetNew(labels);

    bor_list_t *found;
    if ((found = borHTableInsertUnique(lbs->label_set, &ls->htable)) == NULL){
        ls->ref = 1;
        return ls;
    }else{
        pddlLabelSetDel(ls);
        ls = BOR_LIST_ENTRY(found, pddl_label_set_t, htable);
        ls->ref += 1;
        return ls;
    }
}

void pddlLabelsSetDecRef(pddl_labels_t *lbs, pddl_label_set_t *set)
{
    if (--set->ref == 0){
        borHTableErase(lbs->label_set, &set->htable);
        pddlLabelSetDel(set);
    }
}

