#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

#define ROUND_EPS 0.001

#define POT_INIT_STATE 1
#define POT_ALL_SYNT_STATES 2
#define POT_ALL_SYNT_STATES_PLUS_INIT 3
#define POT_ALL_SYNT_STATES_PLUS_HALF_INIT 4

static int roundOff(double z)
{
    return ceil(z - ROUND_EPS);
}

static int potFDRState(const pddl_pot_t *pot,
                       const pddl_fdr_t *fdr,
                       const int *state,
                       const double *w)
{
    double p = 0;
    for (int var = 0; var < fdr->var.var_size; ++var)
        p += w[pot->fdr_var_offset[var] + state[var]];
    if (p < 0.)
        return 0;
    return roundOff(p);
}

static int potStripsState(const pddl_pot_t *pot,
                          const bor_iset_t *state,
                          const double *w)
{
    double p = 0.;
    int fact_id;
    BOR_ISET_FOR_EACH(state, fact_id)
        p += w[fact_id];
    if (p <= 0.)
        return 0;
    return roundOff(p);
}

static int potFDR(const pddl_fdr_t *fdr,
                  int pot_type,
                  bor_err_t *err)
{
    pddl_pot_t pot;
    pddlPotInitFDR(&pot, fdr);

    if (pot_type == POT_INIT_STATE){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);

    }else if (pot_type == POT_ALL_SYNT_STATES){
        pddlPotSetObjFDRAllSyntacticStates(&pot, &fdr->var);

    }else if (pot_type == POT_ALL_SYNT_STATES_PLUS_INIT
                || pot_type == POT_ALL_SYNT_STATES_PLUS_HALF_INIT){
        pddlPotSetObjFDRState(&pot, &fdr->var, fdr->init);
        double *w = BOR_ALLOC_ARR(double, pot.var_size);
        if (pddlPotSolve(&pot, w, pot.var_size, 0) != 0)
            BOR_ERR_RET2(err, -1, "Pot failed");
        int est = potFDRState(&pot, fdr, fdr->init, w);
        if (w != NULL)
            BOR_FREE(w);

        pddlPotSetObjFDRAllSyntacticStates(&pot, &fdr->var);

        BOR_ISET(vars);
        for (int var = 0; var < fdr->var.var_size; ++var){
            int v = fdr->var.var[var].val[fdr->init[var]].global_id;
            borISetAdd(&vars, v);
        }
        double rhs = est;
        if (pot_type == POT_ALL_SYNT_STATES_PLUS_HALF_INIT)
            rhs = rhs / 2.;
        pddlPotSetLowerBoundConstr(&pot, &vars, rhs);
        borISetFree(&vars);

    }else{
        BOR_ERR_RET(err, -1, "Unkown pot-type %d!", pot_type);
    }

    double *w = BOR_ALLOC_ARR(double, pot.var_size);
    if (pddlPotSolve(&pot, w, pot.var_size, 0) != 0)
        BOR_ERR_RET2(err, -1, "Pot failed");

    fprintf(stdout, "1\n");
    fprintf(stdout, "begin_potentials\n");
    for (int fi = 0; fi < fdr->var.global_id_size; ++fi){
        const pddl_fdr_val_t *fval = fdr->var.global_id_to_val[fi];
        fprintf(stdout, "%d %d %.20f\n",
                fval->var_id, fval->val_id, w[fi]);
    }
    fprintf(stdout, "end_potentials\n");

    int pot_init_state = potFDRState(&pot, fdr, fdr->init, w);
    BOR_INFO(err, "Init state estimate: %d", pot_init_state);
    pddlPotFree(&pot);
    if (w != NULL)
        BOR_FREE(w);
    return 0;
}

static int potMGStrips(const pddl_fdr_t *fdr,
                       const pddl_mg_strips_t *mg_strips,
                       const pddl_mutex_pairs_t *mutex,
                       int pot_type,
                       bor_err_t *err)
{
    int ret = 0;

    pddl_pot_t pot;
    if (pddlPotInitMGStrips(&pot, mg_strips, mutex) == 0){
        if (pot_type == POT_INIT_STATE){
            pddlPotSetObjStripsState(&pot, &mg_strips->strips.init);
        }else{
            BOR_ERR_RET(err, -1, "Unkown pot-type %d!", pot_type);
        }

        double *w = BOR_ALLOC_ARR(double, pot.var_size);
        if (pddlPotSolve(&pot, w, pot.var_size, 0) != 0)
            BOR_ERR_RET2(err, -1, "Pot failed");

        fprintf(stdout, "1\n");
        fprintf(stdout, "begin_potentials\n");
        for (int fi = 0; fi < mg_strips->strips.fact.fact_size; ++fi){
            const pddl_fdr_val_t *fval = fdr->var.global_id_to_val[fi];
            fprintf(stdout, "%d %d %.20f\n",
                    fval->var_id, fval->val_id, w[fi]);
        }
        fprintf(stdout, "end_potentials\n");
        int pot_init_state = potStripsState(&pot, &mg_strips->strips.init, w);
        BOR_INFO(err, "Init state estimate: %d", pot_init_state);
        if (w != NULL)
            BOR_FREE(w);

    }else{
        fprintf(stderr, "MG-Strips: Unsolvable\n");
        ret = -1;
    }
    pddlPotFree(&pot);
    return ret;
}

int main(int argc, char *argv[])
{
    int pot_type = -1;
    int pot_fdr = 0;

    bor_err_t err = BOR_ERR_INIT;
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);

    if (argc < 3 || argc > 4){
        fprintf(stderr, "Usage: %s pot-type pddl-file(s)\n", argv[0]);
        return -1;
    }

    // Determine type of potential heuristic
    BOR_INFO(&err, "Pot type: '%s'", argv[1]);
    if (strcmp(argv[1], "fdr-init-state") == 0){
        pot_fdr = 1;
        pot_type = POT_INIT_STATE;

    }else if (strcmp(argv[1], "fdr-all-synt-states") == 0){
        pot_fdr = 1;
        pot_type = POT_ALL_SYNT_STATES;

    }else if (strcmp(argv[1], "fdr-all-synt-states-plus-init") == 0){
        pot_fdr = 1;
        pot_type = POT_ALL_SYNT_STATES_PLUS_INIT;

    }else if (strcmp(argv[1], "fdr-all-synt-states-plus-half-init") == 0){
        pot_fdr = 1;
        pot_type = POT_ALL_SYNT_STATES_PLUS_HALF_INIT;

    }else if (strcmp(argv[1], "init-state") == 0){
        pot_type = POT_INIT_STATE;

    }else{
        fprintf(stderr, "Error: Unkown type '%s'\n", argv[1]);
        return -1;
    }

    // Determine pddl files
    pddl_files_t files;
    if (argc == 3){
        BOR_INFO(&err, "Input file: '%s'", argv[2]);
        if (pddlFiles1(&files, argv[2], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }else if (argc == 4){
        BOR_INFO(&err, "Input files: '%s' and '%s'", argv[2], argv[3]);
        if (pddlFiles(&files, argv[2], argv[3], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }
    BOR_INFO(&err, "PDDL files: '%s' '%s'\n",
             files.domain_pddl, files.problem_pddl);

    // Parse PDDL
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = 1;
    pddl_t pddl;
    if (pddlInit(&pddl, files.domain_pddl, files.problem_pddl,
                 &pddl_cfg, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }
    pddlNormalize(&pddl);
    pddlCheckSizeTypes(&pddl);

    // Lifted mgroups
    pddl_lifted_mgroups_infer_limits_t lifted_mgroups_limits
                = PDDL_LIFTED_MGROUPS_INFER_LIMITS_INIT;
    pddl_lifted_mgroups_t lifted_mgroups;
    pddlLiftedMGroupsInit(&lifted_mgroups);
    pddlLiftedMGroupsInferFAMGroups(&pddl, &lifted_mgroups_limits,
                                    &lifted_mgroups, &err);
    pddlLiftedMGroupsSetExactlyOne(&pddl, &lifted_mgroups, &err);
    pddlLiftedMGroupsSetStatic(&pddl, &lifted_mgroups, &err);

    // Ground to STRIPS
    pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;
    ground_cfg.lifted_mgroups = &lifted_mgroups;
    ground_cfg.prune_op_pre_mutex = 1;
    ground_cfg.prune_op_dead_end = 1;
    pddl_strips_t strips;
    if (pddlStripsGround(&strips, &pddl, &ground_cfg, &err) != 0){
        BOR_INFO2(&err, "Grounding failed.");
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }
    if (strips.has_cond_eff)
        pddlStripsCompileAwayCondEff(&strips);

    // Ground mutex groups
    pddl_mgroups_t mgroups;
    pddlMGroupsGround(&mgroups, &pddl, &lifted_mgroups, &strips);
    pddlMGroupsSetExactlyOne(&mgroups, &strips);
    pddlMGroupsSetGoal(&mgroups, &strips);

    /* TODO: fam-groups
    // Find fam-groups
    pddl_famgroup_config_t fam_cfg = PDDL_FAMGROUP_CONFIG_INIT;
    if (pddlFAMGroupsInfer(&mgroups, &strips, &fam_cfg, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }
    */

    // Prune strips
    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitStrips(&mutex, &strips);
    pddlMutexPairsAddMGroups(&mutex, &mgroups);

    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    pddlFAMGroupsDeadEndOps(&mgroups, &strips, &rm_op);
    if (pddlH2FwBw(&strips, &mgroups, &mutex, &rm_fact, &rm_op, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }
    BOR_ISET(irr_fact);
    BOR_ISET(irr_op);
    if (pddlIrrelevanceAnalysis(&strips, &irr_fact, &irr_op, NULL, &err) != 0){
        BOR_TRACE_RET(&err, -1);
    }
    borISetUnion(&rm_fact, &irr_fact);
    borISetUnion(&rm_op, &irr_op);
    borISetFree(&irr_fact);
    borISetFree(&irr_op);

    if (borISetSize(&rm_fact) > 0 || borISetSize(&rm_op) > 0){
        pddlStripsReduce(&strips, &rm_fact, &rm_op);
        if (borISetSize(&rm_fact) > 0){
            pddlMutexPairsReduce(&mutex, &rm_fact);

            pddlMGroupsReduce(&mgroups, &rm_fact);
            pddlMGroupsSetExactlyOne(&mgroups, &strips);
            pddlMGroupsSetGoal(&mgroups, &strips);
        }
    }

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    // Construct FDR
    pddl_fdr_t fdr;
    unsigned fdr_var_flag = PDDL_FDR_VARS_LARGEST_FIRST;
    pddlFDRInitFromStrips(&fdr, &strips, &mgroups, &mutex, fdr_var_flag, &err);

    pddl_mg_strips_t mg_strips;
    if (!pot_fdr){
        // Construct mg-strips from FDR
        pddlMGStripsInitFDR(&mg_strips, &fdr);

        // Find h^2 mutexes in mg-strips
        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);
        if (pddlH2(&mg_strips.strips, &mutex, NULL, NULL, &err) != 0){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }
    }

    // Print out FDR in fast-downward format
    pddlFDRPrintFD(&fdr, &mgroups, stdout);

    // Compute and print potentials
    if (pot_fdr){
        if (potFDR(&fdr, pot_type, &err) != 0){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }

    }else{
        if (potMGStrips(&fdr, &mg_strips, &mutex, pot_type, &err) != 0){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }
    }

    if (!pot_fdr)
        pddlMGStripsFree(&mg_strips);

    optsClear();
    pddlFDRFree(&fdr);
    pddlMutexPairsFree(&mutex);
    pddlMGroupsFree(&mgroups);
    pddlStripsFree(&strips);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    pddlFree(&pddl);
    return 0;
}



