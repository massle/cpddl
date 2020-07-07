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
#include <boruvka/sort.h>
#include "pddl/labeled_transition.h"

void pddlLabeledTransitionsSetInit(pddl_labeled_transitions_set_t *t)
{
    bzero(t, sizeof(*t));
}

void pddlLabeledTransitionsSetFree(pddl_labeled_transitions_set_t *t)
{
    for (int i = 0; i < t->trans_size; ++i){
        pddlTransitionsFree(&t->trans[i].trans);
    }
    if (t->trans != NULL)
        BOR_FREE(t->trans);
}

int pddlLabeledTransitionsSetAdd(pddl_labeled_transitions_set_t *t,
                                 pddl_trans_system_label_set_t *label,
                                 int from,
                                 int to)
{
    for (int i = 0; i < t->trans_size; ++i){
        if (t->trans[i].label == label){
            pddlTransitionsAdd(&t->trans[i].trans, from, to);
            return 1;
        }
    }

    if (t->trans_size == t->trans_alloc){
        if (t->trans_alloc == 0)
            t->trans_alloc = 1;
        t->trans_alloc *= 2;
        t->trans = BOR_REALLOC_ARR(t->trans, pddl_labeled_transitions_t,
                                   t->trans_alloc);
    }
    pddl_labeled_transitions_t *tr = t->trans + t->trans_size++;
    tr->label = label;
    pddlTransitionsInit(&tr->trans);
    pddlTransitionsAdd(&tr->trans, from, to);
    return 0;
}

static int cmp(const void *a, const void *b, void *arg)
{
    const pddl_labeled_transitions_t *t1 = a;
    const pddl_labeled_transitions_t *t2 = b;
    return borISetCmp(&t1->label->label, &t2->label->label);
}

void pddlLabeledTransitionsSetSort(pddl_labeled_transitions_set_t *t)
{
    borSort(t->trans, t->trans_size, sizeof(pddl_labeled_transitions_t),
            cmp, NULL);
    for (int i = 0; i < t->trans_size; ++i)
        pddlTransitionsSort(&t->trans[i].trans);
}
