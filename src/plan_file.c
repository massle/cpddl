/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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
#include "pddl/plan_file.h"

static void planFileFDRAddState(pddl_plan_file_fdr_t *p,
                                const pddl_fdr_t *fdr,
                                const int *state)
{
    if (p->state_size == p->state_alloc){
        if (p->state_alloc == 0)
            p->state_alloc = 4;
        p->state_alloc *= 2;
        p->state = BOR_REALLOC_ARR(p->state, int *, p->state_alloc);
    }

    p->state[p->state_size] = BOR_ALLOC_ARR(int, fdr->var.var_size);
    memcpy(p->state[p->state_size], state, sizeof(int) * fdr->var.var_size);
    ++p->state_size;
}

int pddlPlanFileFDRInit(pddl_plan_file_fdr_t *p,
                        const pddl_fdr_t *fdr,
                        const char *filename,
                        bor_err_t *err)
{
    bzero(p, sizeof(*p));
    planFileFDRAddState(p, fdr, fdr->init);

    FILE *fin;

    if ((fin = fopen(filename, "r")) == NULL)
        BOR_ERR_RET(err, -1, "Could not open file '%s'", filename);

    int ret = 0;
    int *state = BOR_ALLOC_ARR(int, fdr->var.var_size);
    size_t len = 0;
    char *line = NULL;
    ssize_t nread;
    while ((nread = getline(&line, &len, fin)) != -1){
        // Filter out comments
        for (int i = 0; i < nread; ++i){
            if (line[i] == ';'){
                line[i] = 0x0;
                break;
            }
        }
        char *s = strstr(line, "(");
        if (s == NULL)
            continue;
        char *name = s + 1;
        for (; *s != ')' && s != 0x0; ++s);
        if (*s != ')')
            continue;
        *s = 0x0;
        for (--s; s > name && *s == ' '; --s)
            *s = 0x0;

        const int *cur_state = p->state[p->state_size - 1];
        int found = 0;
        for (int op_id = 0; op_id < fdr->op.op_size; ++op_id){
            const pddl_fdr_op_t *op = fdr->op.op[op_id];
            if (strcmp(op->name, name) == 0
                    && pddlFDROpIsApplicable(op, cur_state)){
                pddlFDROpApplyOnState(op, fdr->var.var_size, cur_state, state);
                planFileFDRAddState(p, fdr, state);
                borIArrAdd(&p->op, op_id);
                p->cost += op->cost;
                found = 1;
            }
        }
        if (!found){
            ret = -1;
            BOR_ERR(err, "Could not find a matching operator for '%s'.", name);
            break;
        }
    }
    if (line != NULL)
        free(line);
    if (state != NULL)
        BOR_FREE(state);

    fclose(fin);
    return ret;
}

void pddlPlanFileFDRFree(pddl_plan_file_fdr_t *p)
{
    borIArrFree(&p->op);
    for (int i = 0; i < p->state_size; ++i)
        BOR_FREE(p->state[i]);
    if (p->state != NULL)
        BOR_FREE(p->state);
}
