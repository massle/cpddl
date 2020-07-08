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

#ifndef __PDDL_LABEL_H__
#define __PDDL_LABEL_H__

#include <pddl/strips_op.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_label {
    int cost;
    int op_id;
};
typedef struct pddl_label pddl_label_t;

struct pddl_labels {
    pddl_label_t *label;
    int label_size;
    int label_alloc;
};
typedef struct pddl_labels pddl_labels_t;

void pddlLabelsInitFromStripsOps(pddl_labels_t *lbs,
                                 const pddl_strips_ops_t *ops);
void pddlLabelsFree(pddl_labels_t *lbs);

struct pddl_label_set {
    bor_iset_t label; /*!< Set of labels */
    // TODO: Cost -- minimal cost among labels
    int ref; /*!< Reference counter */
    bor_htable_key_t key; /*!< Key to hashtable */
    bor_list_t htable; /*!< Connector to the hashtable */
};
typedef struct pddl_label_set pddl_label_set_t;

struct pddl_label_sets {
    bor_htable_t *label_set;
};
typedef struct pddl_label_sets pddl_label_sets_t;

/**
 * Initialize empty set of sets of labels.
 */
void pddlLabelSetsInit(pddl_label_sets_t *lbs);

/**
 * Free allocated memory.
 */
void pddlLabelSetsFree(pddl_label_sets_t *lbs);

/**
 * Adds a set of labels if not already there and returns reference to the
 * added set.
 */
pddl_label_set_t *pddlLabelSetsAdd(pddl_label_sets_t *lbs,
                                   const bor_iset_t *labels);

/**
 * Dereference the given set of labels.
 */
void pddlLabelSetsDecRef(pddl_label_sets_t *lbs, pddl_label_set_t *set);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_LABEL_H__ */
