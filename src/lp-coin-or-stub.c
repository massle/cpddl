/***
 * Copyright (c)2023 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/lp.h"
#include "pddl/libs_info.h"
#include "_lp.h"

#ifdef PDDL_COIN_OR
# error "We have Coin-Or, so this source shouldn't be included!"
#endif

const char * const pddl_coin_or_version = NULL;

pddl_lp_status_t pddlLPSolveCoinOr(const pddl_lp_t *lp,
                                   pddl_lp_solution_t *sol,
                                   pddl_err_t *err)
{
    PANIC("Missing Coin-Or solver");
    return PDDL_LP_STATUS_ERR;
}
