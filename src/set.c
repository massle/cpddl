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

void pddlISetPrintCompressed(const bor_iset_t *set, FILE *fout)
{
    for (int i = 0; i < borISetSize(set); ++i){
        if (i != 0)
            fprintf(fout, " ");
        if (i == borISetSize(set) - 1){
            fprintf(fout, "%d", borISetGet(set, i));
        }else{
            int j;
            for (j = i + 1; j < borISetSize(set)
                    && borISetGet(set, j) == borISetGet(set, j - 1) + 1;
                    ++j);
            if (j - 1 == i){
                fprintf(fout, "%d", borISetGet(set, i));
            }else{
                fprintf(fout, "%d-%d",
                        borISetGet(set, i), borISetGet(set, j - 1));
            }
            i = j - 1;
        }
    }
}

void pddlISetPrint(const bor_iset_t *set, FILE *fout)
{
    int not_first = 0;
    int v;
    BOR_ISET_FOR_EACH(set, v){
        if (not_first)
            fprintf(fout, " ");
        fprintf(fout, "%d", v);
        not_first = 1;
    }
}

void pddlISetPrintln(const bor_iset_t *set, FILE *fout)
{
    pddlISetPrint(set, fout);
    fprintf(fout, "\n");
}
