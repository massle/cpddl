/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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
