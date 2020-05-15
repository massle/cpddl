/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
 * Agent Technology Center, Department of Computer Science,
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

#include <boruvka/pairheap.h>
#include "pddl/cg.h"
#include "assert.h"

#define GOAL_BONUS 100000

/** Strongly connected components */
struct scc {
    bor_iset_t *comp; /*!< List of components */
    int comp_size; /*!< Number of components */
    int comp_alloc;
};
typedef struct scc scc_t;

/** Context for DFS during computing SCC */
struct scc_dfs {
    int cur_index;
    int *index;
    int *lowlink;
    int *in_stack;
    int *stack;
    int stack_size;
};
typedef struct scc_dfs scc_dfs_t;

static void sccTarjanStrongconnect(scc_t *scc,
                                   scc_dfs_t *dfs,
                                   const pddl_cg_t *cg,
                                   int node)
{
    dfs->index[node] = dfs->lowlink[node] = dfs->cur_index++;
    dfs->stack[dfs->stack_size++] = node;
    dfs->in_stack[node] = 1;

    int len = cg->node[node].fw_size;
    const pddl_cg_edge_t *e = cg->node[node].fw;
    int i;
    for (i = 0; i < len; ++i){
        int w = e[i].end;
        if (dfs->index[w] == -1){
            sccTarjanStrongconnect(scc, dfs, cg, w);
            dfs->lowlink[node] = BOR_MIN(dfs->lowlink[node], dfs->lowlink[w]);
        }else if (dfs->in_stack[w]){
            dfs->lowlink[node] = BOR_MIN(dfs->lowlink[node], dfs->lowlink[w]);
        }
    }

    if (dfs->index[node] == dfs->lowlink[node]){
        // Find how deep unroll stack
        for (i = dfs->stack_size - 1; dfs->stack[i] != node; --i)
            dfs->in_stack[dfs->stack[i]] = 0;
        dfs->in_stack[dfs->stack[i]] = 0;

        // Create new component if necessary
        if (scc->comp_size == scc->comp_alloc){
            if (scc->comp_alloc == 0)
                scc->comp_alloc = 2;
            scc->comp_alloc *= 2;
            scc->comp = BOR_REALLOC_ARR(scc->comp, bor_iset_t, scc->comp_alloc);
        }
        bor_iset_t *comp = scc->comp + scc->comp_size++;

        // Copy node IDs from the stack to the component
        borISetInit(comp);
        for (int j = i; j < dfs->stack_size; ++j)
            borISetAdd(comp, dfs->stack[j]);

        // Shrink stack
        dfs->stack_size = i;
    }
}

static void sccTarjan(scc_t *scc, const pddl_cg_t *cg)
{
    scc_dfs_t dfs;

    // Initialize structure for Tarjan's algorithm
    dfs.cur_index = 0;
    dfs.index    = BOR_ALLOC_ARR(int, 4 * cg->node_size);
    dfs.lowlink  = dfs.index + cg->node_size;
    dfs.in_stack = dfs.lowlink + cg->node_size;
    dfs.stack    = dfs.in_stack + cg->node_size;
    dfs.stack_size = 0;
    for (int i = 0; i < cg->node_size; ++i){
        dfs.index[i] = dfs.lowlink[i] = -1;
        dfs.in_stack[i] = 0;
    }

    for (int node = 0; node < cg->node_size; ++node){
        if (dfs.index[node] == -1)
            sccTarjanStrongconnect(scc, &dfs, cg, node);
    }

    BOR_FREE(dfs.index);
}

static void sccInit(scc_t *scc, const pddl_cg_t *cg)
{
    bzero(scc, sizeof(*scc));

    // Run Tarjan's algorithm for finding strongly connected components.
    sccTarjan(scc, cg);
}

static void sccFree(scc_t *scc)
{
    for (int i = 0; i < scc->comp_size; ++i)
        borISetFree(scc->comp + i);
    if (scc->comp != NULL)
        BOR_FREE(scc->comp);
}

static void collectEdges(const bor_iset_t *pre,
                         const bor_iset_t *eff,
                         const pddl_fdr_vars_t *vars,
                         int eff_eff_edges,
                         int *value)
{
    int eff_var;
    BOR_ISET_FOR_EACH(eff, eff_var){
        int pre_var;
        BOR_ISET_FOR_EACH(pre, pre_var){
            if (pre_var == eff_var)
                continue;
            value[pre_var * vars->var_size + eff_var] += 1;
        }
        if (eff_eff_edges){
            int eff_var2;
            BOR_ISET_FOR_EACH(eff, eff_var2){
                if (eff_var2 == eff_var)
                    continue;
                value[eff_var2 * vars->var_size + eff_var] += 1;
                value[eff_var * vars->var_size + eff_var2] += 1;
            }
        }
    }
}

void pddlCGInit(pddl_cg_t *cg,
                const pddl_fdr_vars_t *vars,
                const pddl_fdr_ops_t *ops,
                int eff_eff_edges)
{
    bzero(cg, sizeof(*cg));

    int *value = BOR_CALLOC_ARR(int, vars->var_size * vars->var_size);
    BOR_ISET(pre);
    BOR_ISET(eff);
    for (int oi = 0; oi < ops->op_size; ++oi){
        const pddl_fdr_op_t *op = ops->op[oi];

        borISetEmpty(&pre);
        borISetEmpty(&eff);

        for (int prei = 0; prei < op->pre.fact_size; ++prei)
            borISetAdd(&pre, op->pre.fact[prei].var);
        for (int effi = 0; effi < op->eff.fact_size; ++effi)
            borISetAdd(&eff, op->eff.fact[effi].var);
        for (int cei = 0; cei < op->cond_eff_size; ++cei){
            const pddl_fdr_op_cond_eff_t *ce = op->cond_eff + cei;
            for (int prei = 0; prei < ce->pre.fact_size; ++prei)
                borISetAdd(&pre, ce->pre.fact[prei].var);
            for (int effi = 0; effi < ce->eff.fact_size; ++effi)
                borISetAdd(&eff, ce->eff.fact[effi].var);
        }
        collectEdges(&pre, &eff, vars, eff_eff_edges, value);
    }
    borISetFree(&pre);
    borISetFree(&eff);

    cg->node_size = vars->var_size;
    cg->node = BOR_CALLOC_ARR(pddl_cg_node_t, cg->node_size);
    for (int v1 = 0; v1 < vars->var_size; ++v1){
        pddl_cg_node_t *n1 = cg->node + v1;
        for (int v2 = 0; v2 < vars->var_size; ++v2){
            n1->fw_size += (value[v1 * vars->var_size + v2] > 0 ? 1 : 0);
            n1->bw_size += (value[v2 * vars->var_size + v1] > 0 ? 1 : 0);
        }

        int ins_fw = 0;
        int ins_bw = 0;
        n1->fw = BOR_CALLOC_ARR(pddl_cg_edge_t, n1->fw_size);
        n1->bw = BOR_CALLOC_ARR(pddl_cg_edge_t, n1->bw_size);
        for (int v2 = 0; v2 < vars->var_size; ++v2){
            if (value[v1 * vars->var_size + v2] > 0){
                n1->fw[ins_fw].value = value[v1 * vars->var_size + v2];
                n1->fw[ins_fw++].end = v2;
            }
            if (value[v2 * vars->var_size + v1] > 0){
                n1->bw[ins_bw].value = value[v2 * vars->var_size + v1];
                n1->bw[ins_bw++].end = v2;
            }
        }
    }

    BOR_FREE(value);
}

void pddlCGInitCopy(pddl_cg_t *cg, const pddl_cg_t *cg_in)
{
    bzero(cg, sizeof(*cg));
    cg->node_size = cg_in->node_size;
    cg->node = BOR_CALLOC_ARR(pddl_cg_node_t, cg->node_size);
    for (int node_id = 0; node_id < cg->node_size; ++node_id){
        pddl_cg_node_t *n = cg->node + node_id;
        const pddl_cg_node_t *m = cg_in->node + node_id;
        n->fw_size = m->fw_size;
        n->fw = BOR_ALLOC_ARR(pddl_cg_edge_t, n->fw_size);
        memcpy(n->fw, m->fw, sizeof(pddl_cg_edge_t) * n->fw_size);
        n->bw_size = m->bw_size;
        n->bw = BOR_ALLOC_ARR(pddl_cg_edge_t, n->bw_size);
        memcpy(n->bw, m->bw, sizeof(pddl_cg_edge_t) * n->bw_size);
    }
}

void pddlCGFree(pddl_cg_t *cg)
{
    for (int n = 0; n < cg->node_size; ++n){
        pddl_cg_node_t *node = cg->node + n;
        if (node->fw != NULL)
            BOR_FREE(node->fw);
        if (node->bw != NULL)
            BOR_FREE(node->bw);
    }
    if (cg->node != NULL)
        BOR_FREE(cg->node);
}

static void markBackwardReachableVarsDFS(const pddl_cg_t *cg,
                                         int var,
                                         int *important)
{
    for (int i = 0; i < cg->node[var].bw_size; ++i){
        int w = cg->node[var].bw[i].end;
        if (!important[w]){
            important[w] = 1;
            markBackwardReachableVarsDFS(cg, w, important);
        }
    }
}

void pddlCGMarkBackwardReachableVars(const pddl_cg_t *cg,
                                     const pddl_fdr_part_state_t *goal,
                                     int *important_vars)
{
    bzero(important_vars, sizeof(int) * cg->node_size);

    for (int fi = 0; fi < goal->fact_size; ++fi){
        int var = goal->fact[fi].var;
        if (!important_vars[var]){
            important_vars[var] = 1;
            markBackwardReachableVarsDFS(cg, var, important_vars);
        }
    }
}

struct order_var {
    bor_pairheap_node_t heap;
    int var; /*!< ID of the variable */
    int w; /*!< Incoming weight */
    int ordered; /*!< True if the variable was already ordered */
    int scc_id; /*!< ID of the strongly connected component */
    int is_goal; /*!< True if the variable is goal variable */
};
typedef struct order_var order_var_t;

static int heapLT(const bor_pairheap_node_t *a,
                  const bor_pairheap_node_t *b, void *_)
{
    order_var_t *v1 = bor_container_of(a, order_var_t, heap);
    order_var_t *v2 = bor_container_of(b, order_var_t, heap);
    if (v1->scc_id == v2->scc_id){
        if (v1->w == v2->w)
            return v1->var < v2->var;
        return v1->w < v2->w;
    }
    return v1->scc_id < v2->scc_id;
}

static void orderVarInit(order_var_t *order_var,
                         const pddl_cg_t *cg,
                         const pddl_fdr_part_state_t *goal)
{
    for (int var_id = 0; var_id < cg->node_size; ++var_id){
        order_var_t *v = order_var + var_id;
        v->var = var_id;
        v->w = 0;
        v->ordered = 0;
        v->scc_id = -1;
        v->is_goal = 0;
    }

    if (goal != NULL){
        for (int i = 0; i < goal->fact_size; ++i)
            order_var[goal->fact[i].var].is_goal = 1;
    }

    // Compute strongly connected components and mark variables with IDs of
    // the found components
    scc_t scc;
    sccInit(&scc, cg);
    for (int ci = 0; ci < scc.comp_size; ++ci){
        int var_id;
        BOR_ISET_FOR_EACH(scc.comp + ci, var_id)
            order_var[var_id].scc_id = ci;
    }
    sccFree(&scc);

    // Set weights of variables by summing costs of incoming edges within
    // each components
    for (int var_id = 0; var_id < cg->node_size; ++var_id){
        const pddl_cg_node_t *n = cg->node + var_id;
        for (int ei = 0; ei < n->fw_size; ++ei){
            const pddl_cg_edge_t *e = n->fw + ei;
            if (order_var[var_id].scc_id == order_var[e->end].scc_id){
                order_var[e->end].w += e->value;
                if (order_var[e->end].is_goal){
                    order_var[e->end].w += GOAL_BONUS;
                }
            }
        }
    }
}

static void removeVar(order_var_t *order_var,
                      int var_id,
                      bor_pairheap_t *heap,
                      const pddl_cg_t *cg)
{
    const pddl_cg_node_t *n = cg->node + var_id;
    for (int ei = 0; ei < n->fw_size; ++ei){
        const pddl_cg_edge_t *e = n->fw + ei;
        if (order_var[var_id].scc_id == order_var[e->end].scc_id
                && !order_var[e->end].ordered){
            order_var[e->end].w -= e->value;
            //order_var[e->end].w -= order_var[var_id].w;
            borPairHeapDecreaseKey(heap, &order_var[e->end].heap);
        }
    }
}

/*
static void reverseArr(int *arr, int size)
{
    int len = size / 2;
    for (int i = 0; i < len; ++i){
        int tmp;
        BOR_SWAP(arr[i], arr[size - 1 - i], tmp);
    }
}
*/

static void moveUnimportantVarsBack(const pddl_cg_t *cg,
                                    const pddl_fdr_part_state_t *goal,
                                    int *var_ordering)
{
    int *old_order = BOR_ALLOC_ARR(int, cg->node_size);
    memcpy(old_order, var_ordering, sizeof(int) * cg->node_size);

    int *important = BOR_CALLOC_ARR(int, cg->node_size);
    if (goal == NULL){
        for (int v = 0; v < cg->node_size; ++v)
            important[v] = 1;
    }else{
        pddlCGMarkBackwardReachableVars(cg, goal, important);
    }

    int ins = 0;
    for (int i = 0; i < cg->node_size; ++i){
        if (important[old_order[i]])
            var_ordering[ins++] = old_order[i];
    }
    for (int v = 0; v < cg->node_size; ++v){
        if (!important[v])
            var_ordering[ins++] = v;
    }
    BOR_FREE(important);
    BOR_FREE(old_order);
}

void pddlCGVarOrdering(const pddl_cg_t *cg,
                       const pddl_fdr_part_state_t *goal,
                       int *var_ordering)
{
    if (cg->node_size == 1){
        var_ordering[0] = 0;
        return;
    }

    order_var_t *order_var = BOR_ALLOC_ARR(order_var_t, cg->node_size);
    orderVarInit(order_var, cg, goal);

    bor_pairheap_t *heap = borPairHeapNew(heapLT, NULL);
    for (int var_id = 0; var_id < cg->node_size; ++var_id)
        borPairHeapAdd(heap, &order_var[var_id].heap);

    int ins = 0;
    for (; !borPairHeapEmpty(heap); ++ins){
        bor_pairheap_node_t *hnode = borPairHeapExtractMin(heap);
        order_var_t *minvar = bor_container_of(hnode, order_var_t, heap);
        minvar->ordered = 1;
        var_ordering[ins] = minvar->var;
        removeVar(order_var, minvar->var, heap, cg);
    }
    ASSERT_RUNTIME(ins == cg->node_size);
    //reverseArr(var_ordering, cg->node_size);
    moveUnimportantVarsBack(cg, goal, var_ordering);

    borPairHeapDel(heap);
    BOR_FREE(order_var);
}
