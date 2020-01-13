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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <boruvka/alloc.h>
#include "pddl/clique.h"

void pddlCliqueGraphInit(pddl_clique_graph_t *g, int node_size)
{
    bzero(g, sizeof(*g));
    g->node_size = node_size;
    g->node = BOR_CALLOC_ARR(bor_iset_t, g->node_size);
}

void pddlCliqueGraphFree(pddl_clique_graph_t *g)
{
    for (int i = 0; i < g->node_size; ++i)
        borISetFree(g->node + i);
    if (g->node != NULL)
        BOR_FREE(g->node);
}

void pddlCliqueGraphAddEdge(pddl_clique_graph_t *g, int n1, int n2)
{
    borISetAdd(&g->node[n1], n2);
    borISetAdd(&g->node[n2], n1);
}



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
    BOR_FREE(e);
}

static void stackFree(bk_stack_t *st)
{
    for (int i = 0; i < st->stack_size; ++i)
        stackElDel(st->stack[i]);
    if (st->stack != NULL)
        BOR_FREE(st->stack);
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
        st->stack = BOR_REALLOC_ARR(st->stack, bk_stack_el_t *,
                                    st->stack_alloc);
    }
    bk_stack_el_t *s = BOR_ALLOC(bk_stack_el_t);
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

static int selectPivot(const pddl_clique_graph_t *graph,
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

static void inferCliques(const pddl_clique_graph_t *graph,
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


void pddlCliqueFindMaximal(const pddl_clique_graph_t *g,
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
