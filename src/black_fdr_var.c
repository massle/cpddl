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

#include <boruvka/lp.h>
#include "pddl/config.h"
#include "pddl/invertibility.h"
#include "pddl/scc.h"
#include "pddl/black_fdr_var.h"

struct fact_vertex {
    int fact;
    int mgroup;
};
typedef struct fact_vertex fact_vertex_t;

struct black_vars {
    int fact_size;
    bor_iset_t invertible_facts;
    fact_vertex_t *fact_vertex;
    int fact_vertex_size;
    bor_iset_t *fact_to_fact_vertex;
    pddl_scc_graph_t cg;
};
typedef struct black_vars black_vars_t;

static int numFactVertices(const pddl_mgroups_t *mgroups,
                           const bor_iset_t *invertible_facts)
{
    int num_vert = 0;
    BOR_ISET(facts);
    BOR_ISET(mgfacts);
    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        borISetIntersect2(&mgfacts, invertible_facts,
                          &mgroups->mgroup[mgi].mgroup);
        num_vert += borISetSize(&mgfacts);
        borISetUnion(&facts, &mgfacts);
    }
    num_vert += borISetSize(invertible_facts) - borISetSize(&facts);
    borISetFree(&facts);
    borISetFree(&mgfacts);
    return num_vert;
}

static void blackVarsCGInit(black_vars_t *bv,
                            pddl_scc_graph_t *cg,
                            const pddl_strips_t *strips,
                            const pddl_mgroups_t *mgroups)
{
    pddlSCCGraphInit(cg, bv->fact_vertex_size);

    bor_iset_t *from = BOR_CALLOC_ARR(bor_iset_t, bv->fact_vertex_size);
    bor_iset_t *to = BOR_CALLOC_ARR(bor_iset_t, bv->fact_vertex_size);

    BOR_ISET(facts);
    for (int op_id = 0; op_id < strips->op.op_size; ++op_id){
        const pddl_strips_op_t *op = strips->op.op[op_id];
        borISetUnion2(&facts, &op->add_eff, &op->del_eff);
        int fact;
        BOR_ISET_FOR_EACH(&facts, fact){
            int vert_id;
            BOR_ISET_FOR_EACH(bv->fact_to_fact_vertex + fact, vert_id)
                borISetAdd(&to[vert_id], op_id);
        }

        borISetUnion(&facts, &op->pre);
        BOR_ISET_FOR_EACH(&facts, fact){
            int vert_id;
            BOR_ISET_FOR_EACH(bv->fact_to_fact_vertex + fact, vert_id)
                borISetAdd(&from[vert_id], op_id);
        }

        for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
            if (!mgroups->mgroup[mgi].is_fam_group)
                continue;

            const bor_iset_t *mg = &mgroups->mgroup[mgi].mgroup;
            if (!borISetIsDisjoint(mg, &op->pre)){
                BOR_ISET_FOR_EACH(mg, fact){
                    int vert_id;
                    BOR_ISET_FOR_EACH(bv->fact_to_fact_vertex + fact, vert_id){
                        if (bv->fact_vertex[vert_id].mgroup == mgi)
                            borISetAdd(&from[vert_id], op_id);
                    }
                }
            }
        }
    }
    borISetFree(&facts);

    for (int vfrom = 0; vfrom < bv->fact_vertex_size; ++vfrom){
        for (int vto = 0; vto < bv->fact_vertex_size; ++vto){
            if (vfrom == vto)
                continue;
            if (!borISetIsDisjoint(from + vfrom, to + vto))
                pddlSCCGraphAddEdge(cg, vfrom, vto);
        }
    }

    for (int f = 0; f < bv->fact_vertex_size; ++f){
        borISetFree(from + f);
        borISetFree(to + f);
    }
    BOR_FREE(from);
    BOR_FREE(to);
}

static void blackVarsInit(black_vars_t *bv,
                          const pddl_strips_t *strips,
                          const pddl_mgroups_t *mgroups,
                          bor_err_t *err)
{
    bzero(bv, sizeof(*bv));
    bv->fact_size = strips->fact.fact_size;
    // TODO: mgroups should be only fam-groups
    pddlRSEInvertibleFacts(strips, mgroups, &bv->invertible_facts, err);
    BOR_INFO(err, "Invertible facts: %d/%d",
            borISetSize(&bv->invertible_facts), bv->fact_size);

    bv->fact_to_fact_vertex = BOR_CALLOC_ARR(bor_iset_t, bv->fact_size);
    bv->fact_vertex_size = numFactVertices(mgroups, &bv->invertible_facts);
    BOR_INFO(err, "Fact-mgroup pairs: %d", bv->fact_vertex_size);
    bv->fact_vertex = BOR_CALLOC_ARR(fact_vertex_t, bv->fact_vertex_size);
    BOR_ISET(facts);
    int vert_id = 0;
    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        borISetIntersect2(&facts, &bv->invertible_facts,
                                  &mgroups->mgroup[mgi].mgroup);
        int fact;
        BOR_ISET_FOR_EACH(&facts, fact){
            bv->fact_vertex[vert_id].fact = fact;
            bv->fact_vertex[vert_id].mgroup = mgi;
            borISetAdd(bv->fact_to_fact_vertex + fact, vert_id);
            ++vert_id;
        }
    }
    borISetFree(&facts);

    int fact;
    BOR_ISET_FOR_EACH(&bv->invertible_facts, fact){
        if (borISetSize(bv->fact_to_fact_vertex + fact) == 0){
            bv->fact_vertex[vert_id].fact = fact;
            bv->fact_vertex[vert_id].mgroup = -1;
            borISetAdd(bv->fact_to_fact_vertex + fact, vert_id);
            ++vert_id;
        }
    }

    blackVarsCGInit(bv, &bv->cg, strips, mgroups);
}

static void blackVarsFree(black_vars_t *bv)
{
    borISetFree(&bv->invertible_facts);
    for (int f = 0; f < bv->fact_size; ++f)
        borISetFree(bv->fact_to_fact_vertex + f);
    BOR_FREE(bv->fact_to_fact_vertex);
    BOR_FREE(bv->fact_vertex);
    pddlSCCGraphFree(&bv->cg);
}

static bor_lp_t *createLP(const black_vars_t *bv)
{
    unsigned lp_flags;
    lp_flags  = BOR_LP_DEFAULT;
    lp_flags |= BOR_LP_NUM_THREADS(1);
    lp_flags |= BOR_LP_MAX;
    bor_lp_t *lp = borLPNew(0, bv->fact_vertex_size, lp_flags);
    for (int vi = 0; vi < bv->fact_vertex_size; ++vi){
        borLPSetObj(lp, vi, 1.);
        borLPSetVarBinary(lp, vi);
    }

    return lp;
}

static void addCycle(bor_lp_t *lp, const bor_iarr_t *cycle)
{
    int row = borLPNumRows(lp);
    double rhs = borIArrSize(cycle) - 1;
    char sense = 'L';
    borLPAddRows(lp, 1, &rhs, &sense);
    int var;
    BOR_IARR_FOR_EACH(cycle, var)
        borLPSetCoef(lp, row, var, 1.);
}

static void addCycles2(bor_lp_t *lp, const black_vars_t *bv, bor_err_t *err)
{
    int num = 0;
    for (int v1 = 0; v1 < bv->fact_vertex_size; ++v1){
        int v2;
        BOR_ISET_FOR_EACH(&bv->cg.node[v1], v2){
            // Skip cycles with the same mutex group
            if (bv->fact_vertex[v1].mgroup == bv->fact_vertex[v2].mgroup)
                continue;
            if (borISetIn(v1, &bv->cg.node[v2])){
                BOR_IARR(path);
                borIArrAdd(&path, v1);
                borIArrAdd(&path, v2);
                addCycle(lp, &path);
                borIArrFree(&path);
                ++num;
            }
        }
    }
    BOR_INFO(err, "Added %d 2-cycles", num);
}

static void addCycles3(bor_lp_t *lp, const black_vars_t *bv, bor_err_t *err)
{
    int num = 0;
    for (int v1 = 0; v1 < bv->fact_vertex_size; ++v1){
        int v1mgroup = bv->fact_vertex[v1].mgroup;
        int v2;
        BOR_ISET_FOR_EACH(&bv->cg.node[v1], v2){
            // Skip 2-vertex cycles
            if (borISetIn(v1, &bv->cg.node[v2]))
                continue;
            int v2mgroup = bv->fact_vertex[v2].mgroup;
            int v3;
            BOR_ISET_FOR_EACH(&bv->cg.node[v2], v3){
                int v3mgroup = bv->fact_vertex[v3].mgroup;
                // Skip cycles with the same mutex group
                if (v1mgroup == v2mgroup && v2mgroup == v3mgroup)
                    continue;

                if (borISetIn(v1, &bv->cg.node[v3])){
                    BOR_IARR(path);
                    borIArrAdd(&path, v1);
                    borIArrAdd(&path, v2);
                    borIArrAdd(&path, v3);
                    addCycle(lp, &path);
                    borIArrFree(&path);
                    ++num;
                }
            }
        }
    }
    BOR_INFO(err, "Added %d 2-cycles", num);
}

static int compIsSingleMGroup(const black_vars_t *bv, const bor_iset_t *comp)
{
    int mgi = bv->fact_vertex[borISetGet(comp, 0)].mgroup;
    int var;
    BOR_ISET_FOR_EACH(comp, var){
        if (bv->fact_vertex[var].mgroup != mgi)
            return 0;
    }
    return 1;
}

static int findMultiMGroupComponent(const black_vars_t *bv,
                                    const pddl_scc_graph_t *black_graph,
                                    bor_iset_t *comp)
{
    int found = 0;
    pddl_scc_t scc;
    pddlSCC(&scc, black_graph);
    for (int i = 0; i < scc.comp_size; ++i){
        if (!compIsSingleMGroup(bv, &scc.comp[i])){
            borISetEmpty(comp);
            borISetUnion(comp, &scc.comp[i]);
            found = 1;
        }
    }
    pddlSCCFree(&scc);
    return found;
}

static void updateLPWithCycle(bor_lp_t *lp,
                              const black_vars_t *bv,
                              const pddl_scc_graph_t *black_graph,
                              const bor_iset_t *comp)
{
    pddl_scc_graph_t graph;
    pddlSCCGraphInitInduced(&graph, black_graph, comp);
    pddl_graph_simple_cycles_t cycles;
    pddlGraphSimpleCycles(&cycles, &graph);
    for (int i = 0; i < cycles.cycle_size; ++i){
        addCycle(lp, &cycles.cycle[i]);
    }
    pddlGraphSimpleCyclesFree(&cycles);
    pddlSCCGraphFree(&graph);
}

static int solveLP(bor_lp_t *lp,
                   const black_vars_t *bv,
                   bor_iset_t *black_vars)
{
    double *obj = BOR_CALLOC_ARR(double, bv->fact_vertex_size);
    double val;
    if (borLPSolve(lp, &val, obj) != 0){
        // TODO;
        fprintf(stderr, "Err\n");
        BOR_FREE(obj);
        return -1;
    }

    borISetEmpty(black_vars);
    for (int v = 0; v < bv->fact_vertex_size; ++v){
        if (obj[v] >= .5)
            borISetAdd(black_vars, v);
    }
    BOR_FREE(obj);
    return 0;
}

static int findBlackVarsUsingLP(bor_lp_t *lp,
                                const black_vars_t *bv,
                                const pddl_strips_t *strips,
                                bor_err_t *err)
{
    int ret = 0;
    BOR_ISET(comp);
    BOR_ISET(black_vars);
    while ((ret = solveLP(lp, bv, &black_vars)) == 0){
        BOR_INFO(err, "Solved. Candidate set size: %d",
                 borISetSize(&black_vars));

#ifdef PDDL_DEBUG
        int v;
        BOR_ISET_FOR_EACH(&black_vars, v){
            BOR_INFO(err, "Black fact %d(%s), mgroup: %d",
                     bv->fact_vertex[v].fact,
                     strips->fact.fact[bv->fact_vertex[v].fact]->name,
                     bv->fact_vertex[v].mgroup);
        }
#endif /* PDDL_DEBUG */
        pddl_scc_graph_t black_graph;
        pddlSCCGraphInitInduced(&black_graph, &bv->cg, &black_vars);
        if (findMultiMGroupComponent(bv, &black_graph, &comp)){
            updateLPWithCycle(lp, bv, &black_graph, &comp);
        }else{
            break;
        }
        pddlSCCGraphFree(&black_graph);
    }
    if (ret == 0){
        BOR_INFO(err, "Found %d black facts", borISetSize(&black_vars));
    }
    borISetFree(&black_vars);
    borISetFree(&comp);
    return ret;
}



void pddlBlackVars(const pddl_strips_t *strips,
                   const pddl_mgroups_t *mgroups,
                   bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Black-vars-LP: ");
    black_vars_t bv;
    blackVarsInit(&bv, strips, mgroups, err);
    bor_lp_t *lp = createLP(&bv);
    addCycles2(lp, &bv, err);
    addCycles3(lp, &bv, err);
    findBlackVarsUsingLP(lp, &bv, strips, err);
    borLPDel(lp);
    blackVarsFree(&bv);
    BOR_INFO_PREFIX_POP(err);
}

