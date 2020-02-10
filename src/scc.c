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

#include <boruvka/alloc.h>
#include "pddl/scc.h"

void pddlSCCGraphInit(pddl_scc_graph_t *g, int node_size)
{
    bzero(g, sizeof(*g));
    g->node_size = node_size;
    g->node = BOR_CALLOC_ARR(bor_iset_t, g->node_size);
}

void pddlSCCGraphFree(pddl_scc_graph_t *g)
{
    for (int i = 0; i < g->node_size; ++i)
        borISetFree(g->node + i);
    if (g->node != NULL)
        BOR_FREE(g->node);
}

void pddlSCCGraphAddEdge(pddl_scc_graph_t *g, int from, int to)
{
    borISetAdd(&g->node[from], to);
}

/** Context for DFS during computing SCC */
struct scc_dfs {
    const pddl_scc_graph_t *graph;
    int cur_index;
    int *index;
    int *lowlink;
    int *in_stack;
    int *stack;
    int stack_size;
};
typedef struct scc_dfs scc_dfs_t;

static void sccTarjanStrongconnect(pddl_scc_t *scc, scc_dfs_t *dfs, int vert)
{
    dfs->index[vert] = dfs->lowlink[vert] = dfs->cur_index++;
    dfs->stack[dfs->stack_size++] = vert;
    dfs->in_stack[vert] = 1;

    int end_vert;
    BOR_ISET_FOR_EACH(&dfs->graph->node[vert], end_vert){
        if (dfs->index[end_vert] == -1){
            sccTarjanStrongconnect(scc, dfs, end_vert);
            dfs->lowlink[vert] = BOR_MIN(dfs->lowlink[vert],
                                         dfs->lowlink[end_vert]);
        }else if (dfs->in_stack[end_vert]){
            dfs->lowlink[vert] = BOR_MIN(dfs->lowlink[vert],
                                         dfs->lowlink[end_vert]);
        }
    }

    if (dfs->index[vert] == dfs->lowlink[vert]){
        // Create a new component
        if (scc->comp_size == scc->comp_alloc){
            if (scc->comp_alloc == 0)
                scc->comp_alloc = 2;
            scc->comp_alloc *= 2;
            scc->comp = BOR_REALLOC_ARR(scc->comp, bor_iset_t, scc->comp_alloc);
        }
        bor_iset_t *comp = scc->comp + scc->comp_size++;
        borISetInit(comp);

        // Unroll stack
        int i;
        for (i = dfs->stack_size - 1; dfs->stack[i] != vert; --i){
            dfs->in_stack[dfs->stack[i]] = 0;
            borISetAdd(comp, dfs->stack[i]);
        }
        dfs->in_stack[dfs->stack[i]] = 0;
        borISetAdd(comp, dfs->stack[i]);

        // Shrink stack
        dfs->stack_size = i;
    }
}

static void sccTarjan(pddl_scc_t *scc, const pddl_scc_graph_t *graph)
{
    scc_dfs_t dfs;

    // Initialize structure for Tarjan's algorithm
    dfs.graph = graph;
    dfs.cur_index = 0;
    dfs.index    = BOR_ALLOC_ARR(int, 4 * graph->node_size);
    dfs.lowlink  = dfs.index + graph->node_size;
    dfs.in_stack = dfs.lowlink + graph->node_size;
    dfs.stack    = dfs.in_stack + graph->node_size;
    dfs.stack_size = 0;
    for (int i = 0; i < graph->node_size; ++i){
        dfs.index[i] = dfs.lowlink[i] = -1;
        dfs.in_stack[i] = 0;
    }

    for (int node = 0; node < graph->node_size; ++node){
        if (dfs.index[node] == -1)
            sccTarjanStrongconnect(scc, &dfs, node);
    }

    BOR_FREE(dfs.index);
}

void pddlSCC(pddl_scc_t *scc, const pddl_scc_graph_t *graph)
{
    bzero(scc, sizeof(*scc));
    sccTarjan(scc, graph);
}

void pddlSCCFree(pddl_scc_t *scc)
{
    for (int i = 0; i < scc->comp_size; ++i)
        borISetFree(scc->comp + i);
    if (scc->comp != NULL)
        BOR_FREE(scc->comp);
}
