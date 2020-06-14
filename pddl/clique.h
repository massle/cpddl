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

#ifndef __PDDL_CLIQUE_H__
#define __PDDL_CLIQUE_H__

#include <boruvka/iset.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_clique_graph {
    bor_iset_t *node;
    int node_size;
};
typedef struct pddl_clique_graph pddl_clique_graph_t;

void pddlCliqueGraphInit(pddl_clique_graph_t *g, int node_size);
void pddlCliqueGraphFree(pddl_clique_graph_t *g);
void pddlCliqueGraphAddEdge(pddl_clique_graph_t *g, int n1, int n2);

void pddlCliqueFindMaximal(const pddl_clique_graph_t *g,
                           void (*cb)(const bor_iset_t *clique, void *userdata),
                           void *userdata);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_CLIQUE_H__ */
