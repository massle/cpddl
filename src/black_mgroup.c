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
#include "pddl/strips_fact_cross_ref.h"
#include "pddl/black_mgroup.h"

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

static void uncoveredDelEffs(const pddl_strips_t *strips, bor_iset_t *facts)
{
    BOR_ISET(deleff);
    borISetEmpty(facts);
    for (int opi = 0; opi < strips->op.op_size; ++opi){
        const pddl_strips_op_t *op = strips->op.op[opi];
        borISetMinus2(&deleff, &op->del_eff, &op->pre);
        borISetUnion(facts, &deleff);
    }
    borISetFree(&deleff);
}

static void blackVarsInit(black_vars_t *bv,
                          const pddl_strips_t *strips,
                          const pddl_mgroups_t *mgroups,
                          bor_err_t *err)
{
    bzero(bv, sizeof(*bv));
    bv->fact_size = strips->fact.fact_size;
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

    // Delete effects that are uncovered by preconditions must be encoded
    // as a single-fact variables
    BOR_ISET(uncovered_del_effs);
    uncoveredDelEffs(strips, &uncovered_del_effs);
    BOR_ISET_FOR_EACH(&uncovered_del_effs, fact){
        int vert_id;
        BOR_ISET_FOR_EACH(bv->fact_to_fact_vertex + fact, vert_id)
            bv->fact_vertex[vert_id].mgroup = -1;
    }
    borISetFree(&uncovered_del_effs);

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
            if (bv->fact_vertex[v1].mgroup == bv->fact_vertex[v2].mgroup
                    && bv->fact_vertex[v1].mgroup >= 0){
                continue;
            }

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
                if (v1mgroup == v2mgroup && v2mgroup == v3mgroup
                        && v1mgroup >= 0){
                    continue;
                }

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
    BOR_INFO(err, "Added %d 3-cycles", num);
}

static void addMGroup(bor_lp_t *lp, const bor_iset_t *mg)
{
    int row = borLPNumRows(lp);
    double rhs = borISetSize(mg) - 1;
    char sense = 'L';
    //double rhs = 0;
    //char sense = 'L';
    borLPAddRows(lp, 1, &rhs, &sense);
    int var;
    BOR_ISET_FOR_EACH(mg, var)
        borLPSetCoef(lp, row, var, 1.);
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
            break;
        }
    }
    pddlSCCFree(&scc);
    return found;
}

struct update_lp {
    bor_lp_t *lp;
    const black_vars_t *bv;
};

static int updateLPWithCycleFn(const bor_iarr_t *cycle, void *ud)
{
    int ret = PDDL_GRAPH_SIMPLE_CYCLE_CONT;
    struct update_lp *update = ud;
    BOR_ISET(mg);
    int vert_id;
    BOR_IARR_FOR_EACH(cycle, vert_id){
        borISetAdd(&mg, update->bv->fact_vertex[vert_id].mgroup);
        if (borISetSize(&mg) > 1)
            break;
    }
    if (borISetSize(&mg) > 1){
        addCycle(update->lp, cycle);
        ret = PDDL_GRAPH_SIMPLE_CYCLE_STOP;
    }
    borISetFree(&mg);
    return ret;
}

static void updateLPWithCycle(bor_lp_t *lp,
                              const black_vars_t *bv,
                              const pddl_scc_graph_t *black_graph,
                              const bor_iset_t *comp)
{
    struct update_lp update = { lp, bv };
    pddl_scc_graph_t graph;
    pddlSCCGraphInitInduced(&graph, black_graph, comp);
    pddlGraphSimpleCyclesFn(&graph, updateLPWithCycleFn, &update);
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
        fprintf(stderr, "Error: Could not solve the ILP\n");
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

static pddl_black_mgroup_t *blackMGroupsAdd(pddl_black_mgroups_t *bmgroups,
                                            const bor_iset_t *m)
{
    if (bmgroups->mgroup_size == bmgroups->mgroup_alloc){
        if (bmgroups->mgroup_alloc == 0)
            bmgroups->mgroup_alloc = 2;
        bmgroups->mgroup_alloc *= 2;
        bmgroups->mgroup = BOR_REALLOC_ARR(bmgroups->mgroup,
                                           pddl_black_mgroup_t,
                                           bmgroups->mgroup_alloc);
    }
    pddl_black_mgroup_t *mg = bmgroups->mgroup + bmgroups->mgroup_size++;
    borISetInit(&mg->mgroup);
    borISetUnion(&mg->mgroup, m);
    borISetInit(&mg->mutex_facts);
    return mg;
}

static pddl_black_mgroup_t *blackMGroupsAddSingle(pddl_black_mgroups_t *bmgroups,
                                                  int fact)
{
    pddl_black_mgroup_t *mg;

    BOR_ISET(m);
    borISetAdd(&m, fact);
    mg = blackMGroupsAdd(bmgroups, &m);
    borISetFree(&m);

    return mg;
}

static void blackFactsToBlackMGroups(const black_vars_t *bv,
                                     const bor_iset_t *black_vars,
                                     const pddl_mgroups_t *mgroups,
                                     pddl_black_mgroups_t *bmgroups,
                                     bor_err_t *err)
{
    bor_iset_t *mgs = BOR_CALLOC_ARR(bor_iset_t, mgroups->mgroup_size);
    int vert_id;
    BOR_ISET_FOR_EACH(black_vars, vert_id){
        int fact = bv->fact_vertex[vert_id].fact;
        int mgi = bv->fact_vertex[vert_id].mgroup;
        if (mgi >= 0){
            borISetAdd(mgs + mgi, fact);
        }else{
            blackMGroupsAddSingle(bmgroups, fact);
        }
    }

    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
        if (borISetSize(mgs + mgi) == 0)
            continue;
        blackMGroupsAdd(bmgroups, mgs + mgi);
    }

    for (int bmgi = 0; bmgi < bmgroups->mgroup_size; ++bmgi){
        pddl_black_mgroup_t *mg = bmgroups->mgroup + bmgi;
        if (borISetSize(&mg->mgroup) <= 1)
            continue;
        for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi){
            const bor_iset_t *famgroup = &mgroups->mgroup[mgi].mgroup;
            // Consider only fam-groups that were used to create black
            // mutex groups of size at least 2
            if (borISetSize(mgs + mgi) <= 1)
                continue;

            if (borISetIsSubset(&mg->mgroup, famgroup)){
                BOR_ISET(facts);
                borISetMinus2(&facts, famgroup, &mg->mgroup);
                borISetUnion(&mg->mutex_facts, &facts);
                borISetFree(&facts);
            }
        }
    }

    for (int mgi = 0; mgi < mgroups->mgroup_size; ++mgi)
        borISetFree(mgs + mgi);
    if (mgs != NULL)
        BOR_FREE(mgs);
}

static int mgroupIsLeaf(const bor_iset_t *mgroup,
                        const pddl_strips_t *strips,
                        const bor_iset_t *fact_op)
{
    int fact;
    BOR_ISET_FOR_EACH(mgroup, fact){
        int opi;
        BOR_ISET_FOR_EACH(fact_op + fact, opi){
            const pddl_strips_op_t *op = strips->op.op[opi];
            if (!borISetIsSubset(&op->add_eff, mgroup))
                return 0;
            if (!borISetIsSubset(&op->del_eff, mgroup))
                return 0;
        }
    }

    return 1;
}

static int findAndUpdateLeafs(bor_lp_t *lp,
                              const black_vars_t *bv,
                              const pddl_strips_t *strips,
                              const bor_iset_t *black_vars,
                              int num_mgroups,
                              bor_err_t *err)
{
    int updated = 0;
    pddl_strips_fact_cross_ref_t cref;
    pddlStripsFactCrossRefInit(&cref, strips, 0, 0, 1, 1, 1);

    bor_iset_t *fact_op = BOR_CALLOC_ARR(bor_iset_t, strips->fact.fact_size);
    for (int fact = 0; fact < strips->fact.fact_size; ++fact){
        borISetUnion(fact_op + fact, &cref.fact[fact].op_pre);
        borISetUnion(fact_op + fact, &cref.fact[fact].op_add);
        borISetUnion(fact_op + fact, &cref.fact[fact].op_del);
    }

    bor_iset_t *bmgroups = BOR_CALLOC_ARR(bor_iset_t, num_mgroups);
    bor_iset_t *bmgroups_vert = BOR_CALLOC_ARR(bor_iset_t, num_mgroups);
    int vert_id;
    BOR_ISET_FOR_EACH(black_vars, vert_id){
        if (bv->fact_vertex[vert_id].mgroup >= 0){
            borISetAdd(bmgroups + bv->fact_vertex[vert_id].mgroup,
                       bv->fact_vertex[vert_id].fact);
            borISetAdd(bmgroups_vert + bv->fact_vertex[vert_id].mgroup,
                       vert_id);
        }
    }

    for (int mgi = 0; mgi < num_mgroups; ++mgi){
        if (borISetSize(bmgroups + mgi) == 0)
            continue;

        const bor_iset_t *mg = bmgroups + mgi;
        if (mgroupIsLeaf(mg, strips, fact_op)){
            addMGroup(lp, bmgroups_vert + mgi);
            ++updated;
        }
    }

    for (int i = 0; i < strips->fact.fact_size; ++i)
        borISetFree(fact_op + i);
    BOR_FREE(fact_op);
    for (int i = 0; i < num_mgroups; ++i){
        borISetFree(bmgroups + i);
        borISetFree(bmgroups_vert + i);
    }
    BOR_FREE(bmgroups);
    BOR_FREE(bmgroups_vert);
    pddlStripsFactCrossRefFree(&cref);

    BOR_INFO(err, "Found %d leaf mgroups", updated);
    return updated > 0;
}

static int findBlackVarsUsingLP(bor_lp_t *lp,
                                const black_vars_t *bv,
                                const pddl_strips_t *strips,
                                const pddl_mgroups_t *mgroups,
                                pddl_black_mgroups_t *bmgroups,
                                bor_err_t *err)
{
    int ret = 0;
    BOR_ISET(comp);
    BOR_ISET(black_vars);
    while ((ret = solveLP(lp, bv, &black_vars)) == 0){
        BOR_INFO(err, "Solved. Candidate set size: %d",
                 borISetSize(&black_vars));

        pddl_scc_graph_t black_graph;
        pddlSCCGraphInitInduced(&black_graph, &bv->cg, &black_vars);
        if (findMultiMGroupComponent(bv, &black_graph, &comp)){
            BOR_INFO2(err, "The solution has a cycle."
                           " Updating LP by adding more cycles...");
            updateLPWithCycle(lp, bv, &black_graph, &comp);
            BOR_INFO(err, "Updated. Num constraints: %d", borLPNumRows(lp));

        }else if (findAndUpdateLeafs(lp, bv, strips, &black_vars,
                                     mgroups->mgroup_size, err) == 0){
            pddlSCCGraphFree(&black_graph);
            break;
        }
        pddlSCCGraphFree(&black_graph);
    }
    if (ret == 0){
        BOR_INFO(err, "Found %d black facts", borISetSize(&black_vars));
        blackFactsToBlackMGroups(bv, &black_vars, mgroups, bmgroups, err);
        BOR_INFO(err, "Found %d black mgroups", bmgroups->mgroup_size);
    }
    borISetFree(&black_vars);
    borISetFree(&comp);
    return ret;
}

void pddlBlackMGroups(pddl_black_mgroups_t *bmgroups,
                      const pddl_strips_t *strips,
                      const pddl_mgroups_t *mgroups_in,
                      const pddl_black_mgroups_config_t *cfg,
                      bor_err_t *err)
{
    BOR_INFO_PREFIX_PUSH(err, "Black-mg-LP: ");
    bzero(bmgroups, sizeof(*bmgroups));

    pddl_mgroups_t mgroups;
    pddlMGroupsInitEmpty(&mgroups);
    for (int mgi = 0; mgi < mgroups_in->mgroup_size; ++mgi){
        if (!mgroups_in->mgroup[mgi].is_fam_group)
            continue;

        pddl_mgroup_t *mg;
        mg = pddlMGroupsAdd(&mgroups, &mgroups_in->mgroup[mgi].mgroup);
        mg->is_fam_group = 1;
    }

    black_vars_t bv;
    blackVarsInit(&bv, strips, &mgroups, err);
    bor_lp_t *lp = createLP(&bv);
    if (cfg->lp_add_2cycles)
        addCycles2(lp, &bv, err);
    if (cfg->lp_add_3cycles)
        addCycles3(lp, &bv, err);
    findBlackVarsUsingLP(lp, &bv, strips, &mgroups, bmgroups, err);
    borLPDel(lp);
    blackVarsFree(&bv);
    pddlMGroupsFree(&mgroups);
    BOR_INFO_PREFIX_POP(err);
}

void pddlBlackMGroupsFree(pddl_black_mgroups_t *bmgroups)
{
    for (int i = 0; i < bmgroups->mgroup_size; ++i){
        borISetFree(&bmgroups->mgroup[i].mgroup);
        borISetFree(&bmgroups->mgroup[i].mutex_facts);
    }
    if (bmgroups->mgroup != NULL)
        BOR_FREE(bmgroups->mgroup);
}

void pddlBlackMGroupsPrint(const pddl_strips_t *strips,
                           const pddl_black_mgroups_t *bmgroups,
                           FILE *fout)
{
    for (int mgi = 0; mgi < bmgroups->mgroup_size; ++mgi){
        const pddl_black_mgroup_t *bmg = bmgroups->mgroup + mgi;
        int fact;
        fprintf(fout, "black-mgroup:");
        BOR_ISET_FOR_EACH(&bmg->mgroup, fact)
            fprintf(fout, " %d:(%s)", fact, strips->fact.fact[fact]->name);
        fprintf(fout, "\n");
        fprintf(fout, "  mutex-facts:");
        BOR_ISET_FOR_EACH(&bmg->mutex_facts, fact)
            fprintf(fout, " %d:(%s)", fact, strips->fact.fact[fact]->name);
        fprintf(fout, "\n");
    }
}
