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
#include <boruvka/iset.h>
#include <boruvka/err.h>
#include <pddl/common.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_datalog_atom {
    int pred;
    unsigned *arg;

    int var_size;
    bor_iset_t var_set;
};
typedef struct pddl_datalog_atom pddl_datalog_atom_t;

struct pddl_datalog_rule {
    pddl_datalog_atom_t head;
    pddl_datalog_atom_t *body;
    int body_size;
    int body_alloc;
    pddl_datalog_atom_t *neg_body;
    int neg_body_size;
    int neg_body_alloc;

    bor_iset_t var_set;
    int is_safe;
    int same_head_body_vars;
    bor_iset_t common_body_var_set;
};
typedef struct pddl_datalog_rule pddl_datalog_rule_t;

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
 * TODO: user-id
 */
unsigned pddlDatalogAddConst(pddl_datalog_t *dl, const char *name);

/**
 * Adds predicate to the datalog program.
 * TODO: types
 */
unsigned pddlDatalogAddPred(pddl_datalog_t *dl, int arity, const char *name);

/**
 * Adds variable to the datalog program.
 * TODO: user-id
 */
unsigned pddlDatalogAddVar(pddl_datalog_t *dl, const char *name);

/**
 * Set user-id to const/pred element.
 */
void pddlDatalogSetUserId(pddl_datalog_t *dl, unsigned element, int user_id);

/**
 * Adds rule to the datalog.
 * TODO: neq
 * TODO: types
 */
int pddlDatalogAddRule(pddl_datalog_t *dl, const pddl_datalog_rule_t *cl);

/**
 * Returns true if the program is safe, i.e., all variables from head are
 * in body.
 */
int pddlDatalogIsSafe(const pddl_datalog_t *dl);

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
int pddlDatalogToNormalForm(pddl_datalog_t *dl, bor_err_t *err);

/**
 * TODO
 */
void pddlDatalogCanonicalModel(pddl_datalog_t *dl, bor_err_t *err);

/**
 * TODO
 */
void pddlDatalogFactsFromCanonicalModel(
            pddl_datalog_t *dl,
            unsigned pred,
            void (*fn)(int pred_user_id,
                       int arity,
                       const pddl_obj_id_t *arg_user_id,
                       void *user_data),
            void *user_data);

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
 * Initializes empty rule.
 */
void pddlDatalogRuleInit(pddl_datalog_t *dl, pddl_datalog_rule_t *rule);

/**
 * Deep copy of the rule.
 */
void pddlDatalogRuleCopy(pddl_datalog_t *dl,
                         pddl_datalog_rule_t *dst,
                         const pddl_datalog_rule_t *src);

/**
 * Free rule structure.
 */
void pddlDatalogRuleFree(pddl_datalog_t *dl, pddl_datalog_rule_t *rule);

/**
 * Set head of the rule.
 */
void pddlDatalogRuleSetHead(pddl_datalog_t *dl,
                            pddl_datalog_rule_t *rule,
                            const pddl_datalog_atom_t *head);

/**
 * Adds a body atom to the rule.
 */
void pddlDatalogRuleAddBody(pddl_datalog_t *dl,
                            pddl_datalog_rule_t *rule,
                            const pddl_datalog_atom_t *atom);

/**
 * Adds a negative atom to the body assuming this atom is static, i.e.,
 * it's not in any rule's head.
 */
void pddlDatalogRuleAddNegStaticBody(pddl_datalog_t *dl,
                                     pddl_datalog_rule_t *rule,
                                     const pddl_datalog_atom_t *atom);

/**
 * Removes i'th atom from the body
 */
void pddlDatalogRuleRmBody(pddl_datalog_t *dl,
                           pddl_datalog_rule_t *rule,
                           int i);

/**
 * Returns true if the program is safe, i.e., all variables from head are
 * in body.
 */
int pddlDatalogRuleIsSafe(const pddl_datalog_t *dl,
                          const pddl_datalog_rule_t *rule);


void pddlDatalogPrint(const pddl_datalog_t *dl, FILE *fout);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_DATALOG_H__ */
