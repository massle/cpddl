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

#ifndef __PDDL_SQL_GROUNDER_H__
#define __PDDL_SQL_GROUNDER_H__

#include <pddl/common.h>
#include <pddl/pddl_struct.h>
#include <pddl/ground_atom.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct pddl_sql_grounder pddl_sql_grounder_t;

pddl_sql_grounder_t *pddlSqlGrounderNew(const pddl_t *pddl, bor_err_t *err);
void pddlSqlGrounderDel(pddl_sql_grounder_t *g);

int pddlSqlGrounderInsertAtomArgs(pddl_sql_grounder_t *g,
                                  int pred_id,
                                  const pddl_obj_id_t *args,
                                  bor_err_t *err);
int pddlSqlGrounderInsertGroundAtom(pddl_sql_grounder_t *g,
                                    const pddl_ground_atom_t *ga,
                                    bor_err_t *err);
int pddlSqlGrounderInsertAtom(pddl_sql_grounder_t *g,
                              const pddl_cond_atom_t *a,
                              bor_err_t *err);

int pddlSqlGrounderClearNonStatic(pddl_sql_grounder_t *g, bor_err_t *err);

int pddlSqlGrounderActionStart(pddl_sql_grounder_t *g,
                               int action_id,
                               bor_err_t *err);
int pddlSqlGrounderActionNext(pddl_sql_grounder_t *g,
                              pddl_obj_id_t *args,
                              bor_err_t *err);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SQL_GROUNDER_H__ */
