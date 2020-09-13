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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_SCC_H__
#define __PDDL_SCC_H__

#include <boruvka/iset.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Directed graph for SCC algorithm.
 */
struct pddl_scc_graph {
    bor_iset_t *node;
    int node_size;
};
typedef struct pddl_scc_graph pddl_scc_graph_t;

void pddlSCCGraphInit(pddl_scc_graph_t *g, int node_size);
void pddlSCCGraphFree(pddl_scc_graph_t *g);
void pddlSCCGraphAddEdge(pddl_scc_graph_t *g, int from, int to);

/**
 * Strongly connected components
 */
struct pddl_scc {
    bor_iset_t *comp; /*!< List of components */
    int comp_size;    /*!< Number of components */
    int comp_alloc;
};
typedef struct pddl_scc pddl_scc_t;

/**
 * Initializes scc and fills it with strongly connected components.
 * The components are found by Tarjan's algorithm so the resulting
 * components are ordered in a reverse topological order of the DAG of
 * those components.
 */
void pddlSCC(pddl_scc_t *scc, const pddl_scc_graph_t *graph);

/**
 * Free allocated memory.
 */
void pddlSCCFree(pddl_scc_t *scc);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_SCC_H__ */
