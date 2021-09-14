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

#ifndef __PDDL_DATALOG_H__
#define __PDDL_DATALOG_H__

#include <stdio.h>
#include <pddl/common.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_datalog_atom {
    unsigned pred;
    unsigned *arg;
};
typedef struct pddl_datalog_atom pddl_datalog_atom_t;

struct pddl_datalog_clause {
    pddl_datalog_atom_t head;
    pddl_datalog_atom_t *body;
    int body_size;
    int body_alloc;
};
typedef struct pddl_datalog_clause pddl_datalog_clause_t;

typedef struct pddl_datalog pddl_datalog_t;

/**
 * Creates an empty datalog program.
 */
pddl_datalog_t *pddlDatalogNew(void);

/**
 * Deletes allocated memory.
 */
void pddlDatalogDel(pddl_datalog_t *dl);

/**
 * Adds constant to the datalog program.
 */
unsigned pddlDatalogAddConst(pddl_datalog_t *dl, const char *name);

/**
 * Adds predicate to the datalog program.
 */
unsigned pddlDatalogAddPred(pddl_datalog_t *dl, int arity, const char *name);

/**
 * Adds variable to the datalog program.
 */
unsigned pddlDatalogAddVar(pddl_datalog_t *dl, const char *name);

/**
 * Adds clause to the datalog.
 */
int pddlDatalogAddClause(pddl_datalog_t *dl, const pddl_datalog_clause_t *cl);

/**
 * Transforms the datalog to the normal form according to
 * Helmert, M. (2009). Concise finite-domain representations for PDDL
 * planning tasks. Artificial Intelligence, 173, 503–535.
 *
 * That is, each rule can have at most two atoms in the body, all variables
 * in the head are in the body, and all variables not in the head are in
 * both atoms in the body.
 *
 * Return 0 on success, -1 if the normal form could not be created.
 */
int pddlDatalogToNormalForm(pddl_datalog_t *dl);

/**
 * Initializes atom of the given predicate previously created with
 * pddlDatalogAddPred().
 */
void pddlDatalogAtomInit(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *atom,
                         unsigned pred);

/**
 * Deep copy of the atom
 */
void pddlDatalogAtomCopy(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *dst,
                         const pddl_datalog_atom_t *src);

/**
 * Free atom structure
 */
void pddlDatalogAtomFree(pddl_datalog_t *dl, pddl_datalog_atom_t *atom);

/**
 * Set argi'th argument of the atom to the given term which must be created
 * with pddlDatalogAdd{Var,Const}()
 */
void pddlDatalogAtomSetArg(pddl_datalog_t *dl,
                           pddl_datalog_atom_t *atom,
                           int argi,
                           unsigned term);

/**
 * Initializes empty clause.
 */
void pddlDatalogClauseInit(pddl_datalog_t *dl, pddl_datalog_clause_t *clause);

/**
 * Deep copy of the clause.
 */
void pddlDatalogClauseCopy(pddl_datalog_t *dl,
                           pddl_datalog_clause_t *dst,
                           const pddl_datalog_clause_t *src);

/**
 * Free clause structure.
 */
void pddlDatalogClauseFree(pddl_datalog_t *dl, pddl_datalog_clause_t *clause);

/**
 * Set head of the clause.
 */
void pddlDatalogClauseSetHead(pddl_datalog_t *dl,
                              pddl_datalog_clause_t *clause,
                              const pddl_datalog_atom_t *head);

/**
 * Adds a body atom to the clause.
 */
void pddlDatalogClauseAddBody(pddl_datalog_t *dl,
                              pddl_datalog_clause_t *clause,
                              const pddl_datalog_atom_t *atom);


void pddlDatalogPrint(const pddl_datalog_t *dl, FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_DATALOG_H__ */
