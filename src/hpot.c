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
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <boruvka/hashset.h>
#include <boruvka/rand.h>
#include "pddl/hpot.h"
#include "pddl/pot.h"
#include "pddl/critical_path.h"
#include "assert.h"

#define ROUND_EPS 0.001

static void init(pddl_hpot_t *hpot, int pot_size, int var_size)
{
    bzero(hpot, sizeof(*hpot));
    hpot->pot_size = pot_size;
    hpot->pot = BOR_ALLOC_ARR(double *, pot_size);
    hpot->var_size = var_size;
    for (int i = 0; i < pot_size; ++i)
        hpot->pot[i] = BOR_ALLOC_ARR(double, var_size);
}

static int solve(pddl_hpot_t *hpot, pddl_pot_t *pot, int func)
{
    return pddlPotSolve(pot, hpot->pot[func], hpot->var_size, 0);
}

static int roundOff(double z)
{
    return ceil(z - ROUND_EPS);
}

static double fdrStateEstimateDbl(const double *pot,
                                  const pddl_fdr_vars_t *vars,
                                  const int *state)
{
    double p = 0;
    for (int var = 0; var < vars->var_size; ++var)
        p += pot[vars->var[var].val[state[var]].global_id];
    if (p < 0.)
        return 0;
    return p;
}

static int fdrStateEstimate(const double *pot,
                            const pddl_fdr_vars_t *vars,
                            const int *state)
{
    double p = fdrStateEstimateDbl(pot, vars, state);
    return roundOff(p);
}

static void genStates(const pddl_fdr_t *fdr,
                      const pddl_mutex_pairs_t *mutex,
                      int num_samples,
                      bor_hashset_t *states,
                      bor_err_t *err)
{
    bor_rand_t rnd;
    borRandInit(&rnd);

    unsigned long count = 0UL;
    BOR_ISET(state);
    while (states->size < num_samples){
        borISetEmpty(&state);
        for (int var = 0; var < fdr->var.var_size; ++var){
            int val = borRand(&rnd, 0, fdr->var.var[var].val_size);
            val = BOR_MIN(val, fdr->var.var[var].val_size - 1);
            int global_id = fdr->var.var[var].val[val].global_id;
            borISetAdd(&state, global_id);
        }
        if (mutex == NULL || !pddlMutexPairsIsMutexSet(mutex, &state))
            borHashSetAdd(states, &state);
        if (++count % 100000UL == 0UL){
            BOR_INFO(err, "Pot: tried %lu states, generated %d states",
                     count, states->size);
        }
    }
    borISetFree(&state);
}

static double countStatesMutex(const pddl_mg_strips_t *s,
                               const pddl_mutex_pairs_t *mutex,
                               const bor_iset_t *fixed)
{
    if (pddlMutexPairsIsMutexSet(mutex, fixed))
        return 0.;

    if (fixed == NULL || borISetSize(fixed) == 0){
        double num = borISetSize(&s->mg.mgroup[0].mgroup);
        for (int i = 1; i < s->mg.mgroup_size; ++i)
            num *= borISetSize(&s->mg.mgroup[i].mgroup);
        return num;
    }

    double num = 1.;
    for (int mgi = 0; mgi < s->mg.mgroup_size; ++mgi){
        int mg_size = 0;
        int fact;
        BOR_ISET_FOR_EACH(&s->mg.mgroup[mgi].mgroup, fact){
            if (!pddlMutexPairsIsMutexFactSet(mutex, fact, fixed))
                mg_size += 1;
        }
        num *= (double)mg_size;
    }
    return num;
}

static void setObjAllStatesMutex1(pddl_pot_t *pot,
                                  const pddl_mg_strips_t *s,
                                  const pddl_mutex_pairs_t *mutex)
{
    double *coef = BOR_CALLOC_ARR(double, pot->var_size);
    BOR_ISET(fixed);

    for (int mgi = 0; mgi < s->mg.mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = s->mg.mgroup + mgi;
        double sum = 0.;
        int fixed_fact;
        BOR_ISET_FOR_EACH(&mg->mgroup, fixed_fact){
            borISetEmpty(&fixed);
            borISetAdd(&fixed, fixed_fact);
            coef[fixed_fact] = countStatesMutex(s, mutex, &fixed);
            sum += coef[fixed_fact];
        }
        BOR_ISET_FOR_EACH(&mg->mgroup, fixed_fact)
            coef[fixed_fact] /= sum;
    }

    pddlPotSetObj(pot, coef);

    borISetFree(&fixed);
    if (coef != NULL)
        BOR_FREE(coef);
}

static void setObjAllStatesMutex2(pddl_pot_t *pot,
                                  const pddl_mg_strips_t *s,
                                  const pddl_mutex_pairs_t *mutex)
{
    double *coef = BOR_CALLOC_ARR(double, pot->var_size);
    BOR_ISET(fixed);

    for (int mgi = 0; mgi < s->mg.mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = s->mg.mgroup + mgi;
        double sum = 0.;
        int fixed_fact;
        BOR_ISET_FOR_EACH(&mg->mgroup, fixed_fact){
            coef[fixed_fact] = 0.;
            for (int f = 0; f < s->strips.fact.fact_size; ++f){
                if (f == fixed_fact)
                    continue;

                borISetEmpty(&fixed);
                borISetAdd(&fixed, fixed_fact);
                borISetAdd(&fixed, f);
                ASSERT(borISetSize(&fixed) == 2);
                coef[fixed_fact] += countStatesMutex(s, mutex, &fixed);
            }
            sum += coef[fixed_fact];
        }
        BOR_ISET_FOR_EACH(&mg->mgroup, fixed_fact)
            coef[fixed_fact] /= sum;
    }

    pddlPotSetObj(pot, coef);

    borISetFree(&fixed);
    if (coef != NULL)
        BOR_FREE(coef);
}

static void initPot(pddl_hpot_t *hpot,
                    pddl_pot_t *pot,
                    const pddl_fdr_t *fdr,
                    const pddl_mg_strips_t *mg_strips,
                    const pddl_mutex_pairs_t *mutex,
                    const pddl_hpot_config_t *cfg,
                    bor_err_t *err)
{

    if (cfg->weak_disambiguation){
        pddlPotInitMGStripsSingleFactDisamb(pot, mg_strips, mutex);
        BOR_INFO(err, "Pot: Initialized with weak-disambiguation."
                      " vars: %d, op-constr: %d,"
                      " goal-constr: %d, maxpots: %d",
                      pot->var_size,
                      pot->constr_op.size,
                      pot->constr_goal.size,
                      pot->maxpot_size);

    }else if (cfg->disambiguation){
        pddlPotInitMGStrips(pot, mg_strips, mutex);
        BOR_INFO(err, "Pot: Initialized with disambiguation."
                      " vars: %d, op-constr: %d,"
                      " goal-constr: %d, maxpots: %d",
                      pot->var_size,
                      pot->constr_op.size,
                      pot->constr_goal.size,
                      pot->maxpot_size);

    }else{
        pddlPotInitFDR(pot, fdr);
        BOR_INFO(err, "Pot: Initialized without disambiguation."
                      " vars: %d, op-constr: %d,"
                      " goal-constr: %d, maxpots: %d",
                      pot->var_size,
                      pot->constr_op.size,
                      pot->constr_goal.size,
                      pot->maxpot_size);
    }
}

static int addInitConstr(pddl_hpot_t *hpot,
                         pddl_pot_t *pot,
                         const pddl_fdr_t *fdr,
                         const pddl_hpot_config_t *cfg,
                         bor_err_t *err)
{
    pddlPotResetLowerBoundConstr(pot);
    pddlPotSetObjFDRState(pot, &fdr->var, fdr->init);
    int ret = solve(hpot, pot, 0);
    if (ret != 0){
        BOR_INFO2(err, "Pot: No optimal solution for the initial state");
        return ret;
    }

    double rhs = fdrStateEstimateDbl(hpot->pot[0], &fdr->var, fdr->init);
    BOR_INFO(err, "Pot: Solved for the initial state: %.4f", rhs);
    // make sure it is feasible
    rhs = floor((rhs - ROUND_EPS) * 100.) / 100.;
    rhs *= cfg->init_constr_coef;

    BOR_ISET(vars);
    for (int var = 0; var < fdr->var.var_size; ++var){
        int v = fdr->var.var[var].val[fdr->init[var]].global_id;
        borISetAdd(&vars, v);
    }
    pddlPotSetLowerBoundConstr(pot, &vars, rhs);
    BOR_INFO(err, "Pot: added lower bound constraint with rhs: %.2f", rhs);
    borISetFree(&vars);

    return 0;
}

static int samples(pddl_hpot_t *hpot,
                   pddl_pot_t *pot,
                   const pddl_fdr_t *fdr,
                   const pddl_mutex_pairs_t *mutex,
                   const pddl_hpot_config_t *cfg,
                   bor_err_t *err)
{
    // TODO: Rewrite this: for max, we need to generate samples that are
    // all solvable!
    bor_hashset_t states;
    borHashSetInitISet(&states);

    BOR_INFO(err, "Pot: generating %d samples (mutex: %d)...",
             cfg->num_samples, (mutex != NULL));
    genStates(fdr, mutex, cfg->num_samples, &states, err);
    BOR_INFO(err, "Pot: %d samples generated", cfg->num_samples);

    double *coef = BOR_CALLOC_ARR(double, pot->var_size);
    for (int si = 0; si < states.size; ++si){
        const bor_iset_t *fact_state = borHashSetGet(&states, si);

        if (cfg->obj == PDDL_HPOT_OBJ_SAMPLES_MAX){
            bzero(coef, sizeof(double) * pot->var_size);
            int fact;
            BOR_ISET_FOR_EACH(fact_state, fact)
                coef[fact] = 1.;

            pddlPotSetObj(pot, coef);
            if (solve(hpot, pot, si) != 0)
                return -1;
            if ((si + 1) % 100 == 0){
                BOR_INFO(err, "Pot: Solved for state: %d/%d",
                         si + 1, states.size);
            }
        }else{
            int fact;
            BOR_ISET_FOR_EACH(fact_state, fact)
                coef[fact] += 1.;
        }
    }

    if (cfg->obj == PDDL_HPOT_OBJ_SAMPLES_SUM){
        pddlPotSetObj(pot, coef);
        if (solve(hpot, pot, 0) != 0)
            return -1;
        BOR_INFO(err, "Pot: Solved for a sum of %d states", states.size);
    }

    if (coef != NULL)
        BOR_FREE(coef);
    borHashSetFree(&states);

    return 0;
}

int pddlHPotInit(pddl_hpot_t *hpot,
                 const pddl_fdr_t *fdr,
                 const pddl_hpot_config_t *cfg,
                 bor_err_t *err)
{
    int ret = 0;
    if (fdr->has_cond_eff){
        BOR_INFO2(err, "Pot: Conditional effects are not supported");
        return -1;
    }

    int num_funcs = 1;
    if (cfg->obj == PDDL_HPOT_OBJ_SAMPLES_MAX)
        num_funcs = cfg->num_samples;
    BOR_INFO(err, "Pot: Allocating %d potential functions ...", num_funcs);
    init(hpot, num_funcs, fdr->var.global_id_size);

    pddl_mg_strips_t mg_strips;
    pddl_mutex_pairs_t mutex;
    int need_mutex = 0;
    if (cfg->disambiguation
            || cfg->weak_disambiguation
            || cfg->samples_use_mutex
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX){
        need_mutex = 1;
        pddlMGStripsInitFDR(&mg_strips, fdr);
        pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);
        pddlMutexPairsAddMGroups(&mutex, &mg_strips.mg);
        pddlH2(&mg_strips.strips, &mutex, NULL, NULL, err);
    }

    pddl_pot_t pot;
    initPot(hpot, &pot, fdr, &mg_strips, &mutex, cfg, err);
    if (cfg->add_init_constr){
        if (addInitConstr(hpot, &pot, fdr, cfg, err) != 0){
            pddlPotFree(&pot);
            return -1;
        }
    }

    if (cfg->obj == PDDL_HPOT_OBJ_INIT){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);
        ret = solve(hpot, &pot, 0);
        BOR_INFO(err, "Pot: Solved for the initial state: %d", ret);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES){
        pddlPotSetObjFDRAllSyntacticStates(&pot, &fdr->var);
        ret = solve(hpot, &pot, 0);
        BOR_INFO(err, "Pot: Solved for all states: %d", ret);

    }else if (cfg->obj == PDDL_HPOT_OBJ_SAMPLES_MAX
                || cfg->obj == PDDL_HPOT_OBJ_SAMPLES_SUM){
        const pddl_mutex_pairs_t *m = NULL;
        if (cfg->samples_use_mutex)
            m = &mutex;
        ret = samples(hpot, &pot, fdr, m, cfg, err);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX){
        if (cfg->all_states_mutex_size == 1){
            setObjAllStatesMutex1(&pot, &mg_strips, &mutex);
            ret = solve(hpot, &pot, 0);
        }else if (cfg->all_states_mutex_size == 2){
            setObjAllStatesMutex2(&pot, &mg_strips, &mutex);
            ret = solve(hpot, &pot, 0);
        }else{
            BOR_FATAL("all-states-mutex with size %d unsupported!",
                      cfg->all_states_mutex_size);
        }

    }else{
        if (need_mutex){
            pddlMutexPairsFree(&mutex);
            pddlMGStripsFree(&mg_strips);
        }
        pddlPotFree(&pot);
        BOR_ERR_RET(err, -1, "Unkown objective function for potential"
                             " heuristic: %d", cfg->obj);
    }

    if (need_mutex){
        pddlMutexPairsFree(&mutex);
        pddlMGStripsFree(&mg_strips);
    }
    pddlPotFree(&pot);

    if (ret != 0)
        BOR_INFO2(err, "Pot: No optimal solution found");

    return ret;
}

double pddlHPotFDRStateEstimateDbl(const pddl_hpot_t *hpot,
                                   const pddl_fdr_vars_t *vars,
                                   const int *state)
{
    if (hpot->pot_size <= 0)
        return -1;

    double est = fdrStateEstimateDbl(hpot->pot[0], vars, state);
    for (int p = 1; p < hpot->pot_size; ++p){
        double e = fdrStateEstimateDbl(hpot->pot[p], vars, state);
        if (e > est)
            est = e;
    }
    return est;
}

int pddlHPotFDRStateEstimate(const pddl_hpot_t *hpot,
                             const pddl_fdr_vars_t *vars,
                             const int *state)
{
    if (hpot->pot_size <= 0)
        return -1;

    int est = fdrStateEstimate(hpot->pot[0], vars, state);
    for (int p = 1; p < hpot->pot_size; ++p){
        int e = fdrStateEstimate(hpot->pot[p], vars, state);
        if (e > est)
            est = e;
    }
    return est;
}

void pddlHPotFree(pddl_hpot_t *hpot)
{
    for (int i = 0; i < hpot->pot_size; ++i)
        BOR_FREE(hpot->pot[i]);
    if (hpot->pot != NULL)
        BOR_FREE(hpot->pot);
}
