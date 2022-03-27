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
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <boruvka/hfunc.h>
#include <boruvka/iarr.h>
#include <pddl/timer.h>
#include "pddl/fdr_state_pool.h"
#include "assert.h"

#define PAGESIZE_MULTIPLY 1024
#define MIN_STATES_PER_BLOCK (1024 * 1024)
#define HTABLE_INIT_SIZE 786433ul
#define HTABLE_RESIZE_FACTOR 2

//#define STATE_ID_ARR_FIXED_ARR_SIZE 2
#define STATE_ID_ARR_FIXED_ARR_SIZE \
    (sizeof(pddl_state_id_t *) / sizeof(pddl_state_id_t))
struct state_id_arr {
    union {
        pddl_state_id_t *arr;
        pddl_state_id_t el[STATE_ID_ARR_FIXED_ARR_SIZE];
    } el_arr;
    uint16_t size;
    uint16_t alloc;
} bor_packed;
typedef struct state_id_arr state_id_arr_t;

static void stateIDArrAdd(state_id_arr_t *arr, pddl_state_id_t id)
{
    if (arr->size < STATE_ID_ARR_FIXED_ARR_SIZE){
        arr->el_arr.el[arr->size++] = id;
        arr->alloc = arr->size;
    }else{
        if (arr->size == STATE_ID_ARR_FIXED_ARR_SIZE){
            pddl_state_id_t tmp[STATE_ID_ARR_FIXED_ARR_SIZE];
            memcpy(tmp, arr->el_arr.el,
                    sizeof(pddl_state_id_t) * STATE_ID_ARR_FIXED_ARR_SIZE);
            arr->alloc = 2 * STATE_ID_ARR_FIXED_ARR_SIZE;
            arr->el_arr.arr = BOR_ALLOC_ARR(pddl_state_id_t, arr->alloc);
            memcpy(arr->el_arr.arr, tmp,
                    sizeof(pddl_state_id_t) * STATE_ID_ARR_FIXED_ARR_SIZE);

        }else if (arr->size == arr->alloc){
            arr->alloc *= 2;
            arr->el_arr.arr = BOR_REALLOC_ARR(arr->el_arr.arr, pddl_state_id_t,
                                              arr->alloc);
        }

        if (arr->alloc <= arr->size){
            BOR_FATAL("There is too much pressure on the hash table"
                      "resulting in too many elements sharing the same"
                      "bucket. (The size of the bucket does not fit in %lu"
                      "bytes.)",
                      (unsigned long)sizeof(arr->alloc));
        }

        arr->el_arr.arr[arr->size++] = id;
    }
}

static pddl_state_id_t stateIDArrGet(const state_id_arr_t *arr, int i)
{
    if (arr->size <= STATE_ID_ARR_FIXED_ARR_SIZE)
        return arr->el_arr.el[i];
    return arr->el_arr.arr[i];
}

static void stateIDArrFree(state_id_arr_t *arr)
{
    if (arr->size > STATE_ID_ARR_FIXED_ARR_SIZE)
        BOR_FREE(arr->el_arr.arr);
}

struct htable {
    state_id_arr_t *table;
    size_t size;
    size_t num_elements;
    size_t bufsize;
    const pddl_fdr_state_pool_t *state_pool;
};
typedef struct htable htable_t;

static void htableInit(htable_t *ht,
                       const pddl_fdr_state_pool_t *state_pool,
                       size_t size)
{
    bzero(ht, sizeof(*ht));
    ht->size = size;
    ht->table = BOR_CALLOC_ARR(state_id_arr_t, ht->size);
    ht->bufsize = pddlFDRStatePackerBufSize(&state_pool->packer);
    ht->state_pool = state_pool;
}

static void htableFree(htable_t *ht)
{
    for (size_t i = 0; i < ht->size; ++i)
        stateIDArrFree(ht->table + i);
    if (ht->table != NULL)
        BOR_FREE(ht->table);
}

static htable_t *htableNew(const pddl_fdr_state_pool_t *state_pool)
{
    htable_t *ht = BOR_ALLOC(htable_t);
    htableInit(ht, state_pool, HTABLE_INIT_SIZE);
    return ht;
}

static void htableDel(htable_t *ht)
{
    htableFree(ht);
    BOR_FREE(ht);
}

_bor_inline size_t nextPrime(size_t hint)
{
    static size_t primes[] = {
        5ul,         53ul,         97ul,         193ul,       389ul,
        769ul,       1543ul,       3079ul,       6151ul,      12289ul,
        24593ul,     49157ul,      98317ul,      196613ul,    393241ul,
        786433ul,    1572869ul,    3145739ul,    6291469ul,   12582917ul,
        25165843ul,  50331653ul,   100663319ul,  201326611ul, 402653189ul,
        805306457ul, 1610612741ul, 3221225473ul, 4294967291ul
    };
    static size_t primes_size = sizeof(primes) / sizeof(size_t);

    for (size_t i = 0; i < primes_size; ++i){
        if (HTABLE_RESIZE_FACTOR * primes[i] >= hint)
            return primes[i];
    }
    return primes[primes_size - 1];
}

static void htableResize(htable_t *ht, size_t size);

static pddl_state_id_t htableInsert(htable_t *ht,
                                    pddl_state_id_t id,
                                    const void *packed_state)
{
    // resize table if necessary
    if (ht->num_elements + 1 > HTABLE_RESIZE_FACTOR * ht->size){
        size_t size = nextPrime(ht->num_elements + 1);
        if (size > ht->size)
            htableResize(ht, size);
    }

    //size_t hash = borFastHash_64(sn->packed_state, ht->bufsize, 7583);
    size_t hash = borCityHash_64(packed_state, ht->bufsize);
    //size_t hash = borFnv1a_64(sn->packed_state, ht->bufsize);
    //size_t hash = borMurmur3_32(sn->packed_state, ht->bufsize);
    size_t bucket_id = hash % ht->size;
    state_id_arr_t *bucket = ht->table + bucket_id;
    for (int i = 0; i < bucket->size; ++i){
        pddl_state_id_t id = stateIDArrGet(bucket, i);
        void *packed_state2 = borExtArrGet(ht->state_pool->pool, id);
        if (memcmp(packed_state, packed_state2, ht->bufsize) == 0)
            return id;
    }
    stateIDArrAdd(bucket, id);
    ++ht->num_elements;
    return id;
}

static void htablePrintStats(const htable_t *ht)
{
    char info[256], *cur;
    int remain = 256;

    int sizes[256];
    bzero(sizes, sizeof(int) * 256);
    int sum = 0;
    for (size_t i = 0; i < ht->size; ++i)
        sizes[ht->table[i].size]++;
    cur = info;
    for (int i = 0; i < 256; ++i){
        sum += sizes[i];
        if (sizes[i] > 0){
            int writ = snprintf(cur, remain, " %d:%.2f/%.2f",
                                i, (double)sizes[i] / ht->size,
                                (double)sum / ht->size);
            if (writ >= remain)
                break;
            remain -= writ;
            cur += writ;
        }
    }
    BOR_INFO(ht->state_pool->err, "State pool: rehashing stats %s", info);
}

static void htableResize(htable_t *ht, size_t size)
{
    BOR_INFO(ht->state_pool->err, "State pool: rehashing size: %lu,"
                                  " new-size: %lu, elements: %lu",
             ht->size, size, ht->num_elements);
    htablePrintStats(ht);

    const pddl_fdr_state_pool_t *state_pool = ht->state_pool;
    htableFree(ht);
    htableInit(ht, state_pool, size);
    for (pddl_state_id_t id = 0; id < ht->state_pool->num_states; ++id){
        const void *packed_state = borExtArrGet(ht->state_pool->pool, id);
        htableInsert(ht, id, packed_state);
    }

    BOR_INFO2(ht->state_pool->err, "State pool: rehashing DONE");
}

void pddlFDRStatePoolInit(pddl_fdr_state_pool_t *state_pool,
                          const pddl_fdr_vars_t *vars,
                          bor_err_t *err)
{
    bzero(state_pool, sizeof(*state_pool));
    state_pool->err = err;
    pddlFDRStatePackerInit(&state_pool->packer, vars);
    state_pool->num_states = 0;

    size_t node_size = pddlFDRStatePackerBufSize(&state_pool->packer);
    state_pool->pool = borExtArrNew2(node_size, PAGESIZE_MULTIPLY,
                                      MIN_STATES_PER_BLOCK,
                                      NULL, NULL);

    state_pool->htable = htableNew(state_pool);
    BOR_INFO(err, "State pool created. bytes per state: %d", (int)node_size);
}

void pddlFDRStatePoolFree(pddl_fdr_state_pool_t *state_pool)
{
    if (state_pool->htable != NULL)
        htableDel(state_pool->htable);
    if (state_pool->pool != NULL)
        borExtArrDel(state_pool->pool);
    pddlFDRStatePackerFree(&state_pool->packer);
}

pddl_state_id_t pddlFDRStatePoolInsert(pddl_fdr_state_pool_t *state_pool,
                                       const int *state)
{
    pddl_state_id_t ins_id = state_pool->num_states;
    void *packed_state = borExtArrGet(state_pool->pool, ins_id);
    pddlFDRStatePackerPack(&state_pool->packer, state, packed_state);
    pddl_state_id_t id;
    if ((id = htableInsert(state_pool->htable, ins_id, packed_state)) == ins_id)
        state_pool->num_states++;
    return id;
}

void pddlFDRStatePoolGet(const pddl_fdr_state_pool_t *state_pool,
                         pddl_state_id_t state_id,
                         int *state)
{
    ASSERT(state_id < state_pool->num_states);
    const void *packed_state = borExtArrGet(state_pool->pool, state_id);
    pddlFDRStatePackerUnpack(&state_pool->packer, packed_state, state);
}
