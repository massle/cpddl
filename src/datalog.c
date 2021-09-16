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
#include <sqlite3.h>
#include <boruvka/alloc.h>
#include "pddl/datalog.h"
#include "assert.h"

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
    bor_iset_t relevant_rules;
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
    sqlite3 *db;
    sqlite3_stmt *q_insert_fact;
    sqlite3_stmt **q_find_fact;
    int q_find_fact_size;
    sqlite3_stmt *q_list_fact;
    sqlite3_stmt *q_get_fact;
    sqlite3_stmt *q_insert_body[2];
    sqlite3_stmt **q_search_body[2];
    int q_search_body_size;
    int fact_size;
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

#define CHECK_SQL_ERR(db, code) \
    do { \
    if ((code) != SQLITE_OK){ \
        BOR_FATAL("Sqlite Error: %s: %s\n", \
                  sqlite3_errstr(code), sqlite3_errmsg(db)); \
    } \
    } while (0)

#define QUERY_SIZE 4096

static void sqlCreateFactTable(pddl_datalog_t *dl)
{
    char query[QUERY_SIZE];
    int off = 0;
    off += sprintf(query, "CREATE TABLE fact (ID int, pred int");
    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ", arg%d int", i);
    off += sprintf(query + off, ");");

    int ret = sqlite3_exec(dl->db, query, NULL, NULL, NULL);
    CHECK_SQL_ERR(dl->db, ret);
}

static void sqlPrepareQueryInsertFact(pddl_datalog_t *dl)
{
    char query[QUERY_SIZE];
    int off = 0;
    off += sprintf(query, "INSERT INTO fact values(?,?");
    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ",?");
    off += sprintf(query + off, ");");

    fprintf(stderr, "sql-query insert-fact: %s\n", query);
    int ret = sqlite3_prepare_v2(dl->db, query, -1, &dl->q_insert_fact, NULL);
    CHECK_SQL_ERR(dl->db, ret);
}

static void sqlPrepareQueryFindFact(pddl_datalog_t *dl)
{
    dl->q_find_fact_size = dl->max_pred_arity + 1;
    dl->q_find_fact = BOR_ALLOC_ARR(sqlite3_stmt *, dl->q_find_fact_size);
    char query[QUERY_SIZE];
    for (int arity = 0; arity < dl->q_find_fact_size; ++arity){
        int off = 0;
        off += sprintf(query, "SELECT ID FROM fact WHERE pred = ?");
        for (int i = 0; i < arity; ++i)
            off += sprintf(query + off, " AND arg%d = ?", i);
        off += sprintf(query + off, ";");

        fprintf(stderr, "sql-query find-fact[%d]: %s\n", arity, query);
        int ret = sqlite3_prepare_v2(dl->db, query, -1,
                                     &dl->q_find_fact[arity], NULL);
        CHECK_SQL_ERR(dl->db, ret);
    }
}

static void sqlPrepareQueryListFact(pddl_datalog_t *dl)
{
    char query[QUERY_SIZE];
    int off = 0;
    off += sprintf(query, "SELECT pred");
    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ", arg%d", i);
    off += sprintf(query + off, " FROM fact WHERE pred = ?");
    off += sprintf(query + off, ";");

    fprintf(stderr, "sql-query list-fact: %s\n", query);
    int ret = sqlite3_prepare_v2(dl->db, query, -1, &dl->q_list_fact, NULL);
    CHECK_SQL_ERR(dl->db, ret);
}

static void sqlPrepareQueryGetFact(pddl_datalog_t *dl)
{
    char query[QUERY_SIZE];
    int off = 0;
    off += sprintf(query, "SELECT pred");
    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ", arg%d", i);
    off += sprintf(query + off," FROM fact WHERE ID = ?;");
    fprintf(stderr, "sql-query get-fact: %s\n", query);
    int ret = sqlite3_prepare_v2(dl->db, query, -1, &dl->q_get_fact, NULL);
    CHECK_SQL_ERR(dl->db, ret);
}

static int sqlHasFact(pddl_datalog_t *dl, int pred, const int *arg)
{
    int arity = dl->pred[pred].arity;
    ASSERT(dl->q_find_fact[arity] != NULL);
    sqlite3_reset(dl->q_find_fact[arity]);
    sqlite3_clear_bindings(dl->q_find_fact[arity]);

    int ret = sqlite3_bind_int(dl->q_find_fact[arity], 1, pred);
    CHECK_SQL_ERR(dl->db, ret);

    for (int i = 0; i < arity; ++i){
        int ret = sqlite3_bind_int(dl->q_find_fact[arity], i + 2, arg[i]);
        CHECK_SQL_ERR(dl->db, ret);
    }

    int found = (ret = sqlite3_step(dl->q_find_fact[arity])) == SQLITE_ROW;
    if (ret != SQLITE_ROW && ret != SQLITE_DONE)
        CHECK_SQL_ERR(dl->db, ret);
    return found;
}

static int sqlGetFact(pddl_datalog_t *dl, int id, int *pred, int *arg)
{
    sqlite3_reset(dl->q_get_fact);
    sqlite3_clear_bindings(dl->q_get_fact);
    int ret = sqlite3_bind_int(dl->q_get_fact, 1, id);
    CHECK_SQL_ERR(dl->db, ret);

    int found = (ret = sqlite3_step(dl->q_get_fact)) == SQLITE_ROW;
    if (ret != SQLITE_ROW && ret != SQLITE_DONE)
        CHECK_SQL_ERR(dl->db, ret);
    if (found){
        *pred = sqlite3_column_int(dl->q_get_fact, 0);
        int arity = dl->pred[*pred].arity;
        for (int i = 0; i < arity; ++i)
            arg[i] = sqlite3_column_int(dl->q_get_fact, i + 1);
    }
    return found;
}

static void sqlListFactsPrepare(pddl_datalog_t *dl,
                                int pred,
                                sqlite3_stmt **stmt)
{
    *stmt = dl->q_list_fact;
    ASSERT(*stmt != NULL);
    sqlite3_reset(*stmt);
    sqlite3_clear_bindings(*stmt);

    int ret = sqlite3_bind_int(*stmt, 1, pred);
    CHECK_SQL_ERR(dl->db, ret);
}

static int sqlListFactsNext(pddl_datalog_t *dl,
                            sqlite3_stmt *stmt,
                            int *pred,
                            int *arg)
{
    int ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW && ret != SQLITE_DONE)
        CHECK_SQL_ERR(dl->db, ret);
    if (ret == SQLITE_ROW){
        *pred = sqlite3_column_int(stmt, 0);
        int arity = dl->pred[*pred].arity;
        for (int i = 0; i < arity; ++i)
            arg[i] = sqlite3_column_int(stmt, i + 1);
        return 0;
    }
    return -1;
}

static void sqlInsertFact(pddl_datalog_t *dl, int pred, const int *arg)
{
    fprintf(stderr, "insert fact: %d (%d):", pred, dl->pred[pred].arity);
    for (int i = 0; i < dl->pred[pred].arity; ++i)
        fprintf(stderr, " %d", arg[i]);
    fprintf(stderr, "\n");

    ASSERT(dl->q_insert_fact != NULL);
    sqlite3_reset(dl->q_insert_fact);
    sqlite3_clear_bindings(dl->q_insert_fact);

    int ret = sqlite3_bind_int(dl->q_insert_fact, 1, dl->fact_size);
    CHECK_SQL_ERR(dl->db, ret);

    ret = sqlite3_bind_int(dl->q_insert_fact, 2, pred);
    CHECK_SQL_ERR(dl->db, ret);

    for (int i = 0; i < dl->pred[pred].arity; ++i){
        int ret = sqlite3_bind_int(dl->q_insert_fact, i + 3, arg[i]);
        CHECK_SQL_ERR(dl->db, ret);
    }

    ret = sqlite3_step(dl->q_insert_fact);
    if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT)
        CHECK_SQL_ERR(dl->db, ret);
    if (ret == SQLITE_DONE)
        ++dl->fact_size;
}

static void sqlCreateBodyTable(pddl_datalog_t *dl, int bid)
{
    char query[QUERY_SIZE];
    int off = 0;
    off += sprintf(query, "CREATE TABLE body%d (key_rule int", bid);

    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ", key_arg%d", i);

    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ", val_arg%d", i);
    off += sprintf(query + off, ");");

    int ret = sqlite3_exec(dl->db, query, NULL, NULL, NULL);
    CHECK_SQL_ERR(dl->db, ret);

    sprintf(query, "CREATE INDEX index_body%d ON body%d (key_rule);",
            bid, bid);
    ret = sqlite3_exec(dl->db, query, NULL, NULL, NULL);
    CHECK_SQL_ERR(dl->db, ret);
}

static void sqlPrepareQueryInsertBody(pddl_datalog_t *dl, int bid)
{
    char query[QUERY_SIZE];
    int off = 0;
    off += sprintf(query, "INSERT INTO body%d values(?", bid);
    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ",?");
    for (int i = 0; i < dl->max_pred_arity; ++i)
        off += sprintf(query + off, ",?");
    off += sprintf(query + off, ");");

    fprintf(stderr, "sql-query insert-body[%d]: %s\n", bid, query);
    int ret = sqlite3_prepare_v2(dl->db, query, -1,
                                 &dl->q_insert_body[bid], NULL);
    CHECK_SQL_ERR(dl->db, ret);
}

static void sqlPrepareQuerySearchBody(pddl_datalog_t *dl, int bid)
{
    dl->q_search_body_size = dl->max_pred_arity + 1;
    dl->q_search_body[bid] = BOR_ALLOC_ARR(sqlite3_stmt *,
                                           dl->q_search_body_size);
    for (int arity = 0; arity < dl->q_search_body_size; ++arity){
        char query[QUERY_SIZE];
        int off = 0;
        off += sprintf(query, "SELECT ");
        for (int i = 0; i < dl->max_pred_arity; ++i){
            if (i > 0)
                off += sprintf(query + off, ",");
            off += sprintf(query + off, "val_arg%d", i);
        }
        off += sprintf(query + off, " FROM body%d WHERE key_rule = ?", bid);
        for (int i = 0; i < arity; ++i)
            off += sprintf(query + off, " AND key_arg%d = ?", i);
        off += sprintf(query + off, ";");

        fprintf(stderr, "sql-query search-body[%d][%d]: %s\n", bid, arity, query);
        int ret = sqlite3_prepare_v2(dl->db, query, -1,
                                     &dl->q_search_body[bid][arity], NULL);
        CHECK_SQL_ERR(dl->db, ret);
    }
}

static void sqlInsertBody(pddl_datalog_t *dl,
                          int bid,
                          int rule,
                          const bor_iset_t *key_vars,
                          const bor_iset_t *val_vars,
                          const int *var_map)
{
    {
    fprintf(stderr, "insert body: %d:", rule);
    int var;
    BOR_ISET_FOR_EACH(key_vars, var)
        fprintf(stderr, " %d:%d", var, var_map[var]);
    fprintf(stderr, " ->");
    BOR_ISET_FOR_EACH(val_vars, var)
        fprintf(stderr, " %d:%d", var, var_map[var]);
    fprintf(stderr, "\n");
    }

    ASSERT(dl->q_insert_body[bid] != NULL);
    sqlite3_reset(dl->q_insert_body[bid]);
    sqlite3_clear_bindings(dl->q_insert_body[bid]);

    int ret = sqlite3_bind_int(dl->q_insert_body[bid], 1, rule);
    CHECK_SQL_ERR(dl->db, ret);

    int var;
    int i = 0;
    BOR_ISET_FOR_EACH(key_vars, var){
        int ret = sqlite3_bind_int(dl->q_insert_body[bid], i + 2, var_map[var]);
        CHECK_SQL_ERR(dl->db, ret);
        ++i;
    }

    i = 0;
    BOR_ISET_FOR_EACH(val_vars, var){
        int index = 1 + 1 + dl->max_pred_arity + i;
        int ret = sqlite3_bind_int(dl->q_insert_body[bid], index, var_map[var]);
        CHECK_SQL_ERR(dl->db, ret);
        ++i;
    }

    ret = sqlite3_step(dl->q_insert_body[bid]);
    if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT)
        CHECK_SQL_ERR(dl->db, ret);
}

static void sqlSearchBodyPrepare(pddl_datalog_t *dl,
                                 int bid,
                                 int rule,
                                 const bor_iset_t *key_vars,
                                 const int *var_map,
                                 sqlite3_stmt **stmt)
{
    int size = borISetSize(key_vars);
    *stmt = dl->q_search_body[bid][size];

    ASSERT(*stmt != NULL);
    sqlite3_reset(*stmt);
    sqlite3_clear_bindings(*stmt);

    int ret = sqlite3_bind_int(*stmt, 1, rule);
    CHECK_SQL_ERR(dl->db, ret);

    int var;
    int i = 0;
    BOR_ISET_FOR_EACH(key_vars, var){
        int ret = sqlite3_bind_int(*stmt, i + 2, var_map[var]);
        CHECK_SQL_ERR(dl->db, ret);
        ++i;
    }
}

static int sqlSearchBodyNext(pddl_datalog_t *dl,
                             sqlite3_stmt *stmt,
                             const bor_iset_t *val_vars,
                             int *var_map)
{
    int ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW && ret != SQLITE_DONE)
        CHECK_SQL_ERR(dl->db, ret);
    if (ret == SQLITE_ROW){
        int var;
        int i = 0;
        BOR_ISET_FOR_EACH(val_vars, var){
            var_map[var] = sqlite3_column_int(stmt, i);
            ++i;
        }
        return 0;
    }
    return -1;
}

static void sqlDBFree(pddl_datalog_t *dl, bor_err_t *err)
{
    if (dl->db != NULL){
        sqlite3_finalize(dl->q_insert_fact);
        for (int i = 0; i < dl->q_find_fact_size; ++i)
            sqlite3_finalize(dl->q_find_fact[i]);
        BOR_FREE(dl->q_find_fact);
        sqlite3_finalize(dl->q_list_fact);
        sqlite3_finalize(dl->q_insert_body[0]);
        sqlite3_finalize(dl->q_insert_body[1]);
        for (int i = 0; i < dl->q_search_body_size; ++i){
            sqlite3_finalize(dl->q_search_body[0][i]);
            sqlite3_finalize(dl->q_search_body[1][i]);
        }
        BOR_FREE(dl->q_search_body[0]);
        BOR_FREE(dl->q_search_body[1]);
        int ret = sqlite3_close_v2(dl->db);
        CHECK_SQL_ERR(dl->db, ret);
        dl->db = NULL;
    }
}

static void sqlDBInit(pddl_datalog_t *dl, bor_err_t *err)
{
    sqlDBFree(dl, err);
    int flags = SQLITE_OPEN_READWRITE
                    | SQLITE_OPEN_CREATE
                    //| SQLITE_OPEN_MEMORY
                    | SQLITE_OPEN_PRIVATECACHE;
    unlink("datalog.db");
    int ret = sqlite3_open_v2("datalog.db", &dl->db, flags, NULL);
    CHECK_SQL_ERR(dl->db, ret);
    BOR_INFO2(err, "Sqlite database created");
    ASSERT_RUNTIME(sqlite3_get_autocommit(dl->db));

    sqlCreateFactTable(dl);
    sqlCreateBodyTable(dl, 0);
    sqlCreateBodyTable(dl, 1);
    BOR_INFO2(err, "Sqlite database: created 3 tables");
    sqlPrepareQueryInsertFact(dl);
    sqlPrepareQueryFindFact(dl);
    sqlPrepareQueryGetFact(dl);
    sqlPrepareQueryListFact(dl);
    sqlPrepareQueryInsertBody(dl, 0);
    sqlPrepareQueryInsertBody(dl, 1);
    sqlPrepareQuerySearchBody(dl, 0);
    sqlPrepareQuerySearchBody(dl, 1);
    BOR_INFO(err, "Sqlite database: prepared %d queries",
             1 + dl->q_find_fact_size + 1 + 2 + 2 * dl->q_search_body_size);
}

static void atomSetUp(pddl_datalog_t *dl, pddl_datalog_atom_t *atom)
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
    borISetEmpty(&rule->common_body_var_set);
    for (int i = 0; i < rule->body_size; ++i){
        atomSetUp(dl, rule->body + i);
        if (i == 0){
            borISetUnion(&rule->common_body_var_set, &rule->body[0].var_set);
        }else{
            borISetIntersect(&rule->common_body_var_set,
                             &rule->body[i].var_set);
        }

        borISetUnion(&body_vars, &rule->body[i].var_set);
    }
    rule->is_safe = borISetIsSubset(&rule->head.var_set, &body_vars);
    rule->same_head_body_vars = borISetEq(&body_vars, &rule->head.var_set);
    borISetFree(&body_vars);
}

static void predsSetUp(pddl_datalog_t *dl)
{
    for (int p = 0; p < dl->pred_size; ++p)
        borISetEmpty(&dl->pred[p].relevant_rules);

    for (int r = 0; r < dl->rule_size; ++r){
        const pddl_datalog_rule_t *rule = dl->rule + r;
        if (rule->body_size == 0)
            continue;
        for (int b = 0; b < rule->body_size; ++b){
            int pred = rule->body[b].pred;
            ASSERT(pred < dl->pred_size);
            borISetAdd(&dl->pred[pred].relevant_rules, r);
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
            sqlDBInit(dl, err);

    }else if (db && dl->db == NULL){
        sqlDBInit(dl, err);
    }
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
    sqlDBFree(dl, NULL);
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
    bzero(c, sizeof(*c));
    c->idx = dl->c_size++;
    c->id = IDX_TO_CONST(c->idx);
    c->name = NULL;
    if (name != NULL)
        c->name = BOR_STRDUP(name);
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
        dl->pred = BOR_REALLOC_ARR(dl->pred, pddl_datalog_pred_t,
                                   dl->pred_alloc);
    }
    pddl_datalog_pred_t *p = dl->pred + dl->pred_size;
    bzero(p, sizeof(*p));
    p->idx = dl->pred_size++;
    p->id = IDX_TO_PRED(p->idx);
    p->arity = arity;
    p->name = NULL;
    if (name != NULL)
        p->name = BOR_STRDUP(name);
    p->user_id = -1;
    dl->max_pred_arity = BOR_MAX(dl->max_pred_arity, arity);
    dl->dirty = 1;
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
    bzero(v, sizeof(*v));
    v->idx = dl->var_size++;
    v->id = IDX_TO_VAR(v->idx);
    v->name = NULL;
    if (name != NULL)
        v->name = BOR_STRDUP(name);
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
        dl->rule = BOR_REALLOC_ARR(dl->rule, pddl_datalog_rule_t,
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
                     bor_iset_t *vars)
{
    borISetEmpty(vars);
    borISetUnion2(vars, &a1->var_set, &a2->var_set);

    if (rule->same_head_body_vars){
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
        if (!sqlHasFact(dl, atom->pred, args))
            sqlInsertFact(dl, atom->pred, args);
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
    BOR_ISET_FOR_EACH(&atom->var_set, var)
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

    fprintf(stderr, "unified %d:%s(", fact_pred, dl->pred[fact_pred].name);
    for (int i = 0; i < arity; ++i)
        fprintf(stderr, " %d", fact_arg[i]);
    fprintf(stderr, "):");
    BOR_ISET_FOR_EACH(&atom->var_set, var){
        fprintf(stderr, " %d->%d", var, var_map[var]);
    }
    fprintf(stderr, "\n");

    return 0;
}

static void headToFact(pddl_datalog_t *dl,
                       const pddl_datalog_atom_t *head,
                       const int *var_map)
{
    int arity = dl->pred[head->pred].arity;
    int arg[arity];
    for (int i = 0; i < arity; ++i){
        if (IS_VAR(head->arg[i])){
            arg[i] = var_map[TO_IDX(head->arg[i])];
        }else{
            arg[i] = TO_IDX(head->arg[i]);
        }
    }
    fprintf(stderr, "new fact %d:%s\n", head->pred,
            dl->pred[head->pred].name);
    if (!sqlHasFact(dl, head->pred, arg))
        sqlInsertFact(dl, head->pred, arg);
}

static void applyFactOnJoinRule(pddl_datalog_t *dl,
                                int atom_idx,
                                int rule_id,
                                int *var_map,
                                bor_err_t *err)
{
    const pddl_datalog_rule_t *rule = dl->rule + rule_id;
    int other_atom_idx = (atom_idx + 1) % 2;
    const pddl_datalog_atom_t *b0 = &rule->body[atom_idx];
    const pddl_datalog_atom_t *b1 = &rule->body[other_atom_idx];
    const bor_iset_t *key_var = &rule->common_body_var_set;
    sqlInsertBody(dl, atom_idx, rule_id, key_var, &b0->var_set, var_map);
    sqlite3_stmt *stmt;
    sqlSearchBodyPrepare(dl, other_atom_idx, rule_id, key_var, var_map, &stmt);
    while (sqlSearchBodyNext(dl, stmt, &b1->var_set, var_map) == 0)
        headToFact(dl, &rule->head, var_map);
}

static void applyFactOnRule(pddl_datalog_t *dl,
                            int fact_pred,
                            const int *fact_arg,
                            int rule_id,
                            bor_err_t *err)
{
    const pddl_datalog_rule_t *rule = dl->rule + rule_id;
    int var_map[dl->var_size];
    if (rule->body_size == 1
            && unify(dl, fact_pred, fact_arg, &rule->body[0], var_map) == 0){
        headToFact(dl, &rule->head, var_map);
    }

    if (rule->body_size == 2
            && unify(dl, fact_pred, fact_arg, &rule->body[0], var_map) == 0){
        applyFactOnJoinRule(dl, 0, rule_id, var_map, err);
    }

    if (rule->body_size == 2
            && unify(dl, fact_pred, fact_arg, &rule->body[1], var_map) == 0){
        applyFactOnJoinRule(dl, 1, rule_id, var_map, err);
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
    BOR_INFO(err, "Added initial facts: %d", dl->fact_size);

    int cur_id = 0;
    int cur_pred;
    int cur_arg[dl->max_pred_arity];
    while (cur_id < dl->fact_size){
        sqlGetFact(dl, cur_id, &cur_pred, cur_arg);
        int rule_id;
        BOR_ISET_FOR_EACH(&dl->pred[cur_pred].relevant_rules, rule_id)
            applyFactOnRule(dl, cur_pred, cur_arg, rule_id, err);
        ++cur_id;
        if (cur_id % 1000 == 0)
            BOR_INFO(err, "progress (facts processed: %d, overall: %d)",
                     cur_id, dl->fact_size);
    }
    BOR_INFO(err, "DONE (facts: %d)", dl->fact_size);
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
    sqlite3_stmt *stmt;
    sqlListFactsPrepare(dl, TO_IDX(pred), &stmt);

    int arity = dl->pred[TO_IDX(pred)].arity;
    int p;
    int arg[arity];
    while (sqlListFactsNext(dl, stmt, &p, arg) == 0){
        fprintf(stderr, "next %d\n", TO_IDX(pred));
        p = dl->pred[p].user_id;
        for (int i = 0; i < arity; ++i)
            arg[i] = dl->c[arg[i]].user_id;
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
