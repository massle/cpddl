/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
 * Saarland University, FAI Group, and
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

#include "pddl/relaxed_plan.h"
#include "assert.h"

void pddlRelaxedPlanCountConflictsStrips(const bor_iarr_t *plan,
                                         const bor_iset_t *init,
                                         const bor_iset_t *goal,
                                         const pddl_strips_ops_t *ops,
                                         int goal_conflict_weight,
                                         int *fact_conflicts)
{
    BOR_ISET(conflict);
    BOR_ISET(state);
    borISetUnion(&state, init);
    int op_id;
    BOR_IARR_FOR_EACH(plan, op_id){
        const pddl_strips_op_t *op = ops->op[op_id];
        // TODO: Conditional effects not supported yet
        ASSERT_RUNTIME(op->cond_eff_size == 0);
        borISetMinus2(&conflict, &op->pre, &state);
        int fact_id;
        BOR_ISET_FOR_EACH(&conflict, fact_id)
            fact_conflicts[fact_id] += 1;
        borISetMinus(&state, &op->del_eff);
        borISetUnion(&state, &op->add_eff);
    }

    borISetMinus2(&conflict, goal, &state);
    int fact_id;
    BOR_ISET_FOR_EACH(&conflict, fact_id)
        fact_conflicts[fact_id] += 1 * goal_conflict_weight;

    borISetFree(&state);
    borISetFree(&conflict);
}
