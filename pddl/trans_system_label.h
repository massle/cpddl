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

#ifndef __PDDL_TRANS_SYSTEM_LABEL_H__
#define __PDDL_TRANS_SYSTEM_LABEL_H__

#include <boruvka/iset.h>
#include <boruvka/htable.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_trans_system_label_set {
    bor_iset_t label; /*!< Set of labels */
    int ref; /*!< Reference counter */
    bor_htable_key_t key; /*!< Key to hashtable */
    bor_list_t htable; /*!< Connector to the hashtable */
};
typedef struct pddl_trans_system_label_set pddl_trans_system_label_set_t;

struct pddl_trans_system_labels {
    bor_htable_t *label_set;
};
typedef struct pddl_trans_system_labels pddl_trans_system_labels_t;

/**
 * Initialize empty set of sets of labels.
 */
void pddlTransSystemLabelsInit(pddl_trans_system_labels_t *lbs);

/**
 * Free allocated memory.
 */
void pddlTransSystemLabelsFree(pddl_trans_system_labels_t *lbs);

/**
 * Adds a set of labels if not already there and returns reference to the
 * added set.
 */
pddl_trans_system_label_set_t *
    pddlTransSystemLabelsAdd(pddl_trans_system_labels_t *lbs,
                             const bor_iset_t *labels);

/**
 * Dereference the given set of labels.
 */
void pddlTransSystemLabelsDecRef(pddl_trans_system_labels_t *lbs,
                                 pddl_trans_system_label_set_t *set);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_TRANS_SYSTEM_LABEL_H__ */
