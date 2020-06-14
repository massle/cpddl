/***
 * cpddl
 * -------
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PLAN_TIME_LIMIT_H__
#define __PLAN_TIME_LIMIT_H__

#include <boruvka/timer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_time_limit {
    bor_timer_t timer;
    bor_real_t limit;
};
typedef struct pddl_time_limit pddl_time_limit_t;

/**
 * Initializes time limit as "unlimited".
 */
_bor_inline void pddlTimeLimitInit(pddl_time_limit_t *tm)
{
    borTimerStart(&tm->timer);
    tm->limit = 1E100;
}

/**
 * Sets the time limit in seconds and starts counting.
 */
_bor_inline void pddlTimeLimitSet(pddl_time_limit_t *tm, bor_real_t limit)
{
    borTimerStart(&tm->timer);
    if (limit > 0.){
        tm->limit = limit;
    }else{
        tm->limit = 1E100;
    }
}

/**
 * Checks the time limit.
 * Returns 0 if we are still withing time limit, -1 otherwise.
 */
_bor_inline int pddlTimeLimitCheck(pddl_time_limit_t *tm)
{
    if (tm->limit >= 1E100)
        return 0;

    borTimerStop(&tm->timer);
    if (borTimerElapsedInSF(&tm->timer) > tm->limit)
        return -1;
    return 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PLAN_TIME_LIMIT_H__ */
