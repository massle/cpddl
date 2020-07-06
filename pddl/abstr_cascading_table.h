/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
 * AI Center, Department of Computer Science,
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

#ifndef __PDDL_ABSTR_CASCADING_TABLE_H__
#define __PDDL_ABSTR_CASCADING_TABLE_H__

#include <boruvka/iset.h>
#include <boruvka/iarr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct pddl_abstr_cascading_table pddl_abstr_cascading_table_t;

/**
 * Free allocated memory.
 */
void pddlAbstrCascadingTableDel(pddl_abstr_cascading_table_t *t);

/**
 * Creates a deep copy of the given table.
 */
pddl_abstr_cascading_table_t *
    pddlAbstrCascadingTableClone(const pddl_abstr_cascading_table_t *t);

/**
 * Creates a new table from the given mutex group.
 */
pddl_abstr_cascading_table_t *pddlAbstrCascadingTableNewLeaf(int id, int size);

/**
 * Merges two tables into another one.
 */
pddl_abstr_cascading_table_t *
    pddlAbstrCascadingTableMerge(pddl_abstr_cascading_table_t *t1,
                                 pddl_abstr_cascading_table_t *t2);

/**
 * Further abstract the table.
 * The array abstractions maps IDs between 0 and t->size - 1 to new IDs from
 * the same range or to negative number if such state should be pruned.
 */
void pddlAbstrCascadingTableAbstract(pddl_abstr_cascading_table_t *t,
                                     const bor_iarr_t *abstraction);

/**
 * Returns value corresponding to the given state.
 */
int pddlAbstrCascadingTableValueFromState(pddl_abstr_cascading_table_t *t,
                                          const int *state);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_ABSTR_CASCADING_TABLE_H__ */
