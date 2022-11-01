/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL_ASNETS_H__
#define __PDDL_ASNETS_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int pddlASNetsTrain(const char *domain_fn,
                    const char **problem_fn,
                    int problem_fn_size,
                    pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_ASNETS_H__ */
