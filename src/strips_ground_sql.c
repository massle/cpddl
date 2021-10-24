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
#include "pddl/strips_ground_sql.h"
#include "pddl/prep_action.h"
#include "pddl/ground_atom.h"
#include "pddl/strips_maker.h"
#include "pddl/sql_grounder.h"
#include "assert.h"


struct sql_ground {
    pddl_sql_grounder_t *grounder;
    const pddl_t *pddl;
    pddl_strips_maker_t strips_maker;
};
typedef struct sql_ground sql_ground_t;

static int sqlGroundInit(sql_ground_t *g,
                         const pddl_t *pddl,
                         const pddl_ground_config_t *cfg,
                         bor_err_t *err)
{
    bzero(g, sizeof(*g));
    g->pddl = pddl;
    g->grounder = pddlSqlGrounderNew(pddl, err);
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
            pddlSqlGrounderInsertAtom(g->grounder, a, err);

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
    pddlSqlGrounderDel(g->grounder);
    pddlStripsMakerFree(&g->strips_maker);
}

static int addGroundAction(sql_ground_t *g,
                           int action_id,
                           const pddl_obj_id_t *row)
{
    const pddl_prep_action_t *paction;
    paction = pddlSqlGrounderPrepAction(g->grounder, action_id);
    int is_new = 0;
    int parent_id = action_id;
    if (paction->parent_action >= 0)
        parent_id = paction->parent_action;
    pddlStripsMakerAddAction(&g->strips_maker,
                             parent_id,
                             (parent_id == action_id ? 0 : action_id),
                             row,
                             &is_new);
    return is_new;
}

static int addGroundAtom(sql_ground_t *g,
                         const pddl_cond_atom_t *atom,
                         const pddl_obj_id_t *row,
                         bor_err_t *err)
{
    int is_new = 0;
    pddl_ground_atom_t *ga;
    ga = pddlStripsMakerAddAtom(&g->strips_maker, atom, row, &is_new);
    if (is_new)
        return pddlSqlGrounderInsertAtomArgs(g->grounder, atom->pred, ga->arg, err);
    return 0;
}

static int sqlGroundStepActionRow(sql_ground_t *g,
                                  int action_id,
                                  pddl_obj_id_t *row,
                                  bor_err_t *err)
{
    int updated = 0;

    // Try to add a new ground action
    if (!addGroundAction(g, action_id, row))
        return 0;

    const pddl_prep_action_t *paction;
    paction = pddlSqlGrounderPrepAction(g->grounder, action_id);
    for (int i = 0; i < paction->add_eff.size; ++i){
        const pddl_cond_atom_t *atom;
        atom = PDDL_COND_CAST(paction->add_eff.cond[i], atom);

        ASSERT(!pddlPredIsStatic(&g->pddl->pred.pred[atom->pred]));
        updated |= addGroundAtom(g, atom, row, err);
    }

    return updated;
}


static int sqlGroundStepAction(sql_ground_t *g, int action_id, bor_err_t *err)
{
    if (pddlSqlGrounderActionStart(g->grounder, action_id, err) != 0)
        return 0;

    const pddl_prep_action_t *paction;
    paction = pddlSqlGrounderPrepAction(g->grounder, action_id);

    pddl_obj_id_t row[paction->param_size];
    int updated = 0;
    while (pddlSqlGrounderActionNext(g->grounder, row, err))
        updated |= sqlGroundStepActionRow(g, action_id, row, err);
    return updated;
}

static int sqlGroundStep(sql_ground_t *g, bor_err_t *err)
{
    int action_size = pddlSqlGrounderPrepActionSize(g->grounder);
    int updated = 0;
    for (int ai = 0; ai < action_size; ++ai)
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
    BOR_INFO(err, "Grounding finished: %d (split) actions, %d facts,"
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
