/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_ALLOC_INTERNAL_H__
#define __PDDL_ALLOC_INTERNAL_H__

#include <stdlib.h>
#include <string.h>
#include "pddl/alloc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define FREE(ptr) PDDL_FREE(ptr)

#define ALLOC(type) PDDL_ALLOC(type)

#define ALLOC_ARR(type, num_elements) PDDL_ALLOC_ARR(type, (num_elements))

#define REALLOC_ARR(ptr, type, num_elements) \
    PDDL_REALLOC_ARR((ptr), type, (num_elements))

#define CALLOC_ARR(type, num_elements) PDDL_CALLOC_ARR(type, (num_elements))

#define MALLOC(size) PDDL_MALLOC(size)

#define REALLOC(ptr, size) PDDL_REALLOC((ptr), (size))

#define STRDUP(str) PDDL_STRDUP(str)

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_ALLOC_INTERNAL_H__ */
