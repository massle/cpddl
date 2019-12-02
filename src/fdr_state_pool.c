/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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

#include <boruvka/hfunc.h>
#include "pddl/fdr_state_pool.h"
#include "assert.h"

#define PAGESIZE_MULTIPLY 256
#define MIN_STATES_PER_BLOCK 256


struct state_node {
    pddl_state_id_t id; /*!< ID of this state */
    bor_list_t htable; /*!< Connector to the hash table */
    bor_htable_key_t hash; /*!< Pre-computed hash of the packed state */
    char packed_state[]; /*!< Packed state */
} bor_packed;
typedef struct state_node state_node_t;

static bor_htable_key_t packedStateHash(const void *buf, size_t size)
{
    return borFastHash_64(buf, size, 7583);
}

static bor_htable_key_t htableHash(const bor_list_t *key, void *_)
{
    const state_node_t *n = BOR_LIST_ENTRY(key, state_node_t, htable);
    return n->hash;
}

static int htableEq(const bor_list_t *k1, const bor_list_t *k2, void *_ss)
{
    const pddl_fdr_state_pool_t *state_pool = _ss;
    const state_node_t *n1 = BOR_LIST_ENTRY(k1, state_node_t, htable);
    const state_node_t *n2 = BOR_LIST_ENTRY(k2, state_node_t, htable);
    size_t size = pddlFDRStatePackerBufSize(&state_pool->packer);
    return memcmp(n1->packed_state, n2->packed_state, size) == 0;
}

void pddlFDRStatePoolInit(pddl_fdr_state_pool_t *state_pool,
                          const pddl_fdr_vars_t *vars)
{
    bzero(state_pool, sizeof(*state_pool));
    pddlFDRStatePackerInit(&state_pool->packer, vars);
    state_pool->num_states = 0;

    size_t node_size = sizeof(state_node_t);
    node_size += pddlFDRStatePackerBufSize(&state_pool->packer);
    state_pool->pool = borExtArrNew2(node_size, PAGESIZE_MULTIPLY,
                                      MIN_STATES_PER_BLOCK,
                                      NULL, NULL);

    state_pool->htable = borHTableNew(htableHash, htableEq, state_pool);
}

void pddlFDRStatePoolFree(pddl_fdr_state_pool_t *state_pool)
{
    if (state_pool->htable != NULL)
        borHTableDel(state_pool->htable);
    if (state_pool->pool != NULL)
        borExtArrDel(state_pool->pool);
    pddlFDRStatePackerFree(&state_pool->packer);
}

pddl_state_id_t pddlFDRStatePoolInsert(pddl_fdr_state_pool_t *state_pool,
                                       const int *state)
{
    state_node_t *sn = borExtArrGet(state_pool->pool, state_pool->num_states);
    pddlFDRStatePackerPack(&state_pool->packer, state, sn->packed_state);
    sn->hash = packedStateHash(sn->packed_state,
                               pddlFDRStatePackerBufSize(&state_pool->packer));

    bor_list_t *f;
    if ((f = borHTableInsertUnique(state_pool->htable, &sn->htable)) == NULL){
        sn->id = state_pool->num_states++;
        return sn->id;

    }else{
        const state_node_t *sn = BOR_LIST_ENTRY(f, state_node_t, htable);
        return sn->id;
    }
}

void pddlFDRStatePoolGet(const pddl_fdr_state_pool_t *state_pool,
                         pddl_state_id_t state_id,
                         int *state)
{
    ASSERT(state_id < state_pool->num_states);
    const state_node_t *sn = borExtArrGet(state_pool->pool, state_id);
    pddlFDRStatePackerUnpack(&state_pool->packer, sn->packed_state, state);
}
