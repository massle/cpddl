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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <boruvka/alloc.h>
#include "pddl/op_mutex_sym_redundant.h"
#include "assert.h"

struct reduce_gen {
    int gen_id;
    int is_destroyed;
    const pddl_strips_sym_gen_t *gen;
    bor_iset_t relevant_op;
    bor_iset_t *op_mutex_with;
    int op_size;

    bor_iset_t redundant_set;
    int num_destroyed_syms;

    bor_iset_t to_preserve;

    bor_iset_t tmp_to_preserve; /*!< Operators that need to be pruned in
                                     order to preserve this symmetry */
};
typedef struct reduce_gen reduce_gen_t;

struct reduce {
    bor_iset_t *op_mutex_with;
    int op_size;
    reduce_gen_t *gen;
    int gen_size;
    bor_iset_t pruned_ops;
};
typedef struct reduce reduce_t;


static void reduceGenInit(reduce_gen_t *rgen,
                          int gen_id,
                          const reduce_t *red,
                          const pddl_strips_sym_gen_t *gen,
                          const pddl_strips_sym_t *sym,
                          const pddl_strips_t *strips,
                          const pddl_op_mutex_pairs_t *op_mutex)
{
    bzero(rgen, sizeof(*rgen));
    rgen->gen_id = gen_id;
    rgen->gen = gen;

    const pddl_strips_op_t *op;
    PDDL_STRIPS_OPS_FOR_EACH(&strips->op, op){
        if (gen->op[op->id] != op->id
                && pddlOpMutexPairsIsMutex(op_mutex, op->id, gen->op[op->id]))
            borISetAdd(&rgen->relevant_op, op->id);
    }

    rgen->op_size = strips->op.op_size;
    rgen->op_mutex_with = BOR_CALLOC_ARR(bor_iset_t, rgen->op_size);

    int op_id;
    BOR_ISET_FOR_EACH(&rgen->relevant_op, op_id){
        borISetIntersect2(rgen->op_mutex_with + op_id,
                          red->op_mutex_with + op_id,
                          &rgen->relevant_op);
    }
}

static void reduceGenFree(reduce_gen_t *rgen)
{
    borISetFree(&rgen->relevant_op);
    borISetFree(&rgen->to_preserve);
    borISetFree(&rgen->tmp_to_preserve);

    for (int i = 0; i < rgen->op_size; ++i){
        borISetFree(rgen->op_mutex_with + i);
    }
    if (rgen->op_mutex_with != NULL)
        BOR_FREE(rgen->op_mutex_with);
}

static void reduceInit(reduce_t *red,
                       const pddl_strips_t *strips,
                       const pddl_strips_sym_t *sym,
                       const pddl_op_mutex_pairs_t *op_mutex)
{
    bzero(red, sizeof(*red));
    red->op_size = strips->op.op_size;
    red->op_mutex_with = BOR_CALLOC_ARR(bor_iset_t, red->op_size);
    const pddl_strips_op_t *op;
    PDDL_STRIPS_OPS_FOR_EACH(&strips->op, op){
        pddlOpMutexPairsMutexWith(op_mutex, op->id,
                                  red->op_mutex_with + op->id);
    }

    red->gen_size = sym->gen_size;
    red->gen = BOR_CALLOC_ARR(reduce_gen_t, sym->gen_size);
    for (int i = 0; i < sym->gen_size; ++i){
        reduceGenInit(red->gen + i, i, red,
                      sym->gen + i, sym, strips, op_mutex);
    }
}

static void reduceFree(reduce_t *red)
{
    for (int i = 0; i < red->gen_size; ++i)
        reduceGenFree(red->gen + i);
    if (red->gen != NULL)
        BOR_FREE(red->gen);
}

/*
static int isDestroyed(const reduce_gen_t *rgen, const bor_iset_t *pruned_ops)
{
    int op;
    BOR_ISET_FOR_EACH(pruned_ops, op){
        if (!borISetIn(rgen->gen->op[op], pruned_ops))
            return 1;
    }
    return 0;
}

static void destroyedSymmetries(reduce_gen_t *rgen,
                                const reduce_t *red,
                                int prune_op)
{
    borISetEmpty(&rgen->destroyed_syms);

    for (int gi = 0; gi < red->gen_size; ++gi){
        const pddl_sym_gen_t *gen = red->gen[gi].gen;
        int op;

        if (prune_op >= 0
                && gen->op[prune_op] != prune_op
                && !borISetIn(gen->op[prune_op], &rgen->redundant_set)
                && !borISetIn(gen->op[prune_op], &red->pruned_ops)){
            borISetAdd(&rgen->destroyed_syms, gi);
            continue;
        }

        int cont = 0;
        BOR_ISET_FOR_EACH(&rgen->redundant_set, op){
            if (gen->op[op] != prune_op
                    && !borISetIn(gen->op[op], &rgen->redundant_set)
                    && !borISetIn(gen->op[op], &red->pruned_ops)){
                borISetAdd(&rgen->destroyed_syms, gi);
                cont = 1;
                break;
            }
        }
        if (cont)
            continue;

        BOR_ISET_FOR_EACH(&red->pruned_ops, op){
            if (gen->op[op] != prune_op
                    && !borISetIn(gen->op[op], &rgen->redundant_set)
                    && !borISetIn(gen->op[op], &red->pruned_ops)){
                borISetAdd(&rgen->destroyed_syms, gi);
                break;
            }
        }
    }
}
*/

static void resetToPreserve(reduce_t *red)
{
    for (int gi = 0; gi < red->gen_size; ++gi){
        reduce_gen_t *rgen = red->gen + gi;
        borISetEmpty(&rgen->tmp_to_preserve);
        int op;
        BOR_ISET_FOR_EACH(&red->pruned_ops, op){
            if (!borISetIn(rgen->gen->op[op], &red->pruned_ops))
                borISetAdd(&rgen->tmp_to_preserve, rgen->gen->op[op]);
        }
    }
}

static void updateToPreserveSet(reduce_gen_t *rgen, int prune_op,
                                bor_iset_t *to_preserve)
{
    if (borISetIn(prune_op, to_preserve)){
        borISetRm(to_preserve, prune_op);
    }else if (rgen->gen->op[prune_op] != prune_op){
        borISetAdd(to_preserve, rgen->gen->op[prune_op]);
    }
}

static void updateToPreserve(reduce_t *red, int prune_op)
{
    for (int gi = 0; gi < red->gen_size; ++gi){
        reduce_gen_t *rgen = red->gen + gi;
        updateToPreserveSet(rgen, prune_op, &rgen->tmp_to_preserve);
    }
}

static int numDestroyedSymmetriesWithToPreserve(reduce_gen_t *rgen,
                                                const reduce_t *red,
                                                int prune_op)
{
    int num = 0;

    for (int gi = 0; gi < red->gen_size; ++gi){
        if (prune_op >= 0 && gi == rgen->gen_id){
            ++num;
            continue;
        }

        const reduce_gen_t *gen = red->gen + gi;
        int size = borISetSize(&gen->tmp_to_preserve);
        if (size > 1
                || (size == 1 && !borISetIn(prune_op, &gen->tmp_to_preserve))
                || (size == 0 && prune_op >= 0
                        && gen->gen->op[prune_op] != prune_op)){
            ++num;
        }
    }
    return num;
}

static int selectCandidate(reduce_gen_t *rgen,
                           const reduce_t *red,
                           const bor_iset_t *cand)
{
    int sel = -1;
    int destroy = INT_MAX;
    int size = 0;
    int op;

    BOR_ISET_FOR_EACH(cand, op){
        int dest = numDestroyedSymmetriesWithToPreserve(rgen, red, op);
        int siz = borISetSize(&rgen->op_mutex_with[op]);

        if (dest < destroy || (dest == destroy && siz > size)){
        //if (siz > size){
            sel = op;
            destroy = dest;
            size = siz;
        }
    }

    return sel;
}

static void findRedundantSet(reduce_gen_t *rgen, reduce_t *red)
{

    borISetEmpty(&rgen->redundant_set);
    rgen->num_destroyed_syms = 0;
    if (borISetSize(&rgen->relevant_op) == 0)
        return;

    resetToPreserve(red);

    BOR_ISET(cand);
    borISetUnion(&cand, &rgen->relevant_op);
    while (borISetSize(&cand) > 0){
        int prune_op = selectCandidate(rgen, red, &cand);
        int keep_op = rgen->gen->op[prune_op];
        borISetAdd(&rgen->redundant_set, prune_op);
        borISetRm(&cand, prune_op);
        borISetIntersect(&cand, &rgen->op_mutex_with[keep_op]);

        updateToPreserve(red, prune_op);
    }

    rgen->num_destroyed_syms
            = numDestroyedSymmetriesWithToPreserve(rgen, red, -1);

    borISetFree(&cand);
}

static void pruneRedundantSet(reduce_t *red, const bor_iset_t *redundant)
{
    borISetUnion(&red->pruned_ops, redundant);
    for (int gi = 0; gi < red->gen_size; ++gi){
        reduce_gen_t *rgen = red->gen + gi;
        borISetMinus(&rgen->relevant_op, redundant);
        for (int oi = 0; oi < rgen->op_size; ++oi)
            borISetMinus(&rgen->op_mutex_with[oi], redundant);

        int op;
        BOR_ISET_FOR_EACH(redundant, op)
            updateToPreserveSet(rgen, op, &rgen->to_preserve);
        rgen->is_destroyed = 0;
        if (borISetSize(&rgen->to_preserve) > 0)
            rgen->is_destroyed = 1;
    }
}

static void computeRedundantSets(reduce_t *red, bor_err_t *err)
{
    BOR_INFO2(err, "  --> Computing redundant sets for each symmetry");
    for (int gi = 0; gi < red->gen_size; ++gi){
        reduce_gen_t *rgen = red->gen + gi;
        if (rgen->is_destroyed){
            borISetEmpty(&rgen->redundant_set);
            rgen->num_destroyed_syms = 0;
            continue;
        }

        findRedundantSet(rgen, red);
        /*
        BOR_INFO(err, "    --> Sym %d: size: %d, destroyed symmetries: %d,"
                      " relevant ops: %d",
                 gi, borISetSize(&rgen->redundant_set),
                 rgen->num_destroyed_syms,
                 borISetSize(&rgen->relevant_op));
        */
    }
}

static int selectRedundantSet(const reduce_t *red, bor_err_t *err)
{
    int best_size = -1;
    int best_sym_destroyed = INT_MAX;
    int best = -1;
    for (int gi = 0; gi < red->gen_size; ++gi){
        const reduce_gen_t *rgen = red->gen + gi;
        if (rgen->is_destroyed)
            continue;
        if (borISetSize(&rgen->redundant_set) == 0)
            continue;

        int size = borISetSize(&rgen->redundant_set);
        int destr = rgen->num_destroyed_syms;
        if (destr < best_sym_destroyed
                || (destr == best_sym_destroyed && size > best_size)){
        //if (size > best_size){
            best_size = size;
            best_sym_destroyed = destr;
            best = gi;
        }
    }

    return best;
}

void pddlOpMutexSymRedundantFixpoint(bor_iset_t *redundant_out,
                                     const pddl_strips_t *strips,
                                     const pddl_strips_sym_t *sym,
                                     const pddl_op_mutex_pairs_t *op_mutex,
                                     bor_err_t *err)
{
    reduce_t red;
    int change, gen_id;

    BOR_INFO2(err, "Redundant set with op-mutexes and symmetries:");

    reduceInit(&red, strips, sym, op_mutex);
    BOR_INFO2(err, "  --> Initialized");

    change = 1;
    while (change){
        change = 0;
        computeRedundantSets(&red, err);

        if ((gen_id = selectRedundantSet(&red, err)) >= 0){
            pruneRedundantSet(&red, &red.gen[gen_id].redundant_set);
            change = 1;
            BOR_INFO(err, "  --> Selected redundant set from symmetry"
                     " %d with size %d destroying %d symmetries"
                     " :: overall: %d",
                     gen_id,
                     borISetSize(&red.gen[gen_id].redundant_set),
                     red.gen[gen_id].num_destroyed_syms,
                     borISetSize(&red.pruned_ops));
        }
    }

    BOR_INFO(err, "Op-mutex symmetry redundant operators: %d",
             borISetSize(&red.pruned_ops));


    borISetUnion(redundant_out, &red.pruned_ops);
    reduceFree(&red);
}
