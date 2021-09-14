/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
 * Saarland University, and
 * Czech Technical University in Prague.
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
#include "pddl/datalog.h"
#include "assert.h"

struct pddl_datalog_const {
    unsigned id;
    int idx;
    char *name;
};
typedef struct pddl_datalog_const pddl_datalog_const_t;

struct pddl_datalog_var {
    unsigned id;
    int idx;
    char *name;
};
typedef struct pddl_datalog_var pddl_datalog_var_t;

struct pddl_datalog_pred {
    unsigned id;
    int idx;
    int arity;
    char *name;
};
typedef struct pddl_datalog_pred pddl_datalog_pred_t;

struct pddl_datalog {
    pddl_datalog_const_t *c;
    int c_size;
    int c_alloc;

    pddl_datalog_var_t *var;
    int var_size;
    int var_alloc;

    pddl_datalog_pred_t *pred;
    int pred_size;
    int pred_alloc;

    pddl_datalog_clause_t *clause;
    int clause_size;
    int clause_alloc;
};

#define MASK 0x7u
#define MASK_LEN 3u
#define CONST_MASK 0x1u
#define PRED_MASK 0x2u
#define VAR_MASK 0x3u
#define TO_IDX(v) ((v)>>MASK_LEN)
#define IDX_TO_CONST(v) (((v)<<MASK_LEN) | CONST_MASK)
#define IS_CONST(v) (((v) & MASK) == CONST_MASK)
#define IDX_TO_PRED(v) (((v)<<MASK_LEN) | PRED_MASK)
#define IS_PRED(v) (((v) & MASK) == PRED_MASK)
#define IDX_TO_VAR(v) (((v)<<MASK_LEN) | VAR_MASK)
#define IS_VAR(v) (((v) & MASK) == VAR_MASK)

pddl_datalog_t *pddlDatalogNew(void)
{
    pddl_datalog_t *dl = BOR_ALLOC(pddl_datalog_t);
    bzero(dl, sizeof(*dl));
    return dl;
}

void pddlDatalogDel(pddl_datalog_t *dl)
{
    for (int i = 0; i < dl->clause_size; ++i)
        pddlDatalogClauseFree(dl, dl->clause + i);
    if (dl->clause != NULL)
        BOR_FREE(dl->clause);

    for (int i = 0; i < dl->c_size; ++i){
        if (dl->c[i].name != NULL)
            BOR_FREE(dl->c[i].name);
    }
    if (dl->c != NULL)
        BOR_FREE(dl->c);

    for (int i = 0; i < dl->var_size; ++i){
        if (dl->var[i].name != NULL)
            BOR_FREE(dl->var[i].name);
    }
    if (dl->var != NULL)
        BOR_FREE(dl->var);

    for (int i = 0; i < dl->pred_size; ++i){
        if (dl->pred[i].name != NULL)
            BOR_FREE(dl->pred[i].name);
    }
    if (dl->pred != NULL)
        BOR_FREE(dl->pred);
    BOR_FREE(dl);
}

unsigned pddlDatalogAddConst(pddl_datalog_t *dl, const char *name)
{
    if (dl->c_size == dl->c_alloc){
        if (dl->c_alloc == 0)
            dl->c_alloc = 1;
        dl->c_alloc *= 2;
        dl->c = BOR_REALLOC_ARR(dl->c, pddl_datalog_const_t, dl->c_alloc);
    }
    pddl_datalog_const_t *c = dl->c + dl->c_size;
    c->idx = dl->c_size++;
    c->id = IDX_TO_CONST(c->idx);
    c->name = NULL;
    if (name != NULL)
        c->name = BOR_STRDUP(name);
    return c->id;
}

unsigned pddlDatalogAddPred(pddl_datalog_t *dl, int arity, const char *name)
{
    if (dl->pred_size == dl->pred_alloc){
        if (dl->pred_alloc == 0)
            dl->pred_alloc = 1;
        dl->pred_alloc *= 2;
        dl->pred = BOR_REALLOC_ARR(dl->pred, pddl_datalog_pred_t,
                                   dl->pred_alloc);
    }
    pddl_datalog_pred_t *p = dl->pred + dl->pred_size;
    p->idx = dl->pred_size++;
    p->id = IDX_TO_PRED(p->idx);
    p->arity = arity;
    p->name = NULL;
    if (name != NULL)
        p->name = BOR_STRDUP(name);
    return p->id;
}

unsigned pddlDatalogAddVar(pddl_datalog_t *dl, const char *name)
{
    if (dl->var_size == dl->var_alloc){
        if (dl->var_alloc == 0)
            dl->var_alloc = 1;
        dl->var_alloc *= 2;
        dl->var = BOR_REALLOC_ARR(dl->var, pddl_datalog_var_t, dl->var_alloc);
    }
    pddl_datalog_var_t *v = dl->var + dl->var_size;
    v->idx = dl->var_size++;
    v->id = IDX_TO_VAR(v->idx);
    v->name = NULL;
    if (name != NULL)
        v->name = BOR_STRDUP(name);
    return v->id;
}

int pddlDatalogAddClause(pddl_datalog_t *dl, const pddl_datalog_clause_t *cl)
{
    if (dl->clause_size == dl->clause_alloc){
        if (dl->clause_alloc == 0)
            dl->clause_alloc = 1;
        dl->clause_alloc *= 2;
        dl->clause = BOR_REALLOC_ARR(dl->clause, pddl_datalog_clause_t,
                                     dl->clause_alloc);
    }
    pddl_datalog_clause_t *clause = dl->clause + dl->clause_size++;
    pddlDatalogClauseCopy(dl, clause, cl);
    return 0;
}

int pddlDatalogToNormalForm(pddl_datalog_t *dl)
{
    // TODO
    return -1;
}

void pddlDatalogAtomInit(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *atom,
                         unsigned pred)
{
    int p = TO_IDX(pred);
    atom->pred = pred;
    atom->arg = BOR_CALLOC_ARR(unsigned, dl->pred[p].arity);
}

void pddlDatalogAtomCopy(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *dst,
                         const pddl_datalog_atom_t *src)
{
    int p = TO_IDX(src->pred);
    dst->pred = src->pred;
    dst->arg = BOR_CALLOC_ARR(unsigned, dl->pred[p].arity);
    memcpy(dst->arg, src->arg, sizeof(unsigned) * dl->pred[p].arity);
}

void pddlDatalogAtomFree(pddl_datalog_t *dl, pddl_datalog_atom_t *atom)
{
    if (atom->arg != NULL)
        BOR_FREE(atom->arg);
}

void pddlDatalogAtomSetArg(pddl_datalog_t *dl,
                           pddl_datalog_atom_t *atom,
                           int argi,
                           unsigned term)
{
    ASSERT(argi < dl->pred[TO_IDX(atom->pred)].arity);
    atom->arg[argi] = term;
}

void pddlDatalogClauseInit(pddl_datalog_t *dl, pddl_datalog_clause_t *clause)
{
    bzero(clause, sizeof(*clause));
}

void pddlDatalogClauseCopy(pddl_datalog_t *dl,
                           pddl_datalog_clause_t *dst,
                           const pddl_datalog_clause_t *src)
{
    pddlDatalogAtomCopy(dl, &dst->head, &src->head);
    dst->body_alloc = src->body_alloc;
    dst->body_size = src->body_size;
    dst->body = BOR_ALLOC_ARR(pddl_datalog_atom_t, dst->body_alloc);
    for (int i = 0; i < dst->body_size; ++i)
        pddlDatalogAtomCopy(dl, dst->body + i, src->body + i);
}

void pddlDatalogClauseFree(pddl_datalog_t *dl, pddl_datalog_clause_t *clause)
{
    pddlDatalogAtomFree(dl, &clause->head);
    for (int i = 0; i < clause->body_size; ++i)
        pddlDatalogAtomFree(dl, &clause->body[i]);
    if (clause->body != NULL)
        BOR_FREE(clause->body);
}

void pddlDatalogClauseSetHead(pddl_datalog_t *dl,
                              pddl_datalog_clause_t *clause,
                              const pddl_datalog_atom_t *head)
{
    pddlDatalogAtomFree(dl, &clause->head);
    pddlDatalogAtomCopy(dl, &clause->head, head);
}

void pddlDatalogClauseAddBody(pddl_datalog_t *dl,
                              pddl_datalog_clause_t *clause,
                              const pddl_datalog_atom_t *atom)
{
    if (clause->body_size == clause->body_alloc){
        if (clause->body_alloc == 0)
            clause->body_alloc = 1;
        clause->body_alloc *= 2;
        clause->body = BOR_REALLOC_ARR(clause->body, pddl_datalog_atom_t,
                                       clause->body_alloc);
    }
    pddl_datalog_atom_t *a = clause->body + clause->body_size++;
    pddlDatalogAtomCopy(dl, a, atom);
}

static void printEl(const pddl_datalog_t *dl, unsigned id, FILE *fout)
{
    int idx = TO_IDX(id);
    if (IS_CONST(id)){
        if (dl->c[idx].name != NULL){
            fprintf(fout, "%s", dl->c[idx].name);
        }else{
            fprintf(fout, "c%d", idx);
        }

    }else if (IS_VAR(id)){
        if (dl->var[idx].name != NULL){
            fprintf(fout, "%s", dl->var[idx].name);
        }else{
            fprintf(fout, "v%d", idx);
        }

    }else if (IS_PRED(id)){
        if (dl->pred[idx].name != NULL){
            fprintf(fout, "%s", dl->pred[idx].name);
        }else{
            fprintf(fout, "P%d", idx);
        }
    }
}

static void printAtom(const pddl_datalog_t *dl,
                      const pddl_datalog_atom_t *atom,
                      FILE *fout)
{
    printEl(dl, atom->pred, fout);
    int p = TO_IDX(atom->pred);
    fprintf(fout, "(");
    for (int i = 0; i < dl->pred[p].arity; ++i){
        if (i > 0)
            fprintf(fout, ", ");
        printEl(dl, atom->arg[i], fout);
    }
    fprintf(fout, ")");
}

void pddlDatalogPrint(const pddl_datalog_t *dl, FILE *fout)
{
    for (int ci = 0; ci < dl->clause_size; ++ci){
        const pddl_datalog_clause_t *c = dl->clause + ci;
        printAtom(dl, &c->head, fout);
        if (c->body_size > 0){
            fprintf(fout, " :- ");
            printAtom(dl, c->body + 0, fout);
            for (int i = 1; i < c->body_size; ++i){
                fprintf(fout, ", ");
                printAtom(dl, c->body + i, fout);
            }
        }
        fprintf(fout, ".\n");
    }
}
