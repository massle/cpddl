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

#ifndef __PDDL_COST_H__
#define __PDDL_COST_H__

#include <boruvka/compiler.h>
#include <pddl/common.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_cost {
    int cost;
    int zero_cost;
};
typedef struct pddl_cost pddl_cost_t;

extern pddl_cost_t pddl_cost_zero;
extern pddl_cost_t pddl_cost_max;

_bor_inline void pddlCostSetZero(pddl_cost_t *c1)
{
    c1->cost = 0;
    c1->zero_cost = 0;
}

_bor_inline void pddlCostSetMax(pddl_cost_t *c1)
{
    c1->cost = PDDL_COST_MAX;
    c1->zero_cost = PDDL_COST_MAX;
}

_bor_inline void pddlCostSetOp(pddl_cost_t *c1, int op_cost)
{
    if (op_cost == 0){
        c1->zero_cost = 1;
    }else{
        c1->cost = op_cost;
    }
}

/**
 * c1 += c2
 */
_bor_inline void pddlCostSum(pddl_cost_t *c1, const pddl_cost_t *c2)
{
    c1->cost += c2->cost;
    c1->zero_cost += c2->zero_cost;
}


/**
 * Compare c1 and c2
 */
_bor_inline int pddlCostCmp(const pddl_cost_t *c1, const pddl_cost_t *c2)
{
    int cmp = c1->cost - c2->cost;
    if (cmp == 0)
        cmp = c1->zero_cost - c2->zero_cost;
    return cmp;
}

/**
 * Compare c1 + c2 and cs
 */
_bor_inline int pddlCostCmpSum(const pddl_cost_t *c1,
                               const pddl_cost_t *c2,
                               const pddl_cost_t *cs)
{
    int cmp = (c1->cost + c2->cost) - cs->cost;
    if (cmp == 0)
        cmp = (c1->zero_cost + c2->zero_cost) - cs->zero_cost;
    return cmp;
}


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_COST_H__ */
