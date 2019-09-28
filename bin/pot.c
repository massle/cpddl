#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

#define ROUND_EPS 0.001

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

static int potFDR(const pddl_strips_t *strips,
                  const pddl_mgroups_t *mgroups,
                  const pddl_mutex_pairs_t *mutex,
                  bor_err_t *err)
{
    unsigned fdr_var_flag = PDDL_FDR_VARS_LARGEST_FIRST;
    pddl_fdr_t fdr;
    pddlFDRInitFromStrips(&fdr, strips, mgroups, mutex, fdr_var_flag, err);

    pddl_pot_t pot;
    pddlPotInitFDR(&pot, &fdr);

    double *w = BOR_ALLOC_ARR(double, pot.var_size);
    pddlPotSetObjFDRState(&pot, &fdr.var, fdr.init);
    if (pddlPotSolve(&pot, w, pot.var_size, 0) != 0){
        fprintf(stderr, "Error: Pot failed\n");
        return -1;
    }
    int fdr_init_state = potFDRState(&pot, &fdr, fdr.init, w);
    fprintf(stdout, "FDR Init state: %d\n", fdr_init_state);

    int fdr_all_synt_states = potFDRState(&pot, &fdr, fdr.init, w);
    fprintf(stdout, "FDR All syntactic states: %d\n", fdr_all_synt_states);
    pddlPotFree(&pot);
    if (w != NULL)
        BOR_FREE(w);
    pddlFDRFree(&fdr);
    return 0;
}
static int potMGStrips(const pddl_strips_t *strips,
                       const pddl_mgroups_t *mgroups,
                       const pddl_mutex_pairs_t *mutex_in,
                       bor_err_t *err)
{
    pddl_mg_strips_t mg_strips;
    pddlMGStripsInit(&mg_strips, strips, mgroups);

    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);
    pddlMutexPairsAddMGroups(&mutex, &mg_strips.mg);
    if (pddlH2(&mg_strips.strips, &mutex, NULL, NULL, err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(err, 1, stderr);
        return -1;
    }

    pddl_pot_t pot;
    if (pddlPotInitMGStrips(&pot, &mg_strips, &mutex) != 0){
        fprintf(stdout, "MG-Strips: Unsolvable\n");
    }else{
        double *w = BOR_ALLOC_ARR(double, pot.var_size);
        pddlPotSetObjStripsState(&pot, &mg_strips.strips.init);
        if (pddlPotSolve(&pot, w, pot.var_size, 0) != 0){
            fprintf(stderr, "Error: Pot failed\n");
            return -1;
        }
        int mgs_init_state = potStripsState(&pot, &mg_strips.strips.init, w);
        fprintf(stdout, "MG-Strips Init state: %d\n", mgs_init_state);
        if (w != NULL)
            BOR_FREE(w);
    }
    pddlPotFree(&pot);
    pddlMGStripsFree(&mg_strips);
    pddlMutexPairsFree(&mutex);
    return 0;
}

int main(int argc, char *argv[])
{
    bor_err_t err = BOR_ERR_INIT;
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);

    // Determine pddl files
    pddl_files_t files;
    if (argc == 2){
        BOR_INFO(&err, "Input file: '%s'", argv[1]);
        if (pddlFiles1(&files, argv[1], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }else if (argc == 3){
        BOR_INFO(&err, "Input files: '%s' and '%s'", argv[1], argv[2]);
        if (pddlFiles(&files, argv[1], argv[2], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }else{
        fprintf(stderr, "Usage: %s pddl-file(s)\n", argv[0]);
        return -1;
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

    // TODO: fam-groups

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

    // Find h^2 mutexes
    pddlMutexPairsFree(&mutex);
    pddlMutexPairsInitStrips(&mutex, &strips);
    if (pddlH2(&strips, &mutex, NULL, NULL, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }

    if (potFDR(&strips, &mgroups, &mutex, &err) != 0)
        return -1;
    if (potMGStrips(&strips, &mgroups, &mutex, &err) != 0)
        return -1;

    // Find fam-groups
    pddl_famgroup_config_t fam_cfg = PDDL_FAMGROUP_CONFIG_INIT;
    if (pddlFAMGroupsInfer(&mgroups, &strips, &fam_cfg, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }

    fprintf(stdout, "With fam-groups:\n");
    if (potFDR(&strips, &mgroups, &mutex, &err) != 0)
        return -1;
    if (potMGStrips(&strips, &mgroups, &mutex, &err) != 0)
        return -1;


    optsClear();
    pddlMutexPairsFree(&mutex);
    pddlMGroupsFree(&mgroups);
    pddlStripsFree(&strips);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    pddlFree(&pddl);
    return 0;
}



