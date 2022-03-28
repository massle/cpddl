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

#include <unistd.h>
#include "alloc.h"
#include "pddl/hfunc.h"
#include <pddl/extarr.h>
#include "pddl/htable.h"
#include "pddl/datalog.h"
#include "assert.h"

struct pddl_datalog_fact {
    pddl_htable_key_t hash;
    pddl_list_t htable;

    int id;
    int arity;
    int pred;
    int arg[];
};
typedef struct pddl_datalog_fact pddl_datalog_fact_t;

struct pddl_datalog_relevant_fact {
    pddl_htable_key_t hash;
    pddl_list_t htable;

    int id;
    pddl_iset_t fact;
    int key_size;
    int rule;
    int key[];
};
typedef struct pddl_datalog_relevant_fact pddl_datalog_relevant_fact_t;

struct pddl_datalog_db {
    pddl_htable_t *hfact;
    pddl_htable_t *hrelevant_fact[2];
    size_t used_mem;
    pddl_extarr_t *fact;
    int fact_size;
    pddl_iset_t *pred_to_fact;
    pddl_extarr_t *relevant_fact[2];
    int relevant_fact_size[2];
};
typedef struct pddl_datalog_db pddl_datalog_db_t;

struct pddl_datalog_const {
    unsigned id;
    int idx;
    char *name;
    pddl_obj_id_t user_id;
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
    int user_id;
    pddl_iset_t relevant_rules;
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

    int dirty;
    int max_pred_arity;
    pddl_datalog_db_t db;
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

static pddl_htable_key_t factComputeHash(const pddl_datalog_fact_t *fact)
{
    size_t size = sizeof(int) + fact->arity * sizeof(int);
    return pddlCityHash_64(&fact->pred, size);
}

static pddl_htable_key_t factHash(const pddl_list_t *key, void *_)
{
    return PDDL_LIST_ENTRY(key, pddl_datalog_fact_t, htable)->hash;
}

static int factEq(const pddl_list_t *key1, const pddl_list_t *key2, void *_)
{
    const pddl_datalog_fact_t *f1, *f2;
    f1 = PDDL_LIST_ENTRY(key1, pddl_datalog_fact_t, htable);
    f2 = PDDL_LIST_ENTRY(key2, pddl_datalog_fact_t, htable);
    size_t size = sizeof(int) + f1->arity * sizeof(int);
    return f1->arity == f2->arity && memcmp(&f1->pred, &f2->pred, size) == 0;
}

static pddl_htable_key_t relevantFactComputeHash(
                const pddl_datalog_relevant_fact_t *f)
{
    size_t size = f->key_size * 2 * sizeof(int);
    return pddlCityHash_64(f->key, size);
}

static pddl_htable_key_t relevantFactHash(const pddl_list_t *key, void *_)
{
    return PDDL_LIST_ENTRY(key, pddl_datalog_relevant_fact_t, htable)->hash;
}

static int relevantFactEq(const pddl_list_t *key1,
                          const pddl_list_t *key2,
                          void *_)
{
    const pddl_datalog_relevant_fact_t *f1, *f2;
    f1 = PDDL_LIST_ENTRY(key1, pddl_datalog_relevant_fact_t, htable);
    f2 = PDDL_LIST_ENTRY(key2, pddl_datalog_relevant_fact_t, htable);
    size_t size = sizeof(int) + f1->key_size * 2 * sizeof(int);
    return f1->rule == f2->rule && memcmp(&f1->rule, &f2->rule, size) == 0;
}


static void dbFree(pddl_datalog_t *dl, pddl_datalog_db_t *db);
static void dbInit(pddl_datalog_t *dl, pddl_datalog_db_t *db)
{
    dbFree(dl, db);
    db->hfact = pddlHTableNew(factHash, factEq, NULL);
    db->hrelevant_fact[0] = pddlHTableNew(relevantFactHash,
                                         relevantFactEq, NULL);
    db->hrelevant_fact[1] = pddlHTableNew(relevantFactHash,
                                         relevantFactEq, NULL);

    size_t size = sizeof(pddl_datalog_fact_t);
    size += dl->max_pred_arity * sizeof(int);
    void *init = alloca(size);
    bzero(init, size);
    db->fact = pddlExtArrNew(size, NULL, init);
    db->pred_to_fact = CALLOC_ARR(pddl_iset_t, dl->pred_size);

    size = sizeof(pddl_datalog_relevant_fact_t);
    size += dl->max_pred_arity * 2 * sizeof(int);
    init = alloca(size);
    bzero(init, size);
    db->relevant_fact[0] = pddlExtArrNew(size, NULL, init);
    db->relevant_fact[1] = pddlExtArrNew(size, NULL, init);
}

static void dbFree(pddl_datalog_t *dl, pddl_datalog_db_t *db)
{
    if (db->hfact == NULL)
        return;
    pddlHTableDel(db->hfact);
    pddlHTableDel(db->hrelevant_fact[0]);
    pddlHTableDel(db->hrelevant_fact[1]);
    pddlExtArrDel(db->fact);
    for (int i = 0; i < 2; ++i){
        for (int j = 0; j < db->relevant_fact_size[i]; ++j){
            void *x = pddlExtArrGet(db->relevant_fact[i], j);
            pddl_datalog_relevant_fact_t *f = (pddl_datalog_relevant_fact_t *)x;
            pddlISetFree(&f->fact);
        }
        pddlExtArrDel(db->relevant_fact[i]);
    }
    for (int i = 0; i < dl->pred_size; ++i)
        pddlISetFree(db->pred_to_fact + i);
    FREE(db->pred_to_fact);

    bzero(db, sizeof(*db));
}

static size_t dbUseddMem(const pddl_datalog_db_t *db)
{
    return db->used_mem;
}

static pddl_datalog_fact_t *dbFact(pddl_datalog_db_t *db, int id)
{
    return (pddl_datalog_fact_t *)pddlExtArrGet(db->fact, id);
}

static int dbHasFact(pddl_datalog_t *dl,
                     pddl_datalog_db_t *db,
                     int pred,
                     const int *arg)
{
    int arity = dl->pred[pred].arity;
    size_t size = sizeof(pddl_datalog_fact_t) + arity * sizeof(int);
    pddl_datalog_fact_t *f = alloca(size);

    f->arity = arity;
    f->pred = pred;
    memcpy(f->arg, arg, sizeof(int) * arity);
    f->hash = factComputeHash(f);
    pddl_list_t *ret = pddlHTableFind(db->hfact, &f->htable);
    if (ret == NULL){
        return 0;

    }else{
        f = PDDL_LIST_ENTRY(ret, pddl_datalog_fact_t, htable);
        return f->id;
    }
}

static int dbAddFact(pddl_datalog_t *dl,
                     pddl_datalog_db_t *db,
                     int pred,
                     const int *arg)
{
    pddl_datalog_fact_t *f;
    f = (pddl_datalog_fact_t *)pddlExtArrGet(db->fact, db->fact_size);

    f->arity = dl->pred[pred].arity;
    f->pred = pred;
    memcpy(f->arg, arg, sizeof(int) * f->arity);
    f->hash = factComputeHash(f);
    pddlListInit(&f->htable);
    pddl_list_t *ret = pddlHTableInsertUnique(db->hfact, &f->htable);
    if (ret == NULL){
        f->id = db->fact_size++;
        db->used_mem += db->fact->arr->el_size;
        pddlISetAdd(&db->pred_to_fact[f->pred], f->id);
        db->used_mem += sizeof(int);

    }else{
        f = PDDL_LIST_ENTRY(ret, pddl_datalog_fact_t, htable);
    }
    return f->id;
}

static void dbAddRelevantFact(pddl_datalog_t *dl,
                              pddl_datalog_db_t *db,
                              int bid,
                              int rule,
                              const pddl_iset_t *key_vars,
                              const int *var_map,
                              int fact_id)
{
    void *xf = pddlExtArrGet(db->relevant_fact[bid],
                            db->relevant_fact_size[bid]);
    pddl_datalog_relevant_fact_t *f = (pddl_datalog_relevant_fact_t *)xf;

    pddlISetInit(&f->fact);
    f->key_size = pddlISetSize(key_vars);
    f->rule = rule;
    for (int i = 0; i < f->key_size; ++i){
        f->key[2 * i] = pddlISetGet(key_vars, i);
        f->key[2 * i + 1] = var_map[f->key[2 * i]];
    }
    f->hash = relevantFactComputeHash(f);
    pddlListInit(&f->htable);

    pddl_list_t *ret;
    ret = pddlHTableInsertUnique(db->hrelevant_fact[bid], &f->htable);
    if (ret == NULL){
        f->id = db->relevant_fact_size[bid]++;
        db->used_mem += db->relevant_fact[bid]->arr->el_size;

    }else{
        f = PDDL_LIST_ENTRY(ret, pddl_datalog_relevant_fact_t, htable);
    }
    pddlISetAdd(&f->fact, fact_id);
    db->used_mem += sizeof(int);
}

static pddl_datalog_relevant_fact_t *dbFindRelevantFact(
            pddl_datalog_t *dl,
            pddl_datalog_db_t *db,
            int bid,
            int rule,
            const pddl_iset_t *key_vars,
            const int *var_map)
{
    int key_size = pddlISetSize(key_vars);
    size_t size = sizeof(pddl_datalog_relevant_fact_t);
    size += key_size * 2 * sizeof(int);
    pddl_datalog_relevant_fact_t *f = alloca(size);

    f->key_size = key_size;
    f->rule = rule;
    for (int i = 0; i < key_size; ++i){
        f->key[2 * i] = pddlISetGet(key_vars, i);
        f->key[2 * i + 1] = var_map[f->key[2 * i]];
    }
    f->hash = relevantFactComputeHash(f);

    pddl_list_t *ret;
    ret = pddlHTableFind(db->hrelevant_fact[bid], &f->htable);
    if (ret == NULL){
        return NULL;
    }else{
        return PDDL_LIST_ENTRY(ret, pddl_datalog_relevant_fact_t, htable);
    }
}

static void atomSetUp(pddl_datalog_t *dl, pddl_datalog_atom_t *atom)
{
    atom->var_size = 0;
    pddlISetEmpty(&atom->var_set);
    int arity = dl->pred[atom->pred].arity;
    for (int i = 0; i < arity; ++i){
        if (IS_VAR(atom->arg[i])){
            ASSERT(atom->arg[i] > 0);
            pddlISetAdd(&atom->var_set, TO_IDX(atom->arg[i]));
            ++atom->var_size;
        }
    }
}

static void ruleSetUp(pddl_datalog_t *dl, pddl_datalog_rule_t *rule)
{
    PDDL_ISET(body_vars);
    atomSetUp(dl, &rule->head);
    pddlISetEmpty(&rule->common_body_var_set);
    for (int i = 0; i < rule->body_size; ++i){
        atomSetUp(dl, rule->body + i);
        if (i == 0){
            pddlISetUnion(&rule->common_body_var_set, &rule->body[0].var_set);
        }else{
            pddlISetIntersect(&rule->common_body_var_set,
                             &rule->body[i].var_set);
        }

        pddlISetUnion(&body_vars, &rule->body[i].var_set);
    }
    rule->is_safe = pddlISetIsSubset(&rule->head.var_set, &body_vars);
    rule->same_head_body_vars = pddlISetEq(&body_vars, &rule->head.var_set);
    pddlISetUnion2(&rule->var_set, &body_vars, &rule->head.var_set);
    pddlISetFree(&body_vars);

    // TODO: Check that the neg_body atom has only variables from
    // rule->var_set
    for (int i = 0; i < rule->neg_body_size; ++i)
        atomSetUp(dl, rule->neg_body + i);
}

static void predsSetUp(pddl_datalog_t *dl)
{
    for (int p = 0; p < dl->pred_size; ++p)
        pddlISetEmpty(&dl->pred[p].relevant_rules);

    for (int r = 0; r < dl->rule_size; ++r){
        const pddl_datalog_rule_t *rule = dl->rule + r;
        if (rule->body_size == 0)
            continue;
        for (int b = 0; b < rule->body_size; ++b){
            int pred = rule->body[b].pred;
            ASSERT(pred < dl->pred_size);
            pddlISetAdd(&dl->pred[pred].relevant_rules, r);
        }
    }
}

static void setUp(pddl_datalog_t *dl, int db, bor_err_t *err)
{
    if (dl->dirty){
        for (int r = 0; r < dl->rule_size; ++r)
            ruleSetUp(dl, &dl->rule[r]);
        predsSetUp(dl);
        dl->dirty = 0;
        if (db)
            dbInit(dl, &dl->db);

    }else if (db && dl->db.fact == NULL){
        dbInit(dl, &dl->db);
    }
}

pddl_datalog_t *pddlDatalogNew(void)
{
    pddl_datalog_t *dl = ALLOC(pddl_datalog_t);
    bzero(dl, sizeof(*dl));
    return dl;
}

void pddlDatalogDel(pddl_datalog_t *dl)
{
    for (int i = 0; i < dl->rule_size; ++i)
        pddlDatalogRuleFree(dl, dl->rule + i);
    if (dl->rule != NULL)
        FREE(dl->rule);

    for (int i = 0; i < dl->c_size; ++i){
        if (dl->c[i].name != NULL)
            FREE(dl->c[i].name);
    }
    if (dl->c != NULL)
        FREE(dl->c);

    for (int i = 0; i < dl->var_size; ++i){
        if (dl->var[i].name != NULL)
            FREE(dl->var[i].name);
    }
    if (dl->var != NULL)
        FREE(dl->var);

    for (int i = 0; i < dl->pred_size; ++i){
        if (dl->pred[i].name != NULL)
            FREE(dl->pred[i].name);
        pddlISetFree(&dl->pred[i].relevant_rules);
    }
    if (dl->pred != NULL)
        FREE(dl->pred);
    dbFree(dl, &dl->db);
    FREE(dl);
}

unsigned pddlDatalogAddConst(pddl_datalog_t *dl, const char *name)
{
    if (dl->c_size == dl->c_alloc){
        if (dl->c_alloc == 0)
            dl->c_alloc = 1;
        dl->c_alloc *= 2;
        dl->c = REALLOC_ARR(dl->c, pddl_datalog_const_t, dl->c_alloc);
    }
    pddl_datalog_const_t *c = dl->c + dl->c_size;
    bzero(c, sizeof(*c));
    c->idx = dl->c_size++;
    c->id = IDX_TO_CONST(c->idx);
    c->name = NULL;
    if (name != NULL)
        c->name = STRDUP(name);
    c->user_id = -1;
    dl->dirty = 1;
    return c->id;
}

unsigned pddlDatalogAddPred(pddl_datalog_t *dl, int arity, const char *name)
{
    if (dl->pred_size == dl->pred_alloc){
        if (dl->pred_alloc == 0)
            dl->pred_alloc = 1;
        dl->pred_alloc *= 2;
        dl->pred = REALLOC_ARR(dl->pred, pddl_datalog_pred_t,
                                   dl->pred_alloc);
    }
    pddl_datalog_pred_t *p = dl->pred + dl->pred_size;
    bzero(p, sizeof(*p));
    p->idx = dl->pred_size++;
    p->id = IDX_TO_PRED(p->idx);
    p->arity = arity;
    p->name = NULL;
    if (name != NULL)
        p->name = STRDUP(name);
    p->user_id = -1;
    dl->max_pred_arity = PDDL_MAX(dl->max_pred_arity, arity);
    dl->dirty = 1;
    return p->id;
}

unsigned pddlDatalogAddVar(pddl_datalog_t *dl, const char *name)
{
    if (dl->var_size == dl->var_alloc){
        if (dl->var_alloc == 0)
            dl->var_alloc = 1;
        dl->var_alloc *= 2;
        dl->var = REALLOC_ARR(dl->var, pddl_datalog_var_t, dl->var_alloc);
    }
    pddl_datalog_var_t *v = dl->var + dl->var_size;
    bzero(v, sizeof(*v));
    v->idx = dl->var_size++;
    v->id = IDX_TO_VAR(v->idx);
    v->name = NULL;
    if (name != NULL)
        v->name = STRDUP(name);
    dl->dirty = 1;
    return v->id;
}

void pddlDatalogSetUserId(pddl_datalog_t *dl, unsigned element, int user_id)
{
    if (IS_PRED(element)){
        dl->pred[TO_IDX(element)].user_id = user_id;
    }else if (IS_CONST(element)){
        dl->c[TO_IDX(element)].user_id = user_id;
    }
}

int pddlDatalogAddRule(pddl_datalog_t *dl, const pddl_datalog_rule_t *cl)
{
    if (dl->rule_size == dl->rule_alloc){
        if (dl->rule_alloc == 0)
            dl->rule_alloc = 1;
        dl->rule_alloc *= 2;
        dl->rule = REALLOC_ARR(dl->rule, pddl_datalog_rule_t,
                                     dl->rule_alloc);
    }
    pddl_datalog_rule_t *rule = dl->rule + dl->rule_size++;
    pddlDatalogRuleInit(dl, rule);
    pddlDatalogRuleCopy(dl, rule, cl);
    dl->dirty = 1;
    return 0;
}

static void joinVars(const pddl_datalog_rule_t *rule,
                     const pddl_datalog_atom_t *a1,
                     const pddl_datalog_atom_t *a2,
                     pddl_iset_t *vars)
{
    pddlISetEmpty(vars);
    pddlISetUnion2(vars, &a1->var_set, &a2->var_set);

    if (rule->same_head_body_vars){
        pddlISetIntersect(vars, &rule->head.var_set);

    }else{
        PDDL_ISET(cvars);
        pddlISetUnion(&cvars, &rule->head.var_set);
        for (int i = 0; i < rule->body_size; ++i){
            if (rule->body + i == a1 || rule->body + i == a2)
                continue;
            pddlISetUnion(&cvars, &rule->body[i].var_set);
        }
        pddlISetIntersect(vars, &cvars);
        pddlISetFree(&cvars);
    }
}

static void joinCost(const pddl_datalog_rule_t *rule,
                     const pddl_datalog_atom_t *a1,
                     const pddl_datalog_atom_t *a2,
                     pddl_iset_t *join_vars,
                     int *join_cost)
{
    joinVars(rule, a1, a2, join_vars);
    int a1size = pddlISetSize(&a1->var_set);
    int a2size = pddlISetSize(&a2->var_set);
    join_cost[2] = pddlISetSize(join_vars);
    join_cost[0] = join_cost[2] - PDDL_MAX(a1size, a2size);
    join_cost[1] = join_cost[2] - PDDL_MIN(a1size, a2size);
}

static void selectBodyAtoms(const pddl_datalog_t *dl,
                            const pddl_datalog_rule_t *rule,
                            int *a1, int *a2)
{
    PDDL_ISET(join_vars);
    int join_cost_size = 3 * rule->body_size * rule->body_size;
    int *join_cost = ALLOC_ARR(int, join_cost_size);
    for (int a1i = 0; a1i < rule->body_size; ++a1i){
        const pddl_datalog_atom_t *atom1 = rule->body + a1i;
        for (int a2i = a1i + 1; a2i < rule->body_size; ++a2i){
            const pddl_datalog_atom_t *atom2 = rule->body + a2i;
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

    FREE(join_cost);
    pddlISetFree(&join_vars);
}

static void collectVarsFromAtom(const pddl_datalog_t *dl,
                                const pddl_datalog_atom_t *a,
                                pddl_iset_t *var)
{
    int arity = dl->pred[a->pred].arity;
    for (int i = 0; i < arity; ++i){
        if (IS_VAR(a->arg[i]))
            pddlISetAdd(var, TO_IDX(a->arg[i]));
    }
}

static void collectVarsFromRule(const pddl_datalog_t *dl,
                                const pddl_datalog_rule_t *r,
                                pddl_iset_t *var)
{
    collectVarsFromAtom(dl, &r->head, var);
    for (int i = 0; i < r->body_size; ++i)
        collectVarsFromAtom(dl, &r->body[i], var);
}

static void transferNegBody(pddl_datalog_t *dl,
                            pddl_datalog_rule_t *src,
                            pddl_datalog_rule_t *dst)
{
    PDDL_ISET(vars);
    collectVarsFromRule(dl, dst, &vars);
    int ins = 0;
    for (int i = 0; i < src->neg_body_size; ++i){
        pddl_datalog_atom_t *a = src->neg_body + i;
        if (pddlISetIsSubset(&a->var_set, &vars)){
            pddlDatalogRuleAddNegStaticBody(dl, dst, a);
            pddlDatalogAtomFree(dl, a);
        }else{
            src->neg_body[ins++] = *a;
        }
    }
    src->neg_body_size = ins;
    pddlISetFree(&vars);
}

static void toNormalFormStep(pddl_datalog_t *dl, int rule_id)
{
    pddl_datalog_rule_t *rule = dl->rule + rule_id;
    PDDL_ISET(vars);
    pddl_datalog_atom_t head;

    // Select two atoms from the body
    int a1i = 0, a2i = 0;
    selectBodyAtoms(dl, rule, &a1i, &a2i);
    ASSERT(a1i < a2i);
    const pddl_datalog_atom_t *a1 = rule->body + a1i;
    const pddl_datalog_atom_t *a2 = rule->body + a2i;

    // Determine variables of the head
    joinVars(rule, a1, a2, &vars);

    // Construct a new predicate
    int pred_arity = pddlISetSize(&vars);
    unsigned pred = pddlDatalogAddPred(dl, pred_arity, NULL);

    // Head of the new rule
    pddlDatalogAtomInit(dl, &head, pred);
    for (int i = 0; i < pred_arity; ++i){
        unsigned v = dl->var[pddlISetGet(&vars, i)].id;
        pddlDatalogAtomSetArg(dl, &head, i, v);
    }

    // Construct a new rule
    pddl_datalog_rule_t newrule;
    pddlDatalogRuleInit(dl, &newrule);
    pddlDatalogRuleSetHead(dl, &newrule, &head);
    pddlDatalogRuleAddBody(dl, &newrule, a1);
    pddlDatalogRuleAddBody(dl, &newrule, a2);
    transferNegBody(dl, rule, &newrule);
    pddlDatalogAddRule(dl, &newrule);
    pddlDatalogRuleFree(dl, &newrule);


    // Update rule with the new predicate
    rule = dl->rule + rule_id;
    pddlDatalogRuleRmBody(dl, rule, a1i);
    pddlDatalogRuleRmBody(dl, rule, a2i - 1); // this requires a1i < a2i
    pddlDatalogRuleAddBody(dl, rule, &head);
    ruleSetUp(dl, rule);

    pddlDatalogAtomFree(dl, &head);
    pddlISetFree(&vars);
}

int pddlDatalogIsSafe(const pddl_datalog_t *dl)
{
    for (int i = 0; i < dl->rule_size; ++i){
        if (!pddlDatalogRuleIsSafe(dl, dl->rule + i))
            return 0;
    }
    return 1;
}

int pddlDatalogToNormalForm(pddl_datalog_t *dl, bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "DL: ");
    BOR_INFO(err, "Normal form of the datalog program start"
                  " (consts: %d, vars: %d, predicates: %d, rules: %d)",
             dl->c_size, dl->var_size, dl->pred_size, dl->rule_size);
    setUp(dl, 0, err);
    if (!pddlDatalogIsSafe(dl)){
        BOR_ERR_RET2(err, -1, "Cannot create normal form because the"
                              "datalog program is not safe");
    }

    int rule_size = dl->rule_size;
    for (int ci = 0; ci < rule_size; ++ci){
        while (dl->rule[ci].body_size > 2)
            toNormalFormStep(dl, ci);
    }
    BOR_INFO(err, "Normal form of the datalog program DONE"
                  " (consts: %d, vars: %d, predicates: %d, rules: %d)",
             dl->c_size, dl->var_size, dl->pred_size, dl->rule_size);
    dl->dirty = 1;
    BOR_INFO_PREFIX_POP(err);
    return 0;
}

static void insertInitialFacts(pddl_datalog_t *dl, bor_err_t *err)
{
    int args[dl->max_pred_arity];
    for (int ri = 0; ri < dl->rule_size; ++ri){
        const pddl_datalog_rule_t *rule = dl->rule + ri;
        if (rule->body_size > 0)
            continue;
        const pddl_datalog_atom_t *atom = &rule->head;
        int arity = dl->pred[atom->pred].arity;
        int abort = 0;
        for (int ai = 0; ai < arity; ++ai){
            if (IS_CONST(atom->arg[ai])){
                args[ai] = TO_IDX(atom->arg[ai]);
            }else{
                abort = 1;
                break;
            }
        }
        if (abort)
            continue;
        dbAddFact(dl, &dl->db, atom->pred, args);
    }
}

static int unify(pddl_datalog_t *dl,
                 int fact_pred,
                 const int *fact_arg,
                 const pddl_datalog_atom_t *atom,
                 int *var_map)
{
    if (fact_pred != atom->pred)
        return -1;

    int var;
    PDDL_ISET_FOR_EACH(&atom->var_set, var)
        var_map[var] = -1;

    int arity = dl->pred[fact_pred].arity;
    for (int i = 0; i < arity; ++i){
        if (IS_VAR(atom->arg[i])){
            int var = TO_IDX(atom->arg[i]);
            if (var_map[var] < 0){
                var_map[var] = fact_arg[i];
            }else if (var_map[var] != fact_arg[i]){
                return -1;
            }

        }else{
            int c = TO_IDX(atom->arg[i]);
            if (c != fact_arg[i])
                return -1;
        }
    }

    return 0;
}

static int ruleNegBodySatisfied(pddl_datalog_t *dl,
                                const pddl_datalog_rule_t *rule,
                                const int *var_map)
{
    for (int i = 0; i < rule->neg_body_size; ++i){
        const pddl_datalog_atom_t *a = rule->neg_body + i;
        int arity = dl->pred[a->pred].arity;
        int arg[arity];
        for (int ai = 0; ai < arity; ++ai){
            if (IS_CONST(a->arg[ai])){
                arg[ai] = TO_IDX(a->arg[ai]);
            }else{
                arg[ai] = var_map[TO_IDX(a->arg[ai])];
            }
        }
        if (dbHasFact(dl, &dl->db, a->pred, arg))
            return 0;
    }
    return 1;
}

static void ruleToFact(pddl_datalog_t *dl,
                       const pddl_datalog_rule_t *rule,
                       const int *var_map)
{
    if (!ruleNegBodySatisfied(dl, rule, var_map))
        return;

    const pddl_datalog_atom_t *head = &rule->head;
    int arity = dl->pred[head->pred].arity;
    int arg[arity];
    for (int i = 0; i < arity; ++i){
        if (IS_VAR(head->arg[i])){
            arg[i] = var_map[TO_IDX(head->arg[i])];
        }else{
            arg[i] = TO_IDX(head->arg[i]);
        }
    }
    dbAddFact(dl, &dl->db, head->pred, arg);
}

static void applyFactOnJoinRule(pddl_datalog_t *dl,
                                int atom_idx,
                                int rule_id,
                                int fact_id,
                                int *var_map,
                                bor_err_t *err)
{
    const pddl_datalog_rule_t *rule = dl->rule + rule_id;
    int other_atom_idx = (atom_idx + 1) % 2;
    const pddl_datalog_atom_t *b1 = &rule->body[other_atom_idx];
    const pddl_iset_t *key_var = &rule->common_body_var_set;

    dbAddRelevantFact(dl, &dl->db, atom_idx, rule_id,
                      key_var, var_map, fact_id);
    const pddl_datalog_relevant_fact_t *rel;
    rel = dbFindRelevantFact(dl, &dl->db, other_atom_idx, rule_id,
                             key_var, var_map);
    if (rel == NULL)
        return;

    int b1_arity = dl->pred[b1->pred].arity;
    int fid;
    PDDL_ISET_FOR_EACH(&rel->fact, fid){
        const pddl_datalog_fact_t *f = dbFact(&dl->db, fid);
        ASSERT(f->arity == b1_arity);
        for (int i = 0; i < b1_arity; ++i){
            if (IS_VAR(b1->arg[i]))
                var_map[TO_IDX(b1->arg[i])] = f->arg[i];
        }
        ruleToFact(dl, rule, var_map);
    }
}

static void applyFactOnRule(pddl_datalog_t *dl,
                            const pddl_datalog_fact_t *f,
                            int rule_id,
                            bor_err_t *err)
{
    const pddl_datalog_rule_t *rule = dl->rule + rule_id;
    int var_map[dl->var_size];
    if (rule->body_size == 1
            && unify(dl, f->pred, f->arg, &rule->body[0], var_map) == 0){
        ruleToFact(dl, rule, var_map);
    }

    if (rule->body_size == 2
            && unify(dl, f->pred, f->arg, &rule->body[0], var_map) == 0){
        applyFactOnJoinRule(dl, 0, rule_id, f->id, var_map, err);
    }

    if (rule->body_size == 2
            && unify(dl, f->pred, f->arg, &rule->body[1], var_map) == 0){
        applyFactOnJoinRule(dl, 1, rule_id, f->id, var_map, err);
    }
}

void pddlDatalogCanonicalModel(pddl_datalog_t *dl, bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "DL: ");
    BOR_INFO_PREFIX_PUSH(err, "Canonical model: ");
    BOR_INFO(err, "start (consts: %d, vars: %d, predicates: %d, rules: %d)",
             dl->c_size, dl->var_size, dl->pred_size, dl->rule_size);
    setUp(dl, 1, err);

    insertInitialFacts(dl, err);
    BOR_INFO(err, "Added initial facts: %d", dl->db.fact_size);

    int cur_id = 0;
    while (cur_id < dl->db.fact_size){
        const pddl_datalog_fact_t *f = dbFact(&dl->db, cur_id);
        int rule_id;
        PDDL_ISET_FOR_EACH(&dl->pred[f->pred].relevant_rules, rule_id)
            applyFactOnRule(dl, f, rule_id, err);
        ++cur_id;
        if (cur_id % 100000 == 0){
            BOR_INFO(err, "progress (facts processed: %d, overall: %d,"
                          " db-mem: %luMB)",
                     cur_id, dl->db.fact_size,
                     dbUseddMem(&dl->db) / (1024lu * 1024lu));
        }
    }
    BOR_INFO(err, "DONE (facts: %d, db-mem: %luMB)",
             dl->db.fact_size, dbUseddMem(&dl->db) / (1024lu * 1024lu));
    BOR_INFO_PREFIX_POP(err);
    BOR_INFO_PREFIX_POP(err);
}

void pddlDatalogFactsFromCanonicalModel(
            pddl_datalog_t *dl,
            unsigned pred,
            void (*fn)(int pred_user_id,
                       int arity,
                       const pddl_obj_id_t *arg_user_id,
                       void *user_data),
            void *user_data)
{
    int arity = dl->pred[TO_IDX(pred)].arity;
    pddl_obj_id_t arg[arity];
    int fact_id;
    PDDL_ISET_FOR_EACH(&dl->db.pred_to_fact[TO_IDX(pred)], fact_id){
        pddl_datalog_fact_t *f = dbFact(&dl->db, fact_id);
        int p = dl->pred[f->pred].user_id;
        for (int i = 0; i < arity; ++i)
            arg[i] = dl->c[f->arg[i]].user_id;
        fn(p, arity, arg, user_data);
    }
}

void pddlDatalogAtomInit(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *atom,
                         unsigned pred)
{
    bzero(atom, sizeof(*atom));
    int p = TO_IDX(pred);
    atom->pred = p;
    atom->arg = CALLOC_ARR(unsigned, dl->pred[p].arity);
}

void pddlDatalogAtomCopy(pddl_datalog_t *dl,
                         pddl_datalog_atom_t *dst,
                         const pddl_datalog_atom_t *src)
{
    bzero(dst, sizeof(*dst));
    dst->pred = src->pred;
    dst->arg = CALLOC_ARR(unsigned, dl->pred[dst->pred].arity);
    memcpy(dst->arg, src->arg, sizeof(unsigned) * dl->pred[dst->pred].arity);
}

void pddlDatalogAtomFree(pddl_datalog_t *dl, pddl_datalog_atom_t *atom)
{
    if (atom->arg != NULL)
        FREE(atom->arg);
    pddlISetFree(&atom->var_set);
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
    dst->body = ALLOC_ARR(pddl_datalog_atom_t, dst->body_alloc);
    for (int i = 0; i < dst->body_size; ++i)
        pddlDatalogAtomCopy(dl, dst->body + i, src->body + i);

    dst->neg_body_alloc = src->neg_body_alloc;
    dst->neg_body_size = src->neg_body_size;
    dst->neg_body = ALLOC_ARR(pddl_datalog_atom_t, dst->neg_body_alloc);
    for (int i = 0; i < dst->neg_body_size; ++i)
        pddlDatalogAtomCopy(dl, dst->neg_body + i, src->neg_body + i);
}

void pddlDatalogRuleFree(pddl_datalog_t *dl, pddl_datalog_rule_t *rule)
{
    pddlDatalogAtomFree(dl, &rule->head);
    for (int i = 0; i < rule->body_size; ++i)
        pddlDatalogAtomFree(dl, &rule->body[i]);
    if (rule->body != NULL)
        FREE(rule->body);
    for (int i = 0; i < rule->neg_body_size; ++i)
        pddlDatalogAtomFree(dl, &rule->neg_body[i]);
    if (rule->neg_body != NULL)
        FREE(rule->neg_body);
    pddlISetFree(&rule->var_set);
    pddlISetFree(&rule->common_body_var_set);
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
        rule->body = REALLOC_ARR(rule->body, pddl_datalog_atom_t,
                                     rule->body_alloc);
    }
    pddl_datalog_atom_t *a = rule->body + rule->body_size++;
    pddlDatalogAtomCopy(dl, a, atom);
}

void pddlDatalogRuleAddNegStaticBody(pddl_datalog_t *dl,
                                     pddl_datalog_rule_t *rule,
                                     const pddl_datalog_atom_t *atom)
{
    if (rule->neg_body_size == rule->neg_body_alloc){
        if (rule->neg_body_alloc == 0)
            rule->neg_body_alloc = 1;
        rule->neg_body_alloc *= 2;
        rule->neg_body = REALLOC_ARR(rule->neg_body, pddl_datalog_atom_t,
                                         rule->neg_body_alloc);
    }
    pddl_datalog_atom_t *a = rule->neg_body + rule->neg_body_size++;
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
    PDDL_ISET(body_vars);
    for (int i = 0; i < rule->body_size; ++i){
        int arity = dl->pred[rule->body[i].pred].arity;
        for (int j = 0; j < arity; ++j){
            if (IS_VAR(rule->body[i].arg[j]))
                pddlISetAdd(&body_vars, TO_IDX(rule->body[i].arg[j]));
        }
    }
    int arity = dl->pred[rule->head.pred].arity;
    for (int j = 0; j < arity; ++j){
        if (IS_VAR(rule->head.arg[j])){
            if (!pddlISetIn(TO_IDX(rule->head.arg[j]), &body_vars))
                return 0;
        }
    }
    pddlISetFree(&body_vars);

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
            fprintf(fout, "_X%d", idx);
        }

    }else if (IS_PRED(id)){
        if (dl->pred[idx].name != NULL){
            fprintf(fout, "%s", dl->pred[idx].name);
        }else{
            fprintf(fout, "_p%d", idx);
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
        if (c->body_size > 0 || c->neg_body_size > 0)
            fprintf(fout, " :- ");
        if (c->body_size > 0){
            printAtom(dl, c->body + 0, fout);
            for (int i = 1; i < c->body_size; ++i){
                fprintf(fout, ", ");
                printAtom(dl, c->body + i, fout);
            }
        }
        if (c->neg_body_size > 0){
            if (c->body_size > 0)
                fprintf(fout, ", ");
            fprintf(fout, "!");
            printAtom(dl, c->neg_body + 0, fout);
            for (int i = 1; i < c->neg_body_size; ++i){
                fprintf(fout, ", !");
                printAtom(dl, c->neg_body + i, fout);
            }
        }
        fprintf(fout, ".\n");
    }
}
