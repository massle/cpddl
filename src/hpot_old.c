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

#include "pddl/rand.h"
#include "pddl/hpot_old.h"
#include "pddl/pot.h"
#include "pddl/critical_path.h"
#include "pddl/random_walk.h"
#include "pddl/heur.h"
#include "pddl/set.h"
#include "_heur.h"
#include "internal.h"

#define ROUND_EPS 0.001
// TODO
//#define INIT_STATE_RHS_DECREASE_STEP 0.25
//#define INIT_STATE_RHS_DECREASE_STEP 0.0625
#define INIT_STATE_RHS_DECREASE_STEP 0.125
#define INIT_STATE_RHS_DECREASE_MAX_STEPS 8

static const uint32_t rand_sampler_seed = 524287;
static const uint32_t rand_diverse_seed = 131071;

void pddlHPotOldConfigLog(const pddl_hpot_old_config_t *cfg, pddl_err_t *err)
{
    LOG_CONFIG_BOOL(cfg, disambiguation, err);
    LOG_CONFIG_BOOL(cfg, weak_disambiguation, err);
    const char *obj = "";
    switch (cfg->obj){
        case PDDL_HPOT_OBJ_INIT:
            obj = "init";
            break;
        case PDDL_HPOT_OBJ_ALL_STATES:
            obj = "all";
            break;
        case PDDL_HPOT_OBJ_SAMPLES_MAX:
            obj = "samples-max";
            break;
        case PDDL_HPOT_OBJ_SAMPLES_SUM:
            obj = "samples-sum";
            break;
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX:
            obj = "mutex";
            break;
        case PDDL_HPOT_OBJ_DIVERSE:
            obj = "diverse";
            break;
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED:
            obj = "mutex-cond";
            break;
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND:
            obj = "mutex-cond-rand";
            break;
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND2:
            obj = "mutex-cond-rand2";
            break;
        case PDDL_HPOT_OBJ_MAX_INIT_ALL_STATES:
            obj = "max(init,all)";
            break;
    }
    LOG(err, "obj = %{obj}s", obj);
    LOG_CONFIG_BOOL(cfg, add_init_constr, err);
    LOG_CONFIG_DBL(cfg, init_constr_coef, err);
    LOG_CONFIG_INT(cfg, num_samples, err);
    LOG_CONFIG_BOOL(cfg, samples_use_mutex, err);
    LOG_CONFIG_BOOL(cfg, samples_random_walk, err);
    LOG_CONFIG_INT(cfg, all_states_mutex_size, err);
    LOG_CONFIG_BOOL(cfg, op_pot, err);
    LOG_CONFIG_BOOL(cfg, op_pot_real, err);
}

int pddlHPotOldConfigIsEnsemble(const pddl_hpot_old_config_t *cfg)
{
    return cfg->obj == PDDL_HPOT_OBJ_SAMPLES_MAX
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND2
            || cfg->obj == PDDL_HPOT_OBJ_DIVERSE
            || cfg->obj == PDDL_HPOT_OBJ_MAX_INIT_ALL_STATES;
}

static int solveAndAdd(pddl_pot_t *pot,
                       pddl_pot_solutions_t *sols,
                       const pddl_hpot_old_config_t *cfg,
                       pddl_err_t *err)
{
    pddl_pot_solution_t sol;
    pddlPotSolutionInit(&sol);
    int ret = pddlPotSolve(pot, &sol, err);
    for (int i = 0; cfg->add_init_constr && ret != 0
                        && i < INIT_STATE_RHS_DECREASE_MAX_STEPS; ++i){
        pddlPotDecreaseLowerBoundConstrRHS(pot, INIT_STATE_RHS_DECREASE_STEP);
        double rhs = pddlPotSetLowerBoundConstrRHS(pot);
        PDDL_INFO(err, "Solution not found. Setting lower bound constraint to "
                      "%.4f (%a)", rhs, rhs);
        ret = pddlPotSolve(pot, &sol, err);
    }
    if (ret == 0){
        PDDL_INFO(err, "Have a solution. objval: %.4f", sol.objval);
        pddlPotSolutionsAdd(sols, &sol);
    }
    pddlPotSolutionFree(&sol);
    return ret;
}


#if 0
static void addFunc2(pddl_hpot_old_t *hpot, const double *p)
{
    if (hpot->pot_size == hpot->pot_alloc){
        int old_size = hpot->pot_alloc;
        if (hpot->pot_alloc == 0)
            hpot->pot_alloc = 2;
        hpot->pot_alloc *= 2;
        hpot->pot = REALLOC_ARR(hpot->pot, double *, hpot->pot_alloc);
        for (int i = old_size; i < hpot->pot_alloc; ++i)
            hpot->pot[i] = ALLOC_ARR(double, hpot->var_size);
    }
    double *dst = hpot->pot[hpot->pot_size++];
    memcpy(dst, p, sizeof(double) * hpot->var_size);
}

static void addFunc(pddl_hpot_old_t *hpot)
{
    addFunc2(hpot, hpot->func);
}

static int solve2(pddl_hpot_old_t *hpot, pddl_pot_t *pot, double *w)
{
    return pddlPotSolve(pot, w, hpot->var_size, NULL, NULL, 0);
}

static int solve(pddl_hpot_old_t *hpot, pddl_pot_t *pot)
{
    return solve2(hpot, pot, hpot->func);
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
    if (p > 1E8)
        return PDDL_COST_DEAD_END;
    return p;
}

static int fdrStateEstimate(const double *pot,
                            const pddl_fdr_vars_t *vars,
                            const int *state)
{
    return roundOff(fdrStateEstimateDbl(pot, vars, state));
}
#endif

static int initPot(pddl_pot_t *pot,
                   const pddl_fdr_t *fdr,
                   const pddl_mg_strips_t *mg_strips,
                   const pddl_mutex_pairs_t *mutex,
                   const pddl_hpot_old_config_t *cfg,
                   pddl_err_t *err)
{

    if (cfg->weak_disambiguation){
        if (pddlPotInitMGStripsSingleFactDisamb(pot, mg_strips, mutex) == 0){
            PDDL_INFO(err, "Initialized with weak-disambiguation."
                      " vars: %d, op-constr: %d,"
                      " goal-constr: %d, maxpots: %d",
                      pot->var_size,
                      pot->constr_op.size,
                      pot->constr_goal.size,
                      pot->maxpot_size);
        }else{
            PDDL_INFO2(err, "Disambiguation proved the task unsolvable.");
            return -1;
        }

    }else if (cfg->disambiguation){
        if (pddlPotInitMGStrips(pot, mg_strips, mutex) == 0){
            PDDL_INFO(err, "Initialized with disambiguation."
                      " vars: %d, op-constr: %d,"
                      " goal-constr: %d, maxpots: %d",
                      pot->var_size,
                      pot->constr_op.size,
                      pot->constr_goal.size,
                      pot->maxpot_size);
        }else{
            PDDL_INFO2(err, "Disambiguation proved the task unsolvable.");
            return -1;
        }

    }else{
        pddlPotInitFDR(pot, fdr);
        PDDL_INFO(err, "Initialized without disambiguation."
                  " vars: %d, op-constr: %d,"
                  " goal-constr: %d, maxpots: %d",
                  pot->var_size,
                  pot->constr_op.size,
                  pot->constr_goal.size,
                  pot->maxpot_size);
    }

    if (cfg->op_pot)
        pddlPotEnableOpPot(pot, 1, cfg->op_pot_real);

    return 0;
}

static int addInitConstr(pddl_pot_t *pot,
                         const pddl_fdr_t *fdr,
                         const pddl_hpot_old_config_t *cfg,
                         pddl_err_t *err)
{
    pddlPotResetLowerBoundConstr(pot);
    pddlPotSetObjFDRState(pot, &fdr->var, fdr->init);
    pddl_pot_solution_t sol;
    pddlPotSolutionInit(&sol);
    int ret = pddlPotSolve(pot, &sol, err);
    if (ret != 0){
        PDDL_INFO2(err, "No optimal solution for the initial state");
        return ret;
    }

    double sum = pddlPotSolutionEvalFDRStateFlt(&sol, &fdr->var, fdr->init);
    PDDL_INFO(err, "Solved for the initial state: sum: %.4f (%a),"
              " objval: %.4f (%a)", sum, sum, sol.objval, sol.objval);
    double rhs = sol.objval;
    rhs *= cfg->init_constr_coef;

    PDDL_ISET(vars);
    for (int var = 0; var < fdr->var.var_size; ++var){
        int v = fdr->var.var[var].val[fdr->init[var]].global_id;
        pddlISetAdd(&vars, v);
    }
    rhs -= INIT_STATE_RHS_DECREASE_STEP;
    pddlPotSetLowerBoundConstr(pot, &vars, rhs);
    PDDL_INFO(err, "added lower bound constraint with rhs: %.4f (%a)",
              rhs, rhs);
    pddlISetFree(&vars);
    pddlPotSolutionFree(&sol);

    return 0;
}




// TODO: Move to a separate module
#define STATE_SAMPLER_SYNTACTIC 0
#define STATE_SAMPLER_SYNTACTIC_MUTEX 1
#define STATE_SAMPLER_RANDOM_WALK 2
struct state_sampler {
    int type;
    const pddl_fdr_t *fdr;
    const pddl_mutex_pairs_t *mutex;
    pddl_random_walk_t random_walk;
    int random_walk_max_steps;
    pddl_rand_t rnd;
    int *state;
};
typedef struct state_sampler state_sampler_t;

static void stateSamplerInit(state_sampler_t *s,
                             const pddl_hpot_old_config_t *cfg,
                             const pddl_fdr_t *fdr,
                             const pddl_mutex_pairs_t *mutex,
                             pddl_pot_t *pot,
                             pddl_err_t *err)
{
    bzero(s, sizeof(*s));
    s->fdr = fdr;
    s->state = ALLOC_ARR(int, fdr->var.var_size);
    if (cfg->samples_random_walk){
        s->type = STATE_SAMPLER_RANDOM_WALK;
        //pddlRandomWalkInit(&s->random_walk, fdr, NULL);
        pddlRandomWalkInitSeed(&s->random_walk, fdr, NULL, rand_sampler_seed);

        pddlPotSetObjFDRState(pot, &fdr->var, fdr->init);
        pddl_pot_solution_t sol;
        pddlPotSolutionInit(&sol);
        int ret = pddlPotSolve(pot, &sol, err);
        if (ret != 0){
            PDDL_INFO2(err, "No optimal solution for the initial state");
            s->random_walk_max_steps = 0;
        }

        int hinit = pddlPotSolutionEvalFDRState(&sol, &fdr->var, fdr->init);
        pddlPotSolutionFree(&sol);

        double avg_op_cost = 0.;
        for (int oi = 0; oi < fdr->op.op_size; ++oi)
            avg_op_cost += fdr->op.op[oi]->cost;
        avg_op_cost /= fdr->op.op_size;
        if (avg_op_cost < 1E-2){
            s->random_walk_max_steps = 10;
        }else{
            s->random_walk_max_steps = (ceil(hinit / avg_op_cost) + .5) * 4;
        }

    }else{
        pddlRandInit(&s->rnd, rand_sampler_seed);
        if (mutex != NULL){
            s->mutex = mutex;
            s->type = STATE_SAMPLER_SYNTACTIC_MUTEX;
        }else{
            s->type = STATE_SAMPLER_SYNTACTIC;
        }
    }
}

static void stateSamplerFree(state_sampler_t *s)
{
    if (s->type == STATE_SAMPLER_RANDOM_WALK)
        pddlRandomWalkFree(&s->random_walk);
    if (s->state != NULL)
        FREE(s->state);
}

static void stateSamplerSample(state_sampler_t *s, pddl_err_t *err)
{
    if (s->type == STATE_SAMPLER_SYNTACTIC){
        for (int var = 0; var < s->fdr->var.var_size; ++var){
            int val = pddlRand(&s->rnd, 0, s->fdr->var.var[var].val_size);
            val = PDDL_MIN(val, s->fdr->var.var[var].val_size - 1);
            s->state[var] = val;
        }

    }else if (s->type == STATE_SAMPLER_SYNTACTIC_MUTEX){
        ASSERT(s->mutex != NULL);
        PDDL_ISET(state);
        unsigned long count = 0UL;
        do {
            pddlISetEmpty(&state);
            for (int var = 0; var < s->fdr->var.var_size; ++var){
                int val = pddlRand(&s->rnd, 0, s->fdr->var.var[var].val_size);
                val = PDDL_MIN(val, s->fdr->var.var[var].val_size - 1);
                s->state[var] = val;
                pddlISetAdd(&state, s->fdr->var.var[var].val[val].global_id);
            }
            if (++count % 100000UL == 0UL)
                PDDL_INFO(err, "tried %lu random states", count);
        } while (pddlMutexPairsIsMutexSet(s->mutex, &state));
        pddlISetFree(&state);

    }else if (s->type == STATE_SAMPLER_RANDOM_WALK){
        pddlRandomWalkSampleState(&s->random_walk,
                                  s->fdr->init,
                                  s->random_walk_max_steps,
                                  s->state);
    }
}


static double countStatesMutex(const pddl_mgroups_t *mgs,
                               const pddl_mutex_pairs_t *mutex,
                               const pddl_iset_t *fixed)
{
    if (pddlMutexPairsIsMutexSet(mutex, fixed))
        return 0.;

    if (fixed == NULL || pddlISetSize(fixed) == 0){
        double num = pddlISetSize(&mgs->mgroup[0].mgroup);
        for (int i = 1; i < mgs->mgroup_size; ++i)
            num *= pddlISetSize(&mgs->mgroup[i].mgroup);
        return num;
    }

    double num = 1.;
    for (int mgi = 0; mgi < mgs->mgroup_size; ++mgi){
        int mg_size = 0;
        int fact;
        PDDL_ISET_FOR_EACH(&mgs->mgroup[mgi].mgroup, fact){
            if (!pddlMutexPairsIsMutexFactSet(mutex, fact, fixed))
                mg_size += 1;
        }
        num *= (double)mg_size;
    }
    return num;
}

static void setObjAllStatesMutex1(pddl_pot_t *pot,
                                  const pddl_mgroups_t *mgs,
                                  const pddl_mutex_pairs_t *mutex)
{
    double *coef = CALLOC_ARR(double, pot->var_size);
    PDDL_ISET(fixed);

    for (int mgi = 0; mgi < mgs->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mgs->mgroup + mgi;
        double sum = 0.;
        int fixed_fact;
        PDDL_ISET_FOR_EACH(&mg->mgroup, fixed_fact){
            pddlISetEmpty(&fixed);
            pddlISetAdd(&fixed, fixed_fact);
            coef[fixed_fact] = countStatesMutex(mgs, mutex, &fixed);
            sum += coef[fixed_fact];
        }
        PDDL_ISET_FOR_EACH(&mg->mgroup, fixed_fact){
            coef[fixed_fact] /= sum;
            if (coef[fixed_fact] < 1E-6)
                coef[fixed_fact] = 0.;
        }
    }

    pddlPotSetObj(pot, coef);

    pddlISetFree(&fixed);
    if (coef != NULL)
        FREE(coef);
}

static void setObjAllStatesMutex2(pddl_pot_t *pot,
                                  const pddl_mgroups_t *mgs,
                                  int fact_size,
                                  const pddl_mutex_pairs_t *mutex)
{
    double *coef = CALLOC_ARR(double, pot->var_size);
    PDDL_ISET(fixed);

    for (int mgi = 0; mgi < mgs->mgroup_size; ++mgi){
        const pddl_mgroup_t *mg = mgs->mgroup + mgi;
        double sum = 0.;
        int fixed_fact;
        PDDL_ISET_FOR_EACH(&mg->mgroup, fixed_fact){
            coef[fixed_fact] = 0.;
            for (int f = 0; f < fact_size; ++f){
                if (f == fixed_fact)
                    continue;

                pddlISetEmpty(&fixed);
                pddlISetAdd(&fixed, fixed_fact);
                pddlISetAdd(&fixed, f);
                ASSERT(pddlISetSize(&fixed) == 2);
                coef[fixed_fact] += countStatesMutex(mgs, mutex, &fixed);
            }
            sum += coef[fixed_fact];
        }
        PDDL_ISET_FOR_EACH(&mg->mgroup, fixed_fact){
            coef[fixed_fact] /= sum;
            if (coef[fixed_fact] < 1E-6)
                coef[fixed_fact] = 0.;
        }
    }

    pddlPotSetObj(pot, coef);

    pddlISetFree(&fixed);
    if (coef != NULL)
        FREE(coef);
}


static void setObjAllStatesMutex(pddl_pot_t *pot,
                                 const pddl_mg_strips_t *s,
                                 const pddl_mutex_pairs_t *mutex,
                                 int mutex_size)
{
    if (mutex_size == 1){
        setObjAllStatesMutex1(pot, &s->mg, mutex);
    }else if (mutex_size == 2){
        setObjAllStatesMutex2(pot, &s->mg, s->strips.fact.fact_size, mutex);
    }else{
        ASSERT_RUNTIME_M(0, "mutex-size >= 3 is not supported!");
    }
}

static void setObjAllStatesMutexConditioned(pddl_pot_t *pot,
                                            const pddl_iset_t *cond,
                                            const pddl_mg_strips_t *s,
                                            const pddl_mutex_pairs_t *mutex,
                                            int mutex_size)
{
    pddl_mgroups_t mgs;
    pddlMGroupsInitEmpty(&mgs);
    PDDL_ISET(mg);
    for (int mgi = 0; mgi < s->mg.mgroup_size; ++mgi){
        int fact_id;
        pddlISetEmpty(&mg);
        PDDL_ISET_FOR_EACH(&s->mg.mgroup[mgi].mgroup, fact_id){
            if (!pddlMutexPairsIsMutexFactSet(mutex, fact_id, cond))
                pddlISetAdd(&mg, fact_id);
        }
        pddlMGroupsAdd(&mgs, &mg);
        ASSERT_RUNTIME(pddlISetSize(&mg) > 0);
    }
    pddlISetFree(&mg);

    if (mutex_size == 1){
        setObjAllStatesMutex1(pot, &mgs, mutex);
    }else if (mutex_size == 2){
        setObjAllStatesMutex2(pot, &mgs, s->strips.fact.fact_size, mutex);
    }else{
        ASSERT_RUNTIME_M(0, "mutex-size >= 3 is not supported!");
    }
    pddlMGroupsFree(&mgs);
}

static int allStatesMutexCond(pddl_pot_solutions_t *sols,
                              pddl_pot_t *pot,
                              const pddl_mg_strips_t *mg_strips,
                              const pddl_mutex_pairs_t *mutex,
                              const pddl_hpot_old_config_t *cfg,
                              const pddl_iset_t *facts,
                              pddl_err_t *err)
{
    int mutex_size = cfg->all_states_mutex_size;
    PDDL_ISET(cond);
    int fact_id;
    int count = 0;
    PDDL_ISET_FOR_EACH(facts, fact_id){
        pddlISetEmpty(&cond);
        pddlISetAdd(&cond, fact_id);
        setObjAllStatesMutexConditioned(pot, &cond, mg_strips, mutex,
                                        mutex_size);
        solveAndAdd(pot, sols, cfg, err);
        if (++count % 10 == 0){
            PDDL_INFO(err, "Computed conditioned func %d/%d and generated %d"
                      " potential functions",
                      count, pddlISetSize(facts), sols->sol_size);
        }
    }
    PDDL_INFO(err, "Computed conditioned func %d/%d and generated %d"
              " potential functions",
              count, pddlISetSize(facts), sols->sol_size);
    pddlISetFree(&cond);

    if (sols->sol_size > 0)
        return 0;
    return -1;
}

static int allStatesMutexCond2(pddl_pot_solutions_t *sols,
                               pddl_pot_t *pot,
                               const pddl_mg_strips_t *mg_strips,
                               const pddl_mutex_pairs_t *mutex,
                               const pddl_hpot_old_config_t *cfg,
                               int num_samples,
                               pddl_err_t *err)
{
    int mutex_size = cfg->all_states_mutex_size;
    pddl_rand_t rnd;
    pddlRandInit(&rnd, rand_sampler_seed);
    PDDL_ISET(cond);
    int count = 0;
    int fact_size = mg_strips->strips.fact.fact_size;
    for (int i = 0; i < num_samples; ++i){
        int f1 = pddlRand(&rnd, 0, fact_size);
        int f2 = pddlRand(&rnd, 0, fact_size);
        if (pddlMutexPairsIsMutex(mutex, f1, f2))
            continue;
        pddlISetEmpty(&cond);
        pddlISetAdd(&cond, f1);
        pddlISetAdd(&cond, f2);

        setObjAllStatesMutexConditioned(pot, &cond, mg_strips, mutex,
                                        mutex_size);
        solveAndAdd(pot, sols, cfg, err);
        if (++count % 10 == 0){
            PDDL_INFO(err, "Computed conditioned func^2 %d and generated %d"
                      " potential functions",
                      count, sols->sol_size);
        }
    }
    PDDL_INFO(err, "Computed conditioned func^2 %d and generated %d"
              " potential functions",
              count, sols->sol_size);
    pddlISetFree(&cond);

    if (sols->sol_size > 0)
        return 0;
    return -1;
}


static int samples(pddl_pot_solutions_t *sols,
                   pddl_pot_t *pot,
                   const pddl_fdr_t *fdr,
                   const pddl_mutex_pairs_t *mutex,
                   const pddl_hpot_old_config_t *cfg,
                   pddl_err_t *err)
{
    PDDL_INFO(err, "generating %d samples (mutex: %d, random-walk: %d)...",
              cfg->num_samples,
              (mutex != NULL),
              cfg->samples_random_walk);

    state_sampler_t sampler;
    stateSamplerInit(&sampler, cfg, fdr, mutex, pot, err);

    int num_states = 0;
    double *coef = CALLOC_ARR(double, pot->var_size);
    for (int si = 0; si < cfg->num_samples; ++si){
        stateSamplerSample(&sampler, err);
        if (cfg->obj == PDDL_HPOT_OBJ_SAMPLES_MAX){
            bzero(coef, sizeof(double) * pot->var_size);
            for (int var = 0; var < fdr->var.var_size; ++var)
                coef[fdr->var.var[var].val[sampler.state[var]].global_id] = 1.;

            pddlPotSetObj(pot, coef);
            // Dead-ends are simply skipped
            pddl_pot_solution_t sol;
            pddlPotSolutionInit(&sol);
            if (pddlPotSolve(pot, &sol, err) == 0){
                int h;
                h = pddlPotSolutionEvalFDRState(&sol, &fdr->var, sampler.state);
                if (h != PDDL_COST_DEAD_END){
                    pddlPotSolutionsAdd(sols, &sol);
                    ++num_states;
                    if ((si + 1) % 100 == 0){
                        PDDL_INFO(err, "Solved for state: %d/%d",
                                  num_states, cfg->num_samples);
                    }
                }
            }
            pddlPotSolutionFree(&sol);

        }else{
            for (int var = 0; var < fdr->var.var_size; ++var)
                coef[fdr->var.var[var].val[sampler.state[var]].global_id] += 1.;
            ++num_states;
        }
    }

    int ret = 0;
    if (num_states > 0 && cfg->obj == PDDL_HPOT_OBJ_SAMPLES_SUM){
        pddlPotSetObj(pot, coef);
        if (solveAndAdd(pot, sols, cfg, err) == 0){
            PDDL_INFO(err, "Solved for a sum of %d/%d states",
                      num_states, cfg->num_samples);
        }else{
            PDDL_INFO(err, "No solution for sum of %d/%d states",
                      num_states, cfg->num_samples);
            ret = -1;
        }
    }

    if (coef != NULL)
        FREE(coef);
    stateSamplerFree(&sampler);

    if (num_states == 0){
        PDDL_INFO2(err, "No viable samples found.");
        return -1;
    }
    return ret;
}

static void setStateToFDRState(const pddl_iset_t *state,
                               int *fdr_state,
                               const pddl_fdr_t *fdr)
{
    int fact_id;
    PDDL_ISET_FOR_EACH(state, fact_id){
        const pddl_fdr_val_t *v = fdr->var.global_id_to_val[fact_id];
        fdr_state[v->var_id] = v->val_id;
    }
}

struct diverse_pot {
    double *coef;
    pddl_pot_solutions_t func;
    pddl_pot_solution_t avg_func;
    int *state_est;
    pddl_set_iset_t states;
    int active_states;
    pddl_rand_t rnd;
};
typedef struct diverse_pot diverse_pot_t;

static void diverseInit(diverse_pot_t *div,
                        const pddl_pot_t *pot,
                        const pddl_fdr_t *fdr,
                        int num_samples)
{
    div->coef = ALLOC_ARR(double, pot->var_size);
    pddlPotSolutionsInit(&div->func);
    pddlPotSolutionInit(&div->avg_func);
    div->state_est = CALLOC_ARR(int, num_samples);
    pddlSetISetInit(&div->states);
    div->active_states = 0;
    pddlRandInit(&div->rnd, rand_diverse_seed);
}

static void diverseFree(diverse_pot_t *div,
                        const pddl_pot_t *pot,
                        const pddl_fdr_t *fdr,
                        int num_samples)
{
    FREE(div->coef);
    pddlPotSolutionsFree(&div->func);
    pddlPotSolutionFree(&div->avg_func);
    FREE(div->state_est);
    pddlSetISetFree(&div->states);
}

static void diverseGenStates(diverse_pot_t *div,
                             pddl_pot_t *pot,
                             const pddl_fdr_t *fdr,
                             const pddl_hpot_old_config_t *_cfg,
                             pddl_err_t *err)
{
    pddl_hpot_old_config_t cfg = *_cfg;
    // force random walk
    cfg.samples_random_walk = 1;
    ASSERT_RUNTIME(cfg.num_samples > 0);

    PDDL_INFO(err, "generating %d samples with random walk and"
              " computing potentials...", cfg.num_samples);
    state_sampler_t sampler;
    stateSamplerInit(&sampler, &cfg, fdr, NULL, pot, err);

    // Samples states, filter out dead-ends and compute estimate for each
    // state
    int num_states = 0;
    int num_dead_ends = 0;
    int num_duplicates = 0;
    PDDL_ISET(state);
    for (int si = 0; si < cfg.num_samples; ++si){
        stateSamplerSample(&sampler, err);

        pddlISetEmpty(&state);
        bzero(div->coef, sizeof(double) * pot->var_size);
        for (int var = 0; var < fdr->var.var_size; ++var){
            int id = fdr->var.var[var].val[sampler.state[var]].global_id;
            div->coef[id] = 1.;
            pddlISetAdd(&state, id);
        }

        if (pddlSetISetFind(&div->states, &state) >= 0){
            // Ignore duplicates
            ++num_duplicates;
            continue;
        }

        // Compute heuristic estimate
        pddlPotSetObj(pot, div->coef);
        pddl_pot_solution_t sol;
        pddlPotSolutionInit(&sol);
        if (pddlPotSolve(pot, &sol, err) == 0){
            int h = pddlPotSolutionEvalFDRState(&sol, &fdr->var, sampler.state);
            if (h != PDDL_COST_DEAD_END){
                // Add state to the set of states and store heuristic estimate
                int state_id = pddlSetISetAdd(&div->states, &state);
                ASSERT(state_id == num_states);
                div->state_est[state_id] = h;
                ASSERT_RUNTIME(div->state_est[state_id] >= 0);
                pddlPotSolutionsAdd(&div->func, &sol);
                ++num_states;

                if ((si + 1) % 100 == 0){
                    PDDL_INFO(err, "Diverse: %d/%d (dead-ends: %d)",
                              num_states, cfg.num_samples, num_dead_ends);
                }

            }else{
                ++num_dead_ends;
            }
        }else{
            // Dead-ends are simply skipped
            ++num_dead_ends;
        }
        pddlPotSolutionFree(&sol);
    }
    PDDL_INFO(err, "Detected dead-ends: %d", num_dead_ends);
    PDDL_INFO(err, "Detected duplicates: %d", num_duplicates);
    ASSERT(num_states == pddlSetISetSize(&div->states));
    div->active_states = pddlSetISetSize(&div->states);
    pddlISetFree(&state);
    stateSamplerFree(&sampler);
}



static int diverseAvg(diverse_pot_t *div,
                      pddl_pot_t *pot,
                      pddl_err_t *err)
{
    bzero(div->coef, sizeof(double) * pot->var_size);
    const pddl_iset_t *state;
    PDDL_SET_ISET_FOR_EACH_ID_SET(&div->states, i, state){
        if (div->state_est[i] < 0)
            continue;
        int fact_id;
        PDDL_ISET_FOR_EACH(state, fact_id)
            div->coef[fact_id] += 1.;
    }
    pddlPotSetObj(pot, div->coef);
    return pddlPotSolve(pot, &div->avg_func, err);
}

static const pddl_pot_solution_t *diverseSelectFunc(diverse_pot_t *div,
                                                    pddl_pot_t *pot,
                                                    const pddl_fdr_t *fdr,
                                                    pddl_err_t *err)
{
    if (diverseAvg(div, pot, err) != 0)
        return NULL;

    int *fdr_state = ALLOC_ARR(int, fdr->var.var_size);
    const pddl_iset_t *state;
    PDDL_SET_ISET_FOR_EACH_ID_SET(&div->states, si, state){
        if (div->state_est[si] < 0)
            continue;
        setStateToFDRState(state, fdr_state, fdr);

        int hest;
        hest = pddlPotSolutionEvalFDRState(&div->avg_func, &fdr->var, fdr_state);
        if (hest == div->state_est[si]){
            FREE(fdr_state);
            return &div->avg_func;
        }
    }

    int sid = pddlRand(&div->rnd, 0, div->active_states);
    PDDL_SET_ISET_FOR_EACH_ID(&div->states, si){
        if (div->state_est[si] < 0)
            continue;
        if (sid-- == 0){
            FREE(fdr_state);
            return div->func.sol + si;
        }
    }
    ASSERT_RUNTIME_M(0, "The number of active states is invalid!");
    return NULL;
}

static void diverseFilterOutStates(diverse_pot_t *div,
                                   const pddl_fdr_t *fdr,
                                   const pddl_pot_solution_t *func,
                                   pddl_err_t *err)
{
    int *fdr_state = ALLOC_ARR(int, fdr->var.var_size);
    const pddl_iset_t *state;
    PDDL_SET_ISET_FOR_EACH_ID_SET(&div->states, si, state){
        if (div->state_est[si] < 0)
            continue;
        setStateToFDRState(state, fdr_state, fdr);
        int hest = pddlPotSolutionEvalFDRState(func, &fdr->var, fdr_state);
        if (hest >= div->state_est[si]){
            div->state_est[si] = -1;
            --div->active_states;
        }
    }
    FREE(fdr_state);
}

static int diverse(pddl_pot_solutions_t *sols,
                   pddl_pot_t *pot,
                   const pddl_fdr_t *fdr,
                   const pddl_hpot_old_config_t *cfg,
                   pddl_err_t *err)
{
    PDDL_INFO(err, "Diverse potentials with %d samples", cfg->num_samples);
    ASSERT_RUNTIME(cfg->num_samples > 0);
    diverse_pot_t div;
    diverseInit(&div, pot, fdr, cfg->num_samples);
    diverseGenStates(&div, pot, fdr, cfg, err);
    while (div.active_states > 0){
        const pddl_pot_solution_t *func = diverseSelectFunc(&div, pot, fdr, err);
        if (func == NULL)
            return -1;
        pddlPotSolutionsAdd(sols, func);
        diverseFilterOutStates(&div, fdr, func, err);
    }
    diverseFree(&div, pot, fdr, cfg->num_samples);
    PDDL_INFO(err, "Computed diverse potentials with %d functions",
              sols->sol_size);
    return 0;
}

int pddlHPotOld(pddl_pot_solutions_t *sols,
                const pddl_fdr_t *fdr,
                const pddl_hpot_old_config_t *cfg,
                pddl_err_t *err)
{
    pddlPotSolutionsInit(sols);

    int ret = 0;
    if (fdr->has_cond_eff){
        PDDL_INFO2(err, "Conditional effects are not supported");
        return -1;
    }

    CTX(err, "pot", "Pot");
    CTX_NO_TIME(err, "cfg", "Cfg");
    pddlHPotOldConfigLog(cfg, err);
    CTXEND(err);
    // Construct MG-Strips and compute h^2 mutexes if necessary
    pddl_mg_strips_t mg_strips;
    pddl_mutex_pairs_t mutex;
    int need_mutex = 0;
    if (cfg->disambiguation
            || cfg->weak_disambiguation
            || cfg->samples_use_mutex
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND
            || cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND2){
        need_mutex = 1;
        pddlMGStripsInitFDR(&mg_strips, fdr);
        pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);
        pddlMutexPairsAddMGroups(&mutex, &mg_strips.mg);
        pddlH2(&mg_strips.strips, &mutex, NULL, NULL, 0., err);
    }

    // Initialize potential heuristic
    pddl_pot_t pot;
    if (initPot(&pot, fdr, &mg_strips, &mutex, cfg, err) != 0){
        if (need_mutex){
            pddlMutexPairsFree(&mutex);
            pddlMGStripsFree(&mg_strips);
        }
        CTXEND(err);
        return -1;
    }

    if (cfg->add_init_constr){
        // Add constraint on the initial state
        if (addInitConstr(&pot, fdr, cfg, err) != 0){
            pddlPotFree(&pot);
            CTXEND(err);
            return -1;
        }
    }

    if (cfg->obj == PDDL_HPOT_OBJ_INIT){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);
        ret = solveAndAdd(&pot, sols, cfg, err);
        PDDL_INFO(err, "Solved for the initial state: %d", ret);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES){
        pddlPotSetObjFDRAllSyntacticStates(&pot, &fdr->var);
        ret = solveAndAdd(&pot, sols, cfg, err);
        PDDL_INFO(err, "Solved for all states: %d", ret);

    }else if (cfg->obj == PDDL_HPOT_OBJ_MAX_INIT_ALL_STATES){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);
        ret = solveAndAdd(&pot, sols, cfg, err);
        PDDL_INFO(err, "Solved for the initial state: %d", ret);

        if (ret == 0){
            pddlPotSetObjFDRAllSyntacticStates(&pot, &fdr->var);
            ret = solveAndAdd(&pot, sols, cfg, err);
            PDDL_INFO(err, "Solved for all states: %d", ret);
        }

    }else if (cfg->obj == PDDL_HPOT_OBJ_SAMPLES_MAX
                || cfg->obj == PDDL_HPOT_OBJ_SAMPLES_SUM){
        const pddl_mutex_pairs_t *m = NULL;
        if (cfg->samples_use_mutex)
            m = &mutex;
        ret = samples(sols, &pot, fdr, m, cfg, err);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX){
        setObjAllStatesMutex(&pot, &mg_strips, &mutex,
                             cfg->all_states_mutex_size);
        if (cfg->all_states_mutex_size < 1 || cfg->all_states_mutex_size > 2){
            PDDL_FATAL("all-states-mutex with size %d unsupported!",
                       cfg->all_states_mutex_size);
        }
        ret = solveAndAdd(&pot, sols, cfg, err);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED){
        PDDL_ISET(facts);
        for (int f = 0; f < mg_strips.strips.fact.fact_size; ++f)
            pddlISetAdd(&facts, f);
        ret = allStatesMutexCond(sols, &pot, &mg_strips, &mutex, cfg,
                                 &facts, err);
        pddlISetFree(&facts);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND){
        pddl_rand_t rnd;
        pddlRandInit(&rnd, rand_sampler_seed);
        PDDL_ISET(facts);
        int fact_size = mg_strips.strips.fact.fact_size;
        for (int i = 0; i < cfg->num_samples; ++i)
            pddlISetAdd(&facts, pddlRand(&rnd, 0, fact_size));

        ret = allStatesMutexCond(sols, &pot, &mg_strips, &mutex, cfg,
                                 &facts, err);
        pddlISetFree(&facts);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND2){
        ret = allStatesMutexCond2(sols, &pot, &mg_strips, &mutex, cfg,
                                  cfg->num_samples, err);

    }else if (cfg->obj == PDDL_HPOT_OBJ_DIVERSE){
        ret = diverse(sols, &pot, fdr, cfg, err);

    }else{
        if (need_mutex){
            pddlMutexPairsFree(&mutex);
            pddlMGStripsFree(&mg_strips);
        }
        pddlPotFree(&pot);
        PDDL_ERR_RET(err, -1, "Unkown objective function for potential"
                     " heuristic: %d", cfg->obj);
    }

    if (need_mutex){
        pddlMutexPairsFree(&mutex);
        pddlMGStripsFree(&mg_strips);
    }
    pddlPotFree(&pot);

    if (ret != 0)
        PDDL_INFO2(err, "No optimal solution found");

    CTXEND(err);
    return ret;
}

struct pddl_heur_pot {
    pddl_heur_t heur;
    pddl_pot_solutions_t sols;
    const pddl_fdr_vars_t *vars;
};
typedef struct pddl_heur_pot pddl_heur_pot_t;

static void heurDel(pddl_heur_t *_h)
{
    pddl_heur_pot_t *h = pddl_container_of(_h, pddl_heur_pot_t, heur);
    _pddlHeurFree(&h->heur);
    pddlPotSolutionsFree(&h->sols);
    FREE(h);
}

static int heurEstimate(pddl_heur_t *_h,
                        const pddl_fdr_state_space_node_t *node,
                        const pddl_fdr_state_space_t *state_space)
{
    pddl_heur_pot_t *h = pddl_container_of(_h, pddl_heur_pot_t, heur);
    int est = pddlPotSolutionsEvalMaxFDRState(&h->sols, h->vars, node->state);
    return est;
}

pddl_heur_t *pddlHeurPotOld(const pddl_fdr_t *fdr,
                            const pddl_hpot_old_config_t *cfg,
                            pddl_err_t *err)
{
    pddl_heur_pot_t *h = ALLOC(pddl_heur_pot_t);
    pddlPotSolutionsInit(&h->sols);
    pddlHPotOld(&h->sols, fdr, cfg, err);
    h->vars = &fdr->var;
    _pddlHeurInit(&h->heur, heurDel, heurEstimate);
    return &h->heur;
}
