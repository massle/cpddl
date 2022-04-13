/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_FDR_STATE_POOL_HTABLE_H__
#define __PDDL_FDR_STATE_POOL_HTABLE_H__

#include <pddl/iarr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_fdr_state_pool;

typedef size_t pddl_fdr_state_pool_htable_key_t;
struct pddl_fdr_state_pool_htable {
    pddl_iarr_t *table;
    size_t size;
    size_t num_elements;
    const struct pddl_fdr_state_pool *state_pool;
};
typedef struct pddl_fdr_state_pool_htable pddl_fdr_state_pool_htable_t;

void pddlFDRStatePoolHTableInit(pddl_fdr_state_pool_htable_t *ht,
                                const struct pddl_fdr_state_pool *state_pool);
void pddlFDRStatePoolHTableFree(pddl_fdr_state_pool_htable_t *ht);

/**
 * Insert an element into the hash table only if the same element isn't
 * already there.
 * Returns the equal element if already on hash table or NULL the given
 * element was inserted.
 */
_pddl_inline pddl_state_id_t pddlFDRStatePoolHTableInsertUnique(
                                    pddl_fdr_state_pool_htable_t *ht,
                                    pddl_state_id_t state_id)
{
    size_t bucket;
    pddl_list_t *item;

    bucket = pddlHTableBucket(m, key1);
    item = pddlHTableFindBucket(m, bucket, key1);
    if (item == NULL){
        pddlHTableInsertBucket(m, bucket, key1);
    }

    return item;
}

_pddl_inline pddl_list_t *pddlHTableFindBucket(const pddl_htable_t *m,
                                               size_t bucket,
                                               const pddl_list_t *key1)
{
    pddl_list_t *item;

    PDDL_LIST_FOR_EACH(&m->table[bucket], item){
        if (m->eq(key1, item, m->data))
            return item;
    }

    return NULL;
}

_pddl_inline void pddlHTableInsertBucket(pddl_htable_t *m, size_t bucket,
                                         pddl_list_t *key1)
{
    size_t size;

    // resize table if necessary
    if (m->num_elements + 1 > m->size){
        size = pddlHTableNextPrime(m->num_elements + 1);
        if (size > m->size){
            pddlHTableResize(m, size);

            // re-compute bucket id because of resize
            bucket = pddlHTableBucket(m, key1);
        }
    }

    // put item into table
    pddlHTableInsertBucketNoResize(m, bucket, key1);
}

_pddl_inline void pddlHTableInsertBucketNoResize(pddl_htable_t *m,
                                                 size_t bucket,
                                                 pddl_list_t *key1)
{
    pddlListAppend(&m->table[bucket], key1);
    ++m->num_elements;
}

_pddl_inline size_t pddlHTableBucket(const pddl_htable_t *m,
                                     const pddl_list_t *key1)
{
    return m->hash(key1, m->data) % (pddl_htable_key_t)m->size;
}
_pddl_inline size_t _pddlFDRStatePoolHTableNextPrime(size_t hint)
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
        if (primes[i] >= hint)
            return primes[i];
    }
    return primes[primes_size - 1];
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_FDR_STATE_POOL_HTABLE_H__ */
