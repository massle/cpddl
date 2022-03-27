/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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

#include "alloc.h"
#include "pddl/fdr_state_pool_htable.h"

void pddlFDRStatePoolHTableInit(pddl_fdr_state_pool_htable_t *ht,
                                const struct pddl_fdr_state_pool *state_pool)
{
    bzero(ht, sizeof(*ht));
    ht->size = 98317ul;
    ht->table = CALLOC_ARR(bor_iarr_t, ht->size);
    ht->state_pool = state_pool;
}

void pddlFDRStatePoolHTableFree(pddl_fdr_state_pool_htable_t *ht)
{
    for (int i = 0; i < ht->size; ++i)
        borIArrFree(ht->table + i);
    if (ht->table != NULL)
        FREE(ht->table);
}
