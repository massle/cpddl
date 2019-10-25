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

static int fdrStateEstimate(const double *pot,
                            const pddl_fdr_vars_t *vars,
                            const int *state)
{
    double p = 0;
    for (int var = 0; var < vars->var_size; ++var)
        p += pot[vars->var[var].val[state[var]].global_id];
    if (p < 0.)
        return 0;
    return roundOff(p);
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

    init(hpot, 1, fdr->var.global_id_size);

    pddl_pot_t pot;
    if (cfg->disambiguation || cfg->weak_disambiguation){
        pddl_mg_strips_t mg_strips;
        pddl_mutex_pairs_t mutex;

        pddlMGStripsInitFDR(&mg_strips, fdr);
        pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);
        pddlH2(&mg_strips.strips, &mutex, NULL, NULL, err);
        if (cfg->weak_disambiguation){
            pddlPotInitMGStripsSingleFactDisamb(&pot, &mg_strips, &mutex);
            BOR_INFO(err, "Pot: Initialized with weak-disambiguation."
                          " vars: %d, op-constr: %d,"
                          " goal-constr: %d, maxpots: %d",
                          pot.var_size,
                          pot.constr_op.size,
                          pot.constr_goal.size,
                          pot.maxpot_size);
        }else{
            pddlPotInitMGStrips(&pot, &mg_strips, &mutex);
            BOR_INFO(err, "Pot: Initialized with disambiguation."
                          " vars: %d, op-constr: %d,"
                          " goal-constr: %d, maxpots: %d",
                          pot.var_size,
                          pot.constr_op.size,
                          pot.constr_goal.size,
                          pot.maxpot_size);
        }

        pddlMutexPairsFree(&mutex);
        pddlMGStripsFree(&mg_strips);
    }else{
        pddlPotInitFDR(&pot, fdr);
        BOR_INFO(err, "Pot: Initialized without disambiguation."
                      " vars: %d, op-constr: %d,"
                      " goal-constr: %d, maxpots: %d",
                      pot.var_size,
                      pot.constr_op.size,
                      pot.constr_goal.size,
                      pot.maxpot_size);
    }

    pddlPotResetLowerBoundConstr(&pot);
    if (cfg->add_init_constr){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);
        ret = solve(hpot, &pot, 0);
        if (ret != 0){
            pddlPotFree(&pot);
            return ret;
        }

        double rhs = pddlHPotFDRStateEstimate(hpot, &fdr->var, fdr->init);
        rhs *= cfg->init_constr_coef;

        BOR_ISET(vars);
        for (int var = 0; var < fdr->var.var_size; ++var){
            int v = fdr->var.var[var].val[fdr->init[var]].global_id;
            borISetAdd(&vars, v);
        }
        BOR_INFO(err, "Pot: adding lower bound constraint with rhs: %.2f", rhs);
        pddlPotSetLowerBoundConstr(&pot, &vars, rhs);
        borISetFree(&vars);
    }

    if (cfg->obj == PDDL_HPOT_OBJ_INIT){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);
        ret = solve(hpot, &pot, 0);

    }else if (cfg->obj == PDDL_HPOT_OBJ_ALL_STATES){
        pddlPotSetObjFDRAllSyntacticStates(&pot, &fdr->var);
        ret = solve(hpot, &pot, 0);

    }else{
        pddlPotFree(&pot);
        BOR_ERR_RET(err, -1, "Unkown objective function for potential"
                             " heuristic: %d", cfg->obj);
    }

    pddlPotFree(&pot);

    return ret;
}

int pddlHPotFDRStateEstimate(pddl_hpot_t *hpot,
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
