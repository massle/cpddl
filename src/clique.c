/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
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

#include <stdio.h>
#include "alloc.h"
#include <boruvka/err.h>
#include "pddl/config.h"
#include "pddl/clique.h"

struct bk_stack_el {
    bor_iset_t clique;
    bor_iset_t P;
    bor_iset_t X;
};
typedef struct bk_stack_el bk_stack_el_t;

struct bk_stack {
    bk_stack_el_t **stack;
    int stack_size;
    int stack_alloc;
};
typedef struct bk_stack bk_stack_t;

static void stackElDel(bk_stack_el_t *e)
{
    borISetFree(&e->clique);
    borISetFree(&e->P);
    borISetFree(&e->X);
    FREE(e);
}

static void stackFree(bk_stack_t *st)
{
    for (int i = 0; i < st->stack_size; ++i)
        stackElDel(st->stack[i]);
    if (st->stack != NULL)
        FREE(st->stack);
}

static bk_stack_el_t *stackPop(bk_stack_t *st)
{
    if (st->stack_size == 0)
        return NULL;
    return st->stack[--st->stack_size];
}

static void stackPush(bk_stack_t *st,
                      const bor_iset_t *clique,
                      const bor_iset_t *P,
                      const bor_iset_t *X,
                      int v,
                      const bor_iset_t *v_N)
{
    if (st->stack_size == st->stack_alloc){
        if (st->stack_alloc == 0)
            st->stack_alloc = 2;
        st->stack_alloc *= 2;
        st->stack = REALLOC_ARR(st->stack, bk_stack_el_t *,
                                    st->stack_alloc);
    }
    bk_stack_el_t *s = ALLOC(bk_stack_el_t);
    borISetInit(&s->clique);
    borISetInit(&s->P);
    borISetInit(&s->X);

    borISetUnion(&s->clique, clique);
    if (v >= 0)
        borISetAdd(&s->clique, v);

    if (v_N != NULL){
        borISetIntersect2(&s->P, P, v_N);
        borISetIntersect2(&s->X, X, v_N);
    }else{
        borISetUnion(&s->P, P);
        borISetUnion(&s->X, X);
    }

    st->stack[st->stack_size++] = s;
}

static int selectPivot(const pddl_graph_simple_t *graph,
                       const bor_iset_t *P,
                       const bor_iset_t *X)
{
    int pivot = -1;
    int pivot_size = -1;
    int fact;
    BOR_ISET_FOR_EACH(P, fact){
        int size = borISetIntersectionSize(P, &graph->node[fact]);
        if (size > pivot_size){
            pivot_size = size;
            pivot = fact;
        }
    }
    BOR_ISET_FOR_EACH(X, fact){
        int size = borISetIntersectionSize(P, &graph->node[fact]);
        if (size > pivot_size){
            pivot_size = size;
            pivot = fact;
        }
    }

    return pivot;
}

static void inferCliques(const pddl_graph_simple_t *graph,
                         bk_stack_t *stack,
                         void (*cb)(const bor_iset_t *clique, void *userdata),
                         void *userdata)
{
    bk_stack_el_t *s;

    while ((s = stackPop(stack)) != NULL){
        int pivot = selectPivot(graph, &s->P, &s->X);

        BOR_ISET(P_next);
        BOR_ISET(X_next);
        borISetUnion(&P_next, &s->P);
        borISetUnion(&X_next, &s->X);

        const bor_iset_t *pivot_N = &graph->node[pivot];
        int size = borISetSize(&s->P);
        int pivot_size = borISetSize(pivot_N);
        for (int i = 0, pi = 0; i < size; ++i){
            int P_v = borISetGet(&s->P, i);
            // skip pivot's neighbors
            for (; pi < pivot_size && borISetGet(pivot_N, pi) < P_v; ++pi);
            if (pi < pivot_size && borISetGet(pivot_N, pi) == P_v){
                ++pi;
                continue;
            }

            const bor_iset_t *P_v_N = &graph->node[P_v];
            if (borISetIntersectionSizeAtLeast(&P_next, P_v_N, 1)){
                stackPush(stack, &s->clique, &P_next, &X_next, P_v, P_v_N);

            }else if (!borISetIntersectionSizeAtLeast(&X_next, P_v_N, 1)){
                // {P,X}_next \cap P_v_N = \emptyset so s->clique \cup {P_v}
                // forms a maximal clique
                BOR_ISET(mg);
                borISetUnion(&mg, &s->clique);
                borISetAdd(&mg, P_v);
                if (borISetSize(&mg) > 1)
                    cb(&mg, userdata);
                borISetFree(&mg);
            }

            borISetRm(&P_next, P_v);
            borISetAdd(&X_next, P_v);
        }

        borISetFree(&P_next);
        borISetFree(&X_next);

        stackElDel(s);
    }
}


// TODO: Add interface to cliquer library
void pddlCliqueFindMaximal(const pddl_graph_simple_t *g,
                           void (*cb)(const bor_iset_t *clique, void *userdata),
                           void *userdata)
{

    BOR_ISET(all_facts);
    BOR_ISET(empty);
    for (int i = 0; i < g->node_size; ++i)
        borISetAdd(&all_facts, i);

    bk_stack_t stack;
    bzero(&stack, sizeof(stack));
    stackPush(&stack, &empty, &all_facts, &empty, -1, NULL);
    inferCliques(g, &stack, cb, userdata);
    stackFree(&stack);
    borISetFree(&all_facts);
}

#ifdef PDDL_CLIQUER
# include <cliquer/cliquer.h>
struct cliquer_data {
    void (*cb)(const bor_iset_t *clique, void *userdata);
    void *userdata;
};

static boolean cliquerCB(set_t clq, graph_t *G, clique_options *opts)
{
    struct cliquer_data *d = opts->user_data;
    BOR_ISET(clique);
    for (int i = -1; (i = set_return_next(clq, i)) >= 0;)
        borISetAdd(&clique, i);
    d->cb(&clique, d->userdata);
    borISetFree(&clique);
    return 1;
}

void pddlCliqueFindMaximalCliquer(const pddl_graph_simple_t *g,
                           void (*cb)(const bor_iset_t *clique, void *userdata),
                           void *userdata)
{
    graph_t *G = graph_new(g->node_size);
    for (int v = 0; v < g->node_size; ++v){
        int w;
        BOR_ISET_FOR_EACH(&g->node[v], w){
            if (v < w)
                GRAPH_ADD_EDGE(G, v, w);
        }
    }

    struct cliquer_data d = { cb, userdata };
    clique_options opts = *clique_default_options;
    opts.user_function = cliquerCB;
    opts.user_data = &d;
    opts.time_function = NULL;
    opts.output = NULL;
    clique_find_all(G, 2, g->node_size, 1, &opts);
    graph_free(G);
}
#else /* PDDL_CLIQUER */
void pddlCliqueFindMaximalCliquer(const pddl_graph_simple_t *g,
                           void (*cb)(const bor_iset_t *clique, void *userdata),
                           void *userdata)
{
    BOR_FATAL2("Cliquer library is not linked!");
}
#endif /* PDDL_CLIQUER */

struct biclique_ud {
    void (*cb)(const bor_iset_t *left,
               const bor_iset_t *right, void *ud);
    void *ud;
    int node_size;
};

static void bicliqueCB(const bor_iset_t *clique, void *ud)
{
    struct biclique_ud *bud = ud;
    BOR_ISET(left);
    BOR_ISET(right);
    int n;
    BOR_ISET_FOR_EACH(clique, n){
        if (n < bud->node_size){
            borISetAdd(&left, n);
        }else{
            borISetAdd(&right, n - bud->node_size);
        }
    }
    if (borISetSize(&left) > 0
            && borISetSize(&right) > 0
            && borISetGet(&left, 0) < borISetGet(&right, 0)){
        bud->cb(&left, &right, bud->ud);
    }
    borISetFree(&left);
    borISetFree(&right);
}

void pddlCliqueFindMaximalBicliques(const pddl_graph_simple_t *g,
                                    void (*cb)(const bor_iset_t *left,
                                               const bor_iset_t *right,
                                               void *ud),
                                    void *ud)
{
    pddl_graph_simple_t graph;
    pddlGraphSimpleInit(&graph, g->node_size * 2);
    for (int i = 0; i < g->node_size; ++i){
        for (int j = i + 1; j < g->node_size; ++j){
            pddlGraphSimpleAddEdge(&graph, i, j);
            pddlGraphSimpleAddEdge(&graph, g->node_size + i, g->node_size + j);
        }
        int n;
        BOR_ISET_FOR_EACH(&g->node[i], n){
            pddlGraphSimpleAddEdge(&graph, i, g->node_size + n);
            pddlGraphSimpleAddEdge(&graph, g->node_size + i, n);
        }
    }

    struct biclique_ud bud = { cb, ud, g->node_size };
    pddlCliqueFindMaximal(&graph, bicliqueCB, (void *)&bud);
    pddlGraphSimpleFree(&graph);
}
