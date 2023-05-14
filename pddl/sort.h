/***
 * Copyright (c)2011 Daniel Fiser <danfis@danfis.cz>
 *
 *  This file is part of cpddl.
 *
 *  Distributed under the OSI-approved BSD License (the "License");
 *  see accompanying file BDS-LICENSE for details or see
 *  <http://www.opensource.org/licenses/bsd-license.php>.
 *
 *  This software is distributed WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the License for more information.
 */

#ifndef __PDDL_SORT_H__
#define __PDDL_SORT_H__

#include <pddl/list.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Sort Algorithms
 * ================
 */

/**
 * Compare function for sort functions.
 */
typedef int (*pddl_sort_cmp)(const void *, const void *, void *arg);

/**
 * BSD kqsort.
 */
int pddlQSort(void *base, size_t nmemb, size_t size,
              pddl_sort_cmp cmp, void *carg);

/**
 * Tim sort.
 * Stable sort.
 */
int pddlTimSort(void *base, size_t nmemb, size_t size,
                pddl_sort_cmp cmp, void *carg);


/**
 * Default sorting algorithm.
 * Not guaranteed to be stable.
 */
int pddlSort(void *base, size_t nmemb, size_t size,
             pddl_sort_cmp cmp, void *carg);

/**
 * Default stable sort.
 */
int pddlStableSort(void *base, size_t nmemb, size_t size,
                   pddl_sort_cmp cmp, void *carg);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SORT_H__ */
