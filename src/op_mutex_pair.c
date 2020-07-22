/***
 * cpddl
 * -------
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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

#include <boruvka/alloc.h>
#include "pddl/op_mutex_pair.h"

void pddlOpMutexPairsInit(pddl_op_mutex_pairs_t *m, const pddl_strips_t *s)
{
    bzero(m, sizeof(*m));

    m->op_size = s->op.op_size;
    m->op_id_to_id = BOR_ALLOC_ARR(int, m->op_size);
    for (int i = 0; i < m->op_size; ++i)
        m->op_id_to_id[i] = -1;
}

void pddlOpMutexPairsInitCopy(pddl_op_mutex_pairs_t *dst,
                              const pddl_op_mutex_pairs_t *src)
{
    bzero(dst, sizeof(*dst));

    dst->op_size = src->op_size;
    dst->op_id_to_id = BOR_ALLOC_ARR(int, dst->op_size);
    memcpy(dst->op_id_to_id, src->op_id_to_id, sizeof(int) * dst->op_size);
    dst->id_to_op_id = BOR_ALLOC_ARR(int, src->alloc);
    memcpy(dst->id_to_op_id, src->id_to_op_id, sizeof(int) * src->alloc);
    dst->size = src->size;
    dst->alloc = src->alloc;
    dst->op_mutex = BOR_CALLOC_ARR(bor_iset_t, dst->alloc);
    for (int i = 0; i < src->size; ++i)
        borISetUnion(dst->op_mutex + i, src->op_mutex + i);
    dst->num_op_mutex_pairs = src->num_op_mutex_pairs;
}

void pddlOpMutexPairsFree(pddl_op_mutex_pairs_t *m)
{
    if (m->op_id_to_id != NULL)
        BOR_FREE(m->op_id_to_id);
    if (m->id_to_op_id != NULL)
        BOR_FREE(m->id_to_op_id);
    for (int i = 0; i < m->size; ++i)
        borISetFree(m->op_mutex + i);
    if (m->op_mutex != NULL)
        BOR_FREE(m->op_mutex);
}

int pddlOpMutexPairsSize(const pddl_op_mutex_pairs_t *m)
{
    return m->num_op_mutex_pairs;
}

void pddlOpMutexPairsMutexWith(const pddl_op_mutex_pairs_t *m, int op_id,
                               bor_iset_t *out)
{
    if (m->op_id_to_id[op_id] >= 0)
        borISetUnion(out, m->op_mutex + m->op_id_to_id[op_id]);
    for (int i = op_id - 1; i >= 0; --i){
        if (pddlOpMutexPairsIsMutex(m, i, op_id))
                borISetAdd(out, i);
    }
}

static void registerNewOp(pddl_op_mutex_pairs_t *m, int op_id)
{
    if (m->size == m->alloc){
        if (m->alloc == 0)
            m->alloc = 32;
        m->alloc *= 2;
        m->op_mutex = BOR_REALLOC_ARR(m->op_mutex, bor_iset_t, m->alloc);
        m->id_to_op_id = BOR_REALLOC_ARR(m->id_to_op_id, int, m->alloc);
    }

    int id = m->size++;
    m->op_id_to_id[op_id] = id;
    m->id_to_op_id[id] = op_id;
    borISetInit(m->op_mutex + id);
}

void pddlOpMutexPairsAdd(pddl_op_mutex_pairs_t *m, int o1, int o2)
{
    if (m->op_id_to_id[o1] == -1)
        registerNewOp(m, o1);
    if (m->op_id_to_id[o2] == -1)
        registerNewOp(m, o2);
    if (o1 <= o2){
        int id1 = m->op_id_to_id[o1];
        int s = m->num_op_mutex_pairs - borISetSize(&m->op_mutex[id1]);
        borISetAdd(&m->op_mutex[id1], o2);
        m->num_op_mutex_pairs = s + borISetSize(&m->op_mutex[id1]);
    }else{
        int id2 = m->op_id_to_id[o2];
        int s = m->num_op_mutex_pairs - borISetSize(&m->op_mutex[id2]);
        borISetAdd(&m->op_mutex[id2], o1);
        m->num_op_mutex_pairs = s + borISetSize(&m->op_mutex[id2]);
    }
}

void pddlOpMutexPairsAddGroup(pddl_op_mutex_pairs_t *m, const bor_iset_t *g)
{
    if (borISetSize(g) <= 1)
        return;

    BOR_ISET(group);
    int oid, id;

    borISetUnion(&group, g);

    BOR_ISET_FOR_EACH(g, oid){
        if (m->op_id_to_id[oid] == -1)
            registerNewOp(m, oid);
        id = m->op_id_to_id[oid];
        borISetRm(&group, oid);

        int s = m->num_op_mutex_pairs - borISetSize(&m->op_mutex[id]);
        borISetUnion(&m->op_mutex[id], &group);
        m->num_op_mutex_pairs = s + borISetSize(&m->op_mutex[id]);
    }

    borISetFree(&group);
}

void pddlOpMutexPairsRm(pddl_op_mutex_pairs_t *m, int o1, int o2)
{
    if (m->op_id_to_id[o1] == -1 || m->op_id_to_id[o2] == -1)
        return;
    if (o1 <= o2){
        int id1 = m->op_id_to_id[o1];
        int s = m->num_op_mutex_pairs - borISetSize(&m->op_mutex[id1]);
        borISetRm(&m->op_mutex[id1], o2);
        m->num_op_mutex_pairs = s + borISetSize(&m->op_mutex[id1]);
    }else{
        int id2 = m->op_id_to_id[o2];
        int s = m->num_op_mutex_pairs - borISetSize(&m->op_mutex[id2]);
        borISetRm(&m->op_mutex[id2], o1);
        m->num_op_mutex_pairs = s + borISetSize(&m->op_mutex[id2]);
    }
}

int pddlOpMutexPairsIsMutex(const pddl_op_mutex_pairs_t *m, int o1, int o2)
{
    int id1 = m->op_id_to_id[o1];
    int id2 = m->op_id_to_id[o2];
    if (id1 == -1 || id2 == -1)
        return 0;

    if (o1 <= o2){
        return borISetIn(o2, m->op_mutex + id1);
    }else{
        return borISetIn(o1, m->op_mutex + id2);
    }
}

void pddlOpMutexPairsMinus(pddl_op_mutex_pairs_t *m,
                           const pddl_op_mutex_pairs_t *n)
{
    int o1, o2;
    PDDL_OP_MUTEX_PAIRS_FOR_EACH(n, o1, o2)
        pddlOpMutexPairsRm(m, o1, o2);
}

void pddlOpMutexPairsUnion(pddl_op_mutex_pairs_t *m,
                           const pddl_op_mutex_pairs_t *n)
{
    int o1, o2;
    PDDL_OP_MUTEX_PAIRS_FOR_EACH(n, o1, o2)
        pddlOpMutexPairsAdd(m, o1, o2);
}
