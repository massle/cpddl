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

#include <sqlite3.h>
#include <boruvka/alloc.h>
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include <boruvka/sort.h>
#include "pddl/strips_ground_sql.h"
#include "pddl/prep_action.h"
#include "pddl/ground_atom.h"
#include "pddl/strips_maker.h"
#include "assert.h"

#define QUERY_SIZE 4096
#define QUERY_SELECT_SIZE (5 * QUERY_SIZE)

struct sql_pred {
    int pred;
    int arity;
    char *table_name;
    sqlite3_stmt *stmt_atom;
    sqlite3_stmt *stmt_insert;
};
typedef struct sql_pred sql_pred_t;

struct sql_action {
    int param_size;
    sqlite3_stmt *stmt;
    int applied0;
};
typedef struct sql_action sql_action_t;

struct sql_ground {
    const pddl_t *pddl;
    pddl_prep_actions_t prep_action;
    sqlite3 *db;
    sql_pred_t *pred;
    sql_action_t *action;
    pddl_strips_maker_t strips_maker;
};
typedef struct sql_ground sql_ground_t;

#define CHECK_SQL_ERR(db, code) \
    do { \
    if ((code) != SQLITE_OK){ \
        BOR_FATAL("Sqlite Error: %s: %s\n", \
                  sqlite3_errstr(code), sqlite3_errmsg(db)); \
    } \
    } while (0)


static void createTypeTable(sqlite3 *db, const pddl_t *pddl, int type)
{
    char query[QUERY_SIZE];
    sprintf(query, "CREATE TABLE type_%d (t int, UNIQUE(t));", type);
    int ret = sqlite3_exec(db, query, NULL, NULL, NULL);
    CHECK_SQL_ERR(db, ret);

    int obj_size;
    const pddl_obj_id_t *objs;
    objs = pddlTypesObjsByType(&pddl->type, type, &obj_size);
    for (int i = 0; i < obj_size; ++i){
        sprintf(query, "INSERT INTO type_%d values(%d);", type, objs[i]);
        int ret = sqlite3_exec(db, query, NULL, NULL, NULL);
        CHECK_SQL_ERR(db, ret);
    }
}

static void createTypeTables(sqlite3 *db, const pddl_t *pddl)
{
    for (int type = 0; type < pddl->type.type_size; ++type)
        createTypeTable(db, pddl, type);
}

static void createPredTable(sqlite3 *db,
                            const char *table_name,
                            int param_size,
                            bor_err_t *err)
{
    char query[QUERY_SIZE];
    int shift = sprintf(query, "CREATE TABLE %s (", table_name);
    for (int i = 0; i < param_size; ++i){
        if (i != 0)
            shift += sprintf(query + shift, ",");
        shift += sprintf(query + shift, "x%d int", i);
    }
    shift += sprintf(query + shift, ", UNIQUE(");
    for (int i = 0; i < param_size; ++i){
        if (i != 0)
            shift += sprintf(query + shift, ",");
        shift += sprintf(query + shift, "x%d", i);
    }
    shift += sprintf(query + shift, "));");
    ASSERT_RUNTIME(shift < QUERY_SIZE);

    //BOR_INFO(err, "Predicate table: %s", query);
    int ret = sqlite3_exec(db, query, NULL, NULL, NULL);
    CHECK_SQL_ERR(db, ret);

    for (int i = 0; i < param_size; ++i){
        sprintf(query, "CREATE INDEX index_%s_%d ON %s (x%d);",
                table_name, i, table_name, i);
        int ret = sqlite3_exec(db, query, NULL, NULL, NULL);
        CHECK_SQL_ERR(db, ret);
    }
}

static void sqlPredInit(sql_pred_t *qpred,
                        sqlite3 *db,
                        const pddl_preds_t *preds,
                        int pred_id,
                        bor_err_t *err)
{
    const pddl_pred_t *pred = preds->pred + pred_id;
    bzero(qpred, sizeof(*qpred));
    qpred->pred = pred->id;
    qpred->arity = pred->param_size;

    if (pred_id == preds->eq_pred)
        return;

    qpred->table_name = BOR_ALLOC_ARR(char, 2 + strlen(pred->name) + 1);
    sprintf(qpred->table_name, "t_%s", pred->name);
    int len = strlen(qpred->table_name);
    for (int i = 0; i < len; ++i){
        if (qpred->table_name[i] == '-'
                || qpred->table_name[i] == '='){
            qpred->table_name[i] = '_';
        }
    }

    if (pred->param_size > 0){
        createPredTable(db, qpred->table_name, pred->param_size, err);
    }else{
        createPredTable(db, qpred->table_name, 1, err);
    }

    char query[QUERY_SELECT_SIZE];
    int shift = 0;
    shift += sprintf(query + shift, "SELECT ");
    for (int i = 0; i < qpred->arity; ++i){
        if (i != 0)
            shift += sprintf(query + shift, ",");
        shift += sprintf(query + shift, " x%d", i);
    }
    if (qpred->arity == 0)
        shift += sprintf(query + shift, " x0");
    shift += sprintf(query + shift, " FROM %s", qpred->table_name);
    shift += sprintf(query + shift, " WHERE");
    for (int i = 0; i < qpred->arity; ++i){
        if (i != 0)
            shift += sprintf(query + shift, " AND");
        shift += sprintf(query + shift, " x%d = ?", i);
    }
    if (qpred->arity == 0)
        shift += sprintf(query + shift, " x0 = 1");
    shift += sprintf(query + shift, ";");

    int ret = sqlite3_prepare_v2(db, query, -1, &qpred->stmt_atom, NULL);
    CHECK_SQL_ERR(db, ret);

    shift = sprintf(query, "INSERT INTO %s values(", qpred->table_name);
    for (int i = 0; i < qpred->arity; ++i){
        if (i != 0)
            shift += sprintf(query + shift, ",");
        shift += sprintf(query + shift, "?");
    }
    if (qpred->arity == 0)
        shift += sprintf(query + shift, "1");
    sprintf(query + shift, ");");
    ASSERT_RUNTIME(shift < QUERY_SIZE);

    //BOR_INFO(err, "Insert atom query: %s", query);
    ret = sqlite3_prepare_v2(db, query, -1, &qpred->stmt_insert, NULL);
    CHECK_SQL_ERR(db, ret);
}

static void sqlPredFree(sql_pred_t *qpred, sqlite3 *db)
{
    if (qpred->table_name != NULL)
        BOR_FREE(qpred->table_name);
    if (qpred->stmt_atom != NULL){
        int ret = sqlite3_finalize(qpred->stmt_atom);
        CHECK_SQL_ERR(db, ret);
    }
    if (qpred->stmt_insert != NULL){
        int ret = sqlite3_finalize(qpred->stmt_insert);
        CHECK_SQL_ERR(db, ret);
    }
}

static int sqlPredHasAtomArg(sql_pred_t *qpred,
                             sqlite3 *db,
                             const pddl_obj_id_t *arg)
{
    ASSERT(qpred->stmt_atom != NULL);
    for (int i = 0; i < qpred->arity; ++i){
        int ret = sqlite3_bind_int(qpred->stmt_atom, i + 1, arg[i]);
        CHECK_SQL_ERR(db, ret);
    }
    int ret;
    int found = (ret = sqlite3_step(qpred->stmt_atom)) == SQLITE_ROW;
    if (ret != SQLITE_ROW && ret != SQLITE_DONE)
        CHECK_SQL_ERR(db, ret);
    ret = sqlite3_reset(qpred->stmt_atom);
    CHECK_SQL_ERR(db, ret);
    return found;
}

static int sqlPredHasAtom(sql_pred_t *qpred,
                          sqlite3 *db,
                          const pddl_cond_atom_t *atom)
{
    pddl_obj_id_t arg[qpred->arity];
    for (int i = 0; i < qpred->arity; ++i){
        ASSERT(atom->arg[i].obj >= 0);
        arg[i] = atom->arg[i].obj;
    }
    return sqlPredHasAtomArg(qpred, db, arg);
}

static int sqlPredInsertAtomArg(sql_pred_t *qpred,
                                sqlite3 *db,
                                const pddl_obj_id_t *arg,
                                bor_err_t *err)
{
    int ret = sqlite3_reset(qpred->stmt_insert);
    CHECK_SQL_ERR(db, ret);
    for (int i = 0; i < qpred->arity; ++i){
        ASSERT(arg[i] >= 0);
        int ret = sqlite3_bind_int(qpred->stmt_insert, i + 1, arg[i]);
        CHECK_SQL_ERR(db, ret);
    }
    ret = sqlite3_step(qpred->stmt_insert);
    if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT)
        CHECK_SQL_ERR(db, ret);
    return ret == SQLITE_DONE;
}

static int sqlPredInsertAtom(sql_pred_t *qpred,
                             sqlite3 *db,
                             const pddl_cond_atom_t *atom,
                             bor_err_t *err)
{
    pddl_obj_id_t arg[qpred->arity];
    for (int i = 0; i < atom->arg_size; ++i){
        ASSERT(atom->arg[i].obj >= 0);
        arg[i] = atom->arg[i].obj;
    }
    return sqlPredInsertAtomArg(qpred, db, arg, err);
}


static void sqlActionConstructColumns(char *query,
                                      const pddl_prep_action_t *prep_action,
                                      bor_iset_t *type_tables)
{
    query[0] = 0x0;
    int shift = 0;
    for (int pi = 0; pi < prep_action->param_size; ++pi){
        if (pi != 0)
            shift += sprintf(query + shift, ", ");

        int found = 0;
        for (int ci = 0; ci < prep_action->pre.size; ++ci){
            const pddl_cond_t *c = prep_action->pre.cond[ci];
            const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
            for (int ai = 0; ai < atom->arg_size; ++ai){
                if (atom->arg[ai].param >= 0 && atom->arg[ai].param == pi){
                    shift += sprintf(query + shift, "tb%d.x%d as arg%d",
                                     ci, ai, pi);
                    found = 1;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found){
            int type = prep_action->param_type[pi];
            if (pddlTypeNumObjs(prep_action->type, type) > 0){
                shift += sprintf(query + shift, "tb_type%d.t as arg%d", pi, pi);
                borISetAdd(type_tables, pi);
            }else{
                shift += sprintf(query + shift, "-1");
            }
        }
    }
    ASSERT_RUNTIME(shift < QUERY_SIZE);
}

static void sqlActionConstructTables(char *query,
                                     const sql_pred_t *preds,
                                     const pddl_prep_action_t *prep_action,
                                     const bor_iset_t *type_tables)
{
    query[0] = 0x0;
    int shift = 0;
    for (int ci = 0; ci < prep_action->pre.size; ++ci){
        const pddl_cond_t *c = prep_action->pre.cond[ci];
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        if (ci != 0)
            shift += sprintf(query + shift, ", ");
        shift += sprintf(query + shift, "%s as tb%d",
                         preds[atom->pred].table_name, ci);
    }
    int idx;
    BOR_ISET_FOR_EACH(type_tables, idx){
        if (shift != 0)
            shift += sprintf(query + shift, ", ");
        int type = prep_action->param_type[idx];
        shift += sprintf(query + shift, "type_%d as tb_type%d", type, idx);
    }
    ASSERT_RUNTIME(shift < QUERY_SIZE);
}

static void sqlActionConstructJoinCond(char *query,
                                       const sql_pred_t *preds,
                                       const pddl_prep_action_t *prep_action)
{
    query[0] = 0x0;
    int ins = 0;
    int shift = 0;
    for (int ci1 = 0; ci1 < prep_action->pre.size; ++ci1){
        const pddl_cond_t *c1 = prep_action->pre.cond[ci1];
        const pddl_cond_atom_t *atom1 = PDDL_COND_CAST(c1, atom);
        for (int ci2 = ci1 + 1; ci2 < prep_action->pre.size; ++ci2){
            const pddl_cond_t *c2 = prep_action->pre.cond[ci2];
            const pddl_cond_atom_t *atom2 = PDDL_COND_CAST(c2, atom);

            for (int a1 = 0; a1 < atom1->arg_size; ++a1){
                if (atom1->arg[a1].param < 0)
                    continue;
                for (int a2 = 0; a2 < atom2->arg_size; ++a2){
                    if (atom2->arg[a2].param == atom1->arg[a1].param){
                        if (ins != 0){
                            shift += sprintf(query + shift, " AND ");
                        }else{
                            shift += sprintf(query + shift, "ON(");
                        }
                        shift += sprintf(query + shift, "tb%d.x%d = tb%d.x%d",
                                         ci1, a1, ci2, a2);
                        ++ins;
                    }
                }
            }
        }
    }
    if (query[0] != 0x0)
        shift += sprintf(query + shift, ")");
    ASSERT_RUNTIME(shift < QUERY_SIZE);
}

static int objsConsecutive(const pddl_obj_id_t *objs, int obj_size)
{
    for (int i = 1; i < obj_size; ++i){
        if (objs[i - 1] + 1 != objs[i])
            return 0;
    }
    return 1;
}
static void sqlActionConstructWhereCond(char *query,
                                        const sql_pred_t *preds,
                                        const pddl_prep_action_t *prep_action)
{
    // TODO: refactor
    query[0] = 0x0;
    int ins = 0;
    int shift = 0;
    for (int ci = 0; ci < prep_action->pre_eq.size; ++ci){
        const pddl_cond_t *c = prep_action->pre_eq.cond[ci];
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        if (atom->neg){
            if (atom->arg[0].param >= 0 && atom->arg[1].param >= 0){
                if (ins != 0){
                    shift += sprintf(query + shift, " AND ");
                }else{
                    shift += sprintf(query + shift, "WHERE ");
                }
                shift += sprintf(query + shift, "arg%d != arg%d",
                                 atom->arg[0].param, atom->arg[1].param);
                ++ins;

            }else if (atom->arg[0].param >= 0){
                if (ins != 0){
                    shift += sprintf(query + shift, " AND ");
                }else{
                    shift += sprintf(query + shift, "WHERE ");
                }
                shift += sprintf(query + shift, "arg%d != %d",
                                 atom->arg[0].param, atom->arg[1].obj);
                ++ins;

            }else if (atom->arg[1].param >= 0){
                if (ins != 0){
                    shift += sprintf(query + shift, " AND ");
                }else{
                    shift += sprintf(query + shift, "WHERE ");
                }
                shift += sprintf(query + shift, "arg%d != %d",
                                 atom->arg[1].param, atom->arg[0].obj);
                ++ins;

            }else{
                // TODO
            }
        }else{
            if (atom->arg[0].param >= 0 && atom->arg[1].param >= 0){
                if (ins != 0){
                    shift += sprintf(query + shift, " AND ");
                }else{
                    shift += sprintf(query + shift, "WHERE ");
                }
                shift += sprintf(query + shift, "arg%d = arg%d",
                                 atom->arg[0].param, atom->arg[1].param);
                ++ins;

            }else if (atom->arg[0].param >= 0){
                if (ins != 0){
                    shift += sprintf(query + shift, " AND ");
                }else{
                    shift += sprintf(query + shift, "WHERE ");
                }
                shift += sprintf(query + shift, "arg%d = %d",
                                 atom->arg[0].param, atom->arg[1].obj);
                ++ins;

            }else if (atom->arg[1].param >= 0){
                if (ins != 0){
                    shift += sprintf(query + shift, " AND ");
                }else{
                    shift += sprintf(query + shift, "WHERE ");
                }
                shift += sprintf(query + shift, "arg%d = %d",
                                 atom->arg[1].param, atom->arg[0].obj);
                ++ins;

            }else{
                // TODO
            }
        }
    }

    int used_param[prep_action->param_size];
    bzero(used_param, sizeof(int) * prep_action->param_size);
    for (int ci = 0; ci < prep_action->pre.size; ++ci){
        const pddl_cond_t *c = prep_action->pre.cond[ci];
        const pddl_cond_atom_t *atom = PDDL_COND_CAST(c, atom);
        for (int ai = 0; ai < atom->arg_size; ++ai){
            if (atom->arg[ai].param >= 0)
                used_param[atom->arg[ai].param] = 1;
        }

        if (atom->arg_size == 0){
            if (ins != 0){
                shift += sprintf(query + shift, " AND ");
            }else{
                shift += sprintf(query + shift, "WHERE ");
            }
            shift += sprintf(query + shift, "tb%d.x0 = 1", ci);
            ++ins;
        }else{
            for (int ai = 0; ai < atom->arg_size; ++ai){
                if (atom->arg[ai].obj >= 0){
                    if (ins != 0){
                        shift += sprintf(query + shift, " AND ");
                    }else{
                        shift += sprintf(query + shift, "WHERE ");
                    }
                    shift += sprintf(query + shift, "tb%d.x%d = %d",
                                     ci, ai, atom->arg[ai].obj);
                    ++ins;
                }
            }

        }
    }

    for (int pi = 0; pi < prep_action->param_size; ++pi){
        if (!used_param[pi])
            continue;

        int type = prep_action->param_type[pi];
        int obj_size;
        const pddl_obj_id_t *objs;
        objs = pddlTypesObjsByType(prep_action->type, type, &obj_size);
        if (ins != 0){
            shift += sprintf(query + shift, " AND ");
        }else{
            shift += sprintf(query + shift, "WHERE ");
        }
        if (obj_size == 0){
            // This action is not groundable, so add some dummy value
            shift += sprintf(query + shift, "arg%d = -10000", pi);
        } else if (obj_size == 1){
            shift += sprintf(query + shift, "arg%d = %d", pi, objs[0]);
        }else if (objsConsecutive(objs, obj_size)){
            shift += sprintf(query + shift, "arg%d >= %d AND arg%d <= %d",
                             pi, objs[0], pi, objs[obj_size - 1]);
        }else{
            shift += sprintf(query + shift, "arg%d IN (", pi);
            for (int oi = 0; oi < obj_size; ++oi){
                if (oi != 0)
                    shift += sprintf(query + shift, ",");
                shift += sprintf(query + shift, "%d", objs[oi]);
            }
            shift += sprintf(query + shift, ")");
        }
        ++ins;
    }
    ASSERT_RUNTIME(shift < QUERY_SIZE);
}

static void sqlActionInit(sql_action_t *action,
                          sqlite3 *db,
                          const sql_pred_t *preds,
                          const pddl_prep_action_t *prep_action,
                          bor_err_t *err)
{
    bzero(action, sizeof(*action));
    action->param_size = prep_action->param_size;

    if (action->param_size == 0)
        return;

    BOR_ISET(type_tables);
    char qcols[QUERY_SIZE];
    sqlActionConstructColumns(qcols, prep_action, &type_tables);
    char qtables[QUERY_SIZE];
    sqlActionConstructTables(qtables, preds, prep_action, &type_tables);
    char qjoincond[QUERY_SIZE];
    sqlActionConstructJoinCond(qjoincond, preds, prep_action);
    char qwhere[QUERY_SIZE];
    sqlActionConstructWhereCond(qwhere, preds, prep_action);
    borISetFree(&type_tables);

    char query[QUERY_SELECT_SIZE];
    // TODO: distinct?
    int used = sprintf(query, "SELECT %s FROM %s %s %s;",
                       qcols, qtables, qjoincond, qwhere);
    ASSERT_RUNTIME(used < QUERY_SELECT_SIZE);

    //BOR_INFO(err, "Action query %s: %s", prep_action->action->name, query);

    int ret = sqlite3_prepare_v2(db, query, -1, &action->stmt, NULL);
    CHECK_SQL_ERR(db, ret);
}

static void sqlActionFree(sql_action_t *action, sqlite3 *db)
{
    if (action->stmt != NULL){
        int ret = sqlite3_finalize(action->stmt);
        CHECK_SQL_ERR(db, ret);
    }
}

static int sqlGroundInit(sql_ground_t *g,
                         const pddl_t *pddl,
                         const pddl_ground_config_t *cfg,
                         bor_err_t *err)
{
    bzero(g, sizeof(*g));
    g->pddl = pddl;
    pddlPrepActionsInit(g->pddl, &g->prep_action, err);

    // Create a database
    int flags = SQLITE_OPEN_READWRITE
                    | SQLITE_OPEN_CREATE
                    | SQLITE_OPEN_MEMORY
                    | SQLITE_OPEN_PRIVATECACHE;
    int ret = sqlite3_open_v2("db.sql", &g->db, flags, NULL);
    CHECK_SQL_ERR(g->db, ret);
    BOR_INFO2(err, "Sqlite database created");
    ASSERT_RUNTIME(sqlite3_get_autocommit(g->db));

    // Create type tables
    createTypeTables(g->db, g->pddl);

    // Create sql predicates
    g->pred = BOR_CALLOC_ARR(sql_pred_t, pddl->pred.pred_size);
    for (int pi = 0; pi < pddl->pred.pred_size; ++pi)
        sqlPredInit(g->pred + pi, g->db, &g->pddl->pred, pi, err);
    BOR_INFO(err, "%d predicate tables created.", pddl->pred.pred_size);

    // Create sql actions
    g->action = BOR_CALLOC_ARR(sql_action_t, g->prep_action.action_size);
    for (int ai = 0; ai < g->prep_action.action_size; ++ai){
        sqlActionInit(g->action + ai, g->db, g->pred,
                      g->prep_action.action + ai, err);
    }
    BOR_INFO(err, "%d action sql queries prepared.",
             g->prep_action.action_size);

    pddlStripsMakerInit(&g->strips_maker, g->pddl);

    // Insert initial state
    bor_list_t *item;
    BOR_LIST_FOR_EACH(&g->pddl->init->part, item){
        const pddl_cond_t *c = BOR_LIST_ENTRY(item, pddl_cond_t, conn);
        if (c->type == PDDL_COND_ATOM){
            const pddl_cond_atom_t *a = PDDL_COND_CAST(c, atom);
            if (pddlPredIsStatic(&pddl->pred.pred[a->pred])){
                pddlStripsMakerAddStaticAtom(&g->strips_maker, a, NULL, NULL);
            }else{
                pddlStripsMakerAddAtom(&g->strips_maker, a, NULL, NULL);
            }
            sqlPredInsertAtom(g->pred + a->pred, g->db, a, err);

        }else if (c->type == PDDL_COND_ASSIGN){
            const pddl_cond_func_op_t *ass = PDDL_COND_CAST(c, func_op);
            ASSERT(ass->fvalue == NULL);
            ASSERT(ass->lvalue != NULL);
            ASSERT(pddlCondAtomIsGrounded(ass->lvalue));
            pddlStripsMakerAddFunc(&g->strips_maker, ass, NULL, NULL);
        }
    }
    BOR_INFO(err, "Initial state inserted."
                  " %d atoms, %d static atoms, %d functions",
             g->strips_maker.ground_atom.atom_size,
             g->strips_maker.ground_atom_static.atom_size,
             g->strips_maker.ground_func.atom_size);
    return 0;
}

static void sqlGroundFree(sql_ground_t *g)
{
    for (int pi = 0; pi < g->pddl->pred.pred_size; ++pi)
        sqlPredFree(g->pred + pi, g->db);
    if (g->pred != NULL)
        BOR_FREE(g->pred);
    for (int ai = 0; ai < g->prep_action.action_size; ++ai)
        sqlActionFree(g->action + ai, g->db);
    if (g->action != NULL)
        BOR_FREE(g->action);

    pddlPrepActionsFree(&g->prep_action);
    int ret = sqlite3_close_v2(g->db);
    CHECK_SQL_ERR(g->db, ret);

    pddlStripsMakerFree(&g->strips_maker);
}

static int actionCheckNegPreStatic(sql_ground_t *g,
                                   const pddl_prep_action_t *paction,
                                   const pddl_obj_id_t *row)
{
    for (int i = 0; i < paction->pre_neg_static.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(paction->pre_neg_static.cond[i], atom);
        if (atom->arg_size == 0){
            if (sqlPredHasAtom(g->pred + atom->pred, g->db, atom))
                return 0;
        }else{
            pddl_obj_id_t arg[atom->arg_size];
            for (int ai = 0; ai < atom->arg_size; ++ai){
                if (atom->arg[ai].obj >= 0){
                    arg[ai] = atom->arg[ai].obj;
                }else{
                    arg[ai] = row[atom->arg[ai].param];
                }
            }
            if (sqlPredHasAtomArg(g->pred + atom->pred, g->db, arg))
                return 0;
        }
    }

    return 1;
}

static int sqlGroundStepActionRow(sql_ground_t *g,
                                  int action_id,
                                  pddl_obj_id_t *row,
                                  bor_err_t *err)
{
    int updated = 0;

    // Ground add effects
    const pddl_prep_action_t *paction = g->prep_action.action + action_id;
    if (!actionCheckNegPreStatic(g, paction, row))
        return 0;

    int is_new = 0;
    int parent_id = action_id;
    if (paction->parent_action >= 0)
        parent_id = paction->parent_action;
    pddlStripsMakerAddAction(&g->strips_maker,
                             parent_id,
                             (parent_id == action_id ? 0 : action_id),
                             row,
                             &is_new);
    if (!is_new)
        return 0;

    for (int i = 0; i < paction->add_eff.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(paction->add_eff.cond[i], atom);

        ASSERT(!pddlPredIsStatic(&g->pddl->pred.pred[atom->pred]));
        int is_new = 0;
        pddl_ground_atom_t *ga;
        ga = pddlStripsMakerAddAtom(&g->strips_maker, atom, row, &is_new);
        if (is_new){
            updated |= sqlPredInsertAtomArg(g->pred + atom->pred, g->db,
                                            ga->arg, err);
        }
    }

    return updated;
}

static int actionCheckGroundPre(sql_ground_t *g,
                                const pddl_prep_action_t *paction)
{
    for (int i = 0; i < paction->pre_eq.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(paction->pre_eq.cond[i], atom);
        if (atom->neg){
            if (atom->arg[0].obj == atom->arg[1].obj)
                return 0;
        }else{
            if (atom->arg[0].obj != atom->arg[1].obj)
                return 0;
        }
    }

    for (int i = 0; i < paction->pre.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(paction->pre.cond[i], atom);
        if (!sqlPredHasAtom(g->pred + atom->pred, g->db, atom))
            return 0;
    }

    for (int i = 0; i < paction->pre_neg_static.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(paction->pre_neg_static.cond[i], atom);
        if (sqlPredHasAtom(g->pred + atom->pred, g->db, atom))
            return 0;
    }

    return 1;
}

static int sqlGroundStepAction(sql_ground_t *g, int action_id, bor_err_t *err)
{
    int updated = 0;
    sql_action_t *action = g->action + action_id;
    if (action->param_size == 0 && !action->applied0){
        const pddl_prep_action_t *paction = g->prep_action.action + action_id;
        if (actionCheckGroundPre(g, paction)){
            int updated = 0;
            for (int i = 0; i < paction->add_eff.size; ++i){
                const pddl_cond_atom_t *atom;
                atom = PDDL_COND_CAST(paction->add_eff.cond[i], atom);
                ASSERT(!pddlPredIsStatic(&g->pddl->pred.pred[atom->pred]));
                int is_new = 0;
                pddl_ground_atom_t *ga;
                ga = pddlStripsMakerAddAtom(&g->strips_maker, atom, NULL,
                                            &is_new);
                if (is_new){
                    updated |= sqlPredInsertAtomArg(g->pred + atom->pred, g->db,
                                                    ga->arg, err);
                }
            }
            action->applied0 = 1;
            int parent_id = action_id;
            if (paction->parent_action >= 0)
                parent_id = paction->parent_action;
            pddlStripsMakerAddAction(&g->strips_maker,
                                     parent_id,
                                     (parent_id == action_id ? 0 : action_id),
                                     NULL, NULL);
            BOR_INFO(err, "Applied empty-param action %s", paction->action->name);
            return updated;
        }
    }

    if (action->stmt == NULL)
        return 0;

    pddl_obj_id_t row[action->param_size];
    int ret;
    ret = sqlite3_reset(action->stmt);
    CHECK_SQL_ERR(g->db, ret);
    while ((ret = sqlite3_step(action->stmt)) == SQLITE_ROW){
        int invalid = 0;
        for (int i = 0; i < action->param_size; ++i){
            row[i] = sqlite3_column_int(action->stmt, i);
            if (row[i] < 0){
                invalid = 1;
                break;
            }
        }
        if (invalid){
            BOR_INFO2(err, "Invalid row");
            continue;
        }

        updated |= sqlGroundStepActionRow(g, action_id, row, err);
    }
    ASSERT_RUNTIME(ret == SQLITE_DONE);

    return updated;
}

static int sqlGroundStep(sql_ground_t *g, bor_err_t *err)
{
    int updated = 0;
    for (int ai = 0; ai < g->prep_action.action_size; ++ai)
        updated |= sqlGroundStepAction(g, ai, err);
    return updated;
}

int pddlStripsGroundSql(pddl_strips_t *strips,
                        const pddl_t *pddl,
                        const pddl_ground_config_t *cfg,
                        bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Ground SQL: ");
    BOR_INFO2(err, "Grounding using sqlite ...");

    sql_ground_t ground;
    sqlGroundInit(&ground, pddl, cfg, err);
    for (int step = 0; 1; ++step){
        BOR_INFO(err, "Grounding step %d"
                      " (%d (split) actions and %d facts grounded so far) ...",
                 step, ground.strips_maker.num_action_args,
                 ground.strips_maker.ground_atom.atom_size);
        if (!sqlGroundStep(&ground, err))
            break;
    }
    BOR_INFO(err, "Grounding of finished: %d (split) actions, %d facts,"
                  " %d static facts, %d functions",
             ground.strips_maker.num_action_args,
             ground.strips_maker.ground_atom.atom_size,
             ground.strips_maker.ground_atom_static.atom_size,
             ground.strips_maker.ground_func.atom_size);

    int ret = pddlStripsMakerMakeStrips(&ground.strips_maker, ground.pddl, cfg,
                                        strips, err);

    sqlGroundFree(&ground);
    if (ret != 0){
        BOR_INFO_PREFIX_POP(err);
        BOR_TRACE_RET(err, ret);
    }

    BOR_INFO2(err, "Grounding finished.");
    BOR_INFO_PREFIX_POP(err);
    return 0;
}
