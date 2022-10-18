/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL_VERSION_H__
#define __PDDL_VERSION_H__

#define PDDL_VERSION_MAJOR 0
#define PDDL_VERSION_MINOR 0
#define PDDL_VERSION_PATCH 0


#define _PDDL_VERSION_TO_STR1(x) #x
#define _PDDL_VERSION_TO_STR(x) _PDDL_VERSION_TO_STR1(x)

#define PDDL_VERSION_NUM \
    (10000 * PDDL_VERSION_MAJOR) + (100 * PDDL_VERSION_MINOR) + PDDL_VERSION_PATCH
#define PDDL_VERSION_STR \
    _PDDL_VERSION_TO_STR(PDDL_VERSION_MAJOR.PDDL_VERSION_MINOR.PDDL_VERSION_PATCH)

#endif /* __PDDL_VERSION_H__ */
