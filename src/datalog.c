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

    pddl_datalog_rule_t *rule;
    int rule_size;
    int rule_alloc;
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

static void atomSetUp(pddl_datalog_t *dl,
                      pddl_datalog_atom_t *atom)
{
    atom->var_size = 0;
    borISetEmpty(&atom->var_set);
    int arity = dl->pred[atom->pred].arity;
    for (int i = 0; i < arity; ++i){
        if (IS_VAR(atom->arg[i])){
            ASSERT(atom->arg[i] > 0);
            borISetAdd(&atom->var_set, TO_IDX(atom->arg[i]));
            ++atom->var_size;
        }
    }
}

static void ruleSetUp(pddl_datalog_t *dl, pddl_datalog_rule_t *rule)
{
    BOR_ISET(body_vars);
    atomSetUp(dl, &rule->head);
    for (int i = 0; i < rule->body_size; ++i){
        atomSetUp(dl, rule->body + i);
        borISetUnion(&body_vars, &rule->body[i].var_set);
    }
    rule->head_has_all_vars_from_body
            = borISetIsSubset(&rule->head.var_set, &body_vars);
    borISetFree(&body_vars);
}

pddl_datalog_t *pddlDatalogNew(void)
{
    pddl_datalog_t *dl = BOR_ALLOC(pddl_datalog_t);
    bzero(dl, sizeof(*dl));
    return dl;
}

void pddlDatalogDel(pddl_datalog_t *dl)
{
    for (int i = 0; i < dl->rule_size; ++i)
        pddlDatalogRuleFree(dl, dl->rule + i);
    if (dl->rule != NULL)
        BOR_FREE(dl->rule);

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

int pddlDatalogAddRule(pddl_datalog_t *dl, const pddl_datalog_rule_t *cl)
{
    if (dl->rule_size == dl->rule_alloc){
        if (dl->rule_alloc == 0)
            dl->rule_alloc = 1;
        dl->rule_alloc *= 2;
        dl->rule = BOR_REALLOC_ARR(dl->rule, pddl_datalog_rule_t,
                                     dl->rule_alloc);
    }
    pddl_datalog_rule_t *rule = dl->rule + dl->rule_size++;
    pddlDatalogRuleInit(dl, rule);
    pddlDatalogRuleCopy(dl, rule, cl);
    ruleSetUp(dl, rule);
    return 0;
}

static void joinVars(const pddl_datalog_rule_t *rule,
                     const pddl_datalog_atom_t *a1,
                     const pddl_datalog_atom_t *a2,
                     bor_iset_t *vars)
{
    borISetEmpty(vars);
    borISetUnion2(vars, &a1->var_set, &a2->var_set);

    if (rule->head_has_all_vars_from_body){
        borISetIntersect(vars, &rule->head.var_set);

    }else{
        BOR_ISET(cvars);
        borISetUnion(&cvars, &rule->head.var_set);
        for (int i = 0; i < rule->body_size; ++i){
            if (rule->body + i == a1 || rule->body + i == a2)
                continue;
            borISetUnion(&cvars, &rule->body[i].var_set);
        }
        borISetIntersect(vars, &cvars);
        borISetFree(&cvars);
    }
}

static void joinCost(const pddl_datalog_rule_t *rule,
                     const pddl_datalog_atom_t *a1,
                     const pddl_datalog_atom_t *a2,
                     bor_iset_t *join_vars,
                     int *join_cost)
{
    joinVars(rule, a1, a2, join_vars);
    int a1size = borISetSize(&a1->var_set);
    int a2size = borISetSize(&a2->var_set);
    join_cost[2] = borISetSize(join_vars);
    join_cost[0] = join_cost[2] - BOR_MAX(a1size, a2size);
    join_cost[1] = join_cost[2] - BOR_MIN(a1size, a2size);
}

static void selectBodyAtoms(const pddl_datalog_t *dl,
                            const pddl_datalog_rule_t *rule,
                            int *a1, int *a2)
{
    BOR_ISET(join_vars);
    int join_cost_size = 3 * rule->body_size * rule->body_size;
    int *join_cost = BOR_ALLOC_ARR(int, join_cost_size);
    for (int a1i = 0; a1i < rule->body_size; ++a1i){
        const pddl_datalog_atom_t *atom1 = rule->body + a1i;
        for (int a2i = a1i + 1; a2i < rule->body_size; ++a2i){
            const pddl_datalog_atom_t *atom2 = rule->body + a1i;
            int *cost = join_cost + 3 * (a1i * rule->body_size + a2i);
            joinCost(rule, atom1, atom2, &join_vars, cost);
        }
    }

    int best_join_cost[3] = { INT_MAX, INT_MAX, INT_MAX };
    for (int a1i = 0; a1i < rule->body_size; ++a1i){
        for (int a2i = a1i + 1; a2i < rule->body_size; ++a2i){
            const int *cost = join_cost + 3 * (a1i * rule->body_size + a2i);
            if (memcmp(cost, best_join_cost, 3 * sizeof(int)) < 0){
                memcpy(best_join_cost, cost, 3 * sizeof(int));
                *a1 = a1i;
                *a2 = a2i;
            }
        }
    }

    BOR_FREE(join_cost);
    borISetFree(&join_vars);
}

static void toNormalFormStep(pddl_datalog_t *dl, int rule_id)
{
    pddl_datalog_rule_t *rule = dl->rule + rule_id;
    BOR_ISET(vars);
    pddl_datalog_atom_t head;

    // Select two atoms from the body
    int a1i, a2i;
    selectBodyAtoms(dl, rule, &a1i, &a2i);
    ASSERT(a1i < a2i);
    const pddl_datalog_atom_t *a1 = rule->body + a1i;
    const pddl_datalog_atom_t *a2 = rule->body + a2i;

    // Determine variables of the head
    joinVars(rule, a1, a2, &vars);

    // Construct a new predicate
    int pred_arity = borISetSize(&vars);
    unsigned pred = pddlDatalogAddPred(dl, pred_arity, NULL);

    // Head of the new rule
    pddlDatalogAtomInit(dl, &head, pred);
    for (int i = 0; i < pred_arity; ++i){
        unsigned v = dl->var[borISetGet(&vars, i)].id;
        pddlDatalogAtomSetArg(dl, &head, i, v);
    }

    // Construct a new rule
    pddl_datalog_rule_t newrule;
    pddlDatalogRuleInit(dl, &newrule);
    pddlDatalogRuleSetHead(dl, &newrule, &head);
    pddlDatalogRuleAddBody(dl, &newrule, a1);
    pddlDatalogRuleAddBody(dl, &newrule, a2);
    pddlDatalogAddRule(dl, &newrule);
    pddlDatalogRuleFree(dl, &newrule);


    // Update rule with the new predicate
    rule = dl->rule + rule_id;
    pddlDatalogRuleRmBody(dl, rule, a1i);
    pddlDatalogRuleRmBody(dl, rule, a2i - 1); // this requires a1i < a2i
    pddlDatalogRuleAddBody(dl, rule, &head);
    ruleSetUp(dl, rule);

    pddlDatalogAtomFree(dl, &head);
    borISetFree(&vars);
}

int pddlDatalogIsSafe(const pddl_datalog_t *dl)
{
    for (int i = 0; i < dl->rule_size; ++i){
        if (!pddlDatalogRuleIsSafe(dl, dl->rule + i))
            return 0;
    }
    return 1;
}

int pddlDatalogToNormalForm(pddl_datalog_t *dl)
{
    if (!pddlDatalogIsSafe(dl))
        return -1;

    int rule_size = dl->rule_size;
    for (int ci = 0; ci < rule_size; ++ci){
        while (dl->rule[ci].body_size > 2)
            toNormalFormStep(dl, ci);
    }
    return 0;
}

void pddlDatalogAtomInit(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *atom,
                         unsigned pred)
{
    bzero(atom, sizeof(*atom));
    int p = TO_IDX(pred);
    atom->pred = p;
    atom->arg = BOR_CALLOC_ARR(unsigned, dl->pred[p].arity);
}

void pddlDatalogAtomCopy(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *dst,
                         const pddl_datalog_atom_t *src)
{
    bzero(dst, sizeof(*dst));
    dst->pred = src->pred;
    dst->arg = BOR_CALLOC_ARR(unsigned, dl->pred[dst->pred].arity);
    memcpy(dst->arg, src->arg, sizeof(unsigned) * dl->pred[dst->pred].arity);
}

void pddlDatalogAtomFree(pddl_datalog_t *dl, pddl_datalog_atom_t *atom)
{
    if (atom->arg != NULL)
        BOR_FREE(atom->arg);
    borISetFree(&atom->var_set);
}

void pddlDatalogAtomSetArg(pddl_datalog_t *dl,
                           pddl_datalog_atom_t *atom,
                           int argi,
                           unsigned term)
{
    ASSERT(argi < dl->pred[atom->pred].arity);
    atom->arg[argi] = term;
}

void pddlDatalogRuleInit(pddl_datalog_t *dl, pddl_datalog_rule_t *rule)
{
    bzero(rule, sizeof(*rule));
}

void pddlDatalogRuleCopy(pddl_datalog_t *dl,
                         pddl_datalog_rule_t *dst,
                         const pddl_datalog_rule_t *src)
{
    bzero(dst, sizeof(*dst));
    pddlDatalogAtomCopy(dl, &dst->head, &src->head);
    dst->body_alloc = src->body_alloc;
    dst->body_size = src->body_size;
    dst->body = BOR_ALLOC_ARR(pddl_datalog_atom_t, dst->body_alloc);
    for (int i = 0; i < dst->body_size; ++i)
        pddlDatalogAtomCopy(dl, dst->body + i, src->body + i);
}

void pddlDatalogRuleFree(pddl_datalog_t *dl, pddl_datalog_rule_t *rule)
{
    pddlDatalogAtomFree(dl, &rule->head);
    for (int i = 0; i < rule->body_size; ++i)
        pddlDatalogAtomFree(dl, &rule->body[i]);
    if (rule->body != NULL)
        BOR_FREE(rule->body);
}

void pddlDatalogRuleSetHead(pddl_datalog_t *dl,
                            pddl_datalog_rule_t *rule,
                            const pddl_datalog_atom_t *head)
{
    pddlDatalogAtomFree(dl, &rule->head);
    pddlDatalogAtomCopy(dl, &rule->head, head);
}

void pddlDatalogRuleAddBody(pddl_datalog_t *dl,
                            pddl_datalog_rule_t *rule,
                            const pddl_datalog_atom_t *atom)
{
    if (rule->body_size == rule->body_alloc){
        if (rule->body_alloc == 0)
            rule->body_alloc = 1;
        rule->body_alloc *= 2;
        rule->body = BOR_REALLOC_ARR(rule->body, pddl_datalog_atom_t,
                                       rule->body_alloc);
    }
    pddl_datalog_atom_t *a = rule->body + rule->body_size++;
    pddlDatalogAtomCopy(dl, a, atom);
}

void pddlDatalogRuleRmBody(pddl_datalog_t *dl,
                           pddl_datalog_rule_t *rule,
                           int i)
{
    pddlDatalogAtomFree(dl, rule->body + i);
    for (int j = i + 1; j < rule->body_size; ++j)
        rule->body[j - 1] = rule->body[j];
    --rule->body_size;
}

int pddlDatalogRuleIsSafe(const pddl_datalog_t *dl,
                          const pddl_datalog_rule_t *rule)
{
    BOR_ISET(body_vars);
    for (int i = 0; i < rule->body_size; ++i){
        int arity = dl->pred[rule->body[i].pred].arity;
        for (int j = 0; j < arity; ++j){
            if (IS_VAR(rule->body[i].arg[j]))
                borISetAdd(&body_vars, TO_IDX(rule->body[i].arg[j]));
        }
    }
    int arity = dl->pred[rule->head.pred].arity;
    for (int j = 0; j < arity; ++j){
        if (IS_VAR(rule->head.arg[j])){
            if (!borISetIn(TO_IDX(rule->head.arg[j]), &body_vars))
                return 0;
        }
    }
    borISetFree(&body_vars);

    return 1;
}

static void printEl(const pddl_datalog_t *dl, unsigned id, FILE *fout)
{
    int idx = TO_IDX(id);
    if (IS_CONST(id)){
        if (dl->c[idx].name != NULL){
            fprintf(fout, "%s", dl->c[idx].name);
        }else{
            fprintf(fout, "_c%d", idx);
        }

    }else if (IS_VAR(id)){
        if (dl->var[idx].name != NULL){
            fprintf(fout, "%s", dl->var[idx].name);
        }else{
            fprintf(fout, "_v%d", idx);
        }

    }else if (IS_PRED(id)){
        if (dl->pred[idx].name != NULL){
            fprintf(fout, "%s", dl->pred[idx].name);
        }else{
            fprintf(fout, "_P%d", idx);
        }
    }
}

static void printAtom(const pddl_datalog_t *dl,
                      const pddl_datalog_atom_t *atom,
                      FILE *fout)
{
    printEl(dl, IDX_TO_PRED(atom->pred), fout);
    int p = atom->pred;
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
    for (int ci = 0; ci < dl->rule_size; ++ci){
        const pddl_datalog_rule_t *c = dl->rule + ci;
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
