/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/set.h"
#include "internal.h"

static void _genAllSubsetsRec(pddl_set_iset_t *ss,
                              const pddl_iset_t *set,
                              int cur_idx,
                              int subset_size,
                              int *subset,
                              int subset_pos)
{
    subset[subset_pos] = pddlISetGet(set, cur_idx);
    if (subset_pos == subset_size - 1){
        PDDL_ISET(newset);
        for (int i = 0; i < subset_size; ++i)
            pddlISetAdd(&newset, subset[i]);
        pddlSetISetAdd(ss, &newset);
        pddlISetFree(&newset);
        return;
    }

    for (int idx = cur_idx + 1;
            idx <= pddlISetSize(set) - subset_size + subset_pos + 1; ++idx){
        _genAllSubsetsRec(ss, set, idx, subset_size, subset, subset_pos + 1);
    }

}

static void genAllSubsetsRec(pddl_set_iset_t *ss,
                             const pddl_iset_t *set,
                             int subset_size)
{
    ASSERT(subset_size < pddlISetSize(set));
    int subset[subset_size];

    for (int idx = 0; idx <= pddlISetSize(set) - subset_size; ++idx)
        _genAllSubsetsRec(ss, set, idx, subset_size, subset, 0);
}

static void genAllSubsets(pddl_set_iset_t *ss,
                          const pddl_iset_t *set,
                          int min_size)
{
    if (min_size >= pddlISetSize(set)){
        return;

    }else if (min_size == 0){
        PDDL_ISET(newset);
        pddlSetISetAdd(ss, &newset);
        pddlISetFree(&newset);
        genAllSubsets(ss, set, 1);

    }else if (min_size == 1){
        PDDL_ISET(newset);
        int el;
        PDDL_ISET_FOR_EACH(set, el){
            pddlISetEmpty(&newset);
            pddlISetAdd(&newset, el);
            pddlSetISetAdd(ss, &newset);
        }
        pddlISetFree(&newset);
        genAllSubsets(ss, set, 2);

    }else if (min_size == 2){
        PDDL_ISET(newset);
        int set_size = pddlISetSize(set);
        for (int i = 0; i < set_size - 1; ++i){
            int el1 = pddlISetGet(set, i);
            for (int j = i + 1; j < set_size; ++j){
                int el2 = pddlISetGet(set, j);
                PDDL_ISET_SET(&newset, el1, el2);
                pddlSetISetAdd(ss, &newset);
            }
        }
        pddlISetFree(&newset);
        genAllSubsets(ss, set, 3);

    }else if (min_size == 3){
        PDDL_ISET(newset);
        int set_size = pddlISetSize(set);
        for (int i = 0; i < set_size - 2; ++i){
            int el1 = pddlISetGet(set, i);
            for (int j = i + 1; j < set_size - 1; ++j){
                int el2 = pddlISetGet(set, j);
                for (int k = j + 1; k < set_size; ++k){
                    int el3 = pddlISetGet(set, k);
                    PDDL_ISET_SET(&newset, el1, el2, el3);
                    pddlSetISetAdd(ss, &newset);
                }
            }
        }
        pddlISetFree(&newset);
        genAllSubsets(ss, set, 4);

    }else{
        for (int size = min_size; size < pddlISetSize(set); ++size)
            genAllSubsetsRec(ss, set, size);
    }
}

void pddlSetISetGenAllSubsets(pddl_set_iset_t *ss, int min_size)
{
    int input_size = pddlSetISetSize(ss);
    for (int seti = 0; seti < input_size; ++seti){
        const pddl_iset_t *set = pddlSetISetGet(ss, seti);
        if (pddlISetSize(set) > min_size)
            genAllSubsets(ss, set, min_size);
    }
}

void pddlISetPrintCompressed(const pddl_iset_t *set, FILE *fout)
{
    for (int i = 0; i < pddlISetSize(set); ++i){
        if (i != 0)
            fprintf(fout, " ");
        if (i == pddlISetSize(set) - 1){
            fprintf(fout, "%d", pddlISetGet(set, i));
        }else{
            int j;
            for (j = i + 1; j < pddlISetSize(set)
                    && pddlISetGet(set, j) == pddlISetGet(set, j - 1) + 1;
                    ++j);
            if (j - 1 == i){
                fprintf(fout, "%d", pddlISetGet(set, i));
            }else{
                fprintf(fout, "%d-%d",
                        pddlISetGet(set, i), pddlISetGet(set, j - 1));
            }
            i = j - 1;
        }
    }
}

void pddlISetPrint(const pddl_iset_t *set, FILE *fout)
{
    int not_first = 0;
    int v;
    PDDL_ISET_FOR_EACH(set, v){
        if (not_first)
            fprintf(fout, " ");
        fprintf(fout, "%d", v);
        not_first = 1;
    }
}

void pddlISetPrintln(const pddl_iset_t *set, FILE *fout)
{
    pddlISetPrint(set, fout);
    fprintf(fout, "\n");
}
