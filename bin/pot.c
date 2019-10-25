#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

struct options {
    int help;
    char *fdr_out;
    pddl_files_t files;
} opt;

static void usage(const char *name)
{
    fprintf(stderr, "pddl-pot for computing potential heuristic.\n");
    fprintf(stderr, "Usage: %s [OPTIONS] [domain.pddl] problem.pddl\n", name);
    fprintf(stderr, "  OPTIONS:\n");
    optsPrint(stderr, "    ");
    fprintf(stderr, "\n");
}

static int readOpts(int *argc,
                    char *argv[],
                    pddl_hpot_config_t *pot_cfg,
                    bor_err_t *err)
{
    int disamb = 0;
    int weak_disamb = 0;
    int no_disamb = 0;
    int obj_init = 0;
    int obj_all_states = 0;
    double add_init_constr = -1.;

    optsAddDesc("help", 'h', OPTS_NONE, &opt.help, NULL,
                "Print this help.");
    optsAddDesc("output", 'o', OPTS_STR, &opt.fdr_out, NULL,
                "Output filename (default: stdout)");

    optsAddDesc("disamb", 'd', OPTS_NONE, &disamb, NULL,
                "Enable disambiguation.");
    optsAddDesc("weak-disamb", 'w', OPTS_NONE, &weak_disamb, NULL,
                "Enable weak disambiguation.");
    optsAddDesc("no-disamb", 'n', OPTS_NONE, &no_disamb, NULL,
                "Disable disambiguation.");

    optsAddDesc("init-state", 'I', OPTS_NONE, &obj_init, NULL,
                "Optimize for the initial state");
    optsAddDesc("all-states", 'A', OPTS_NONE, &obj_all_states, NULL,
                "Optimize for all syntactic states");

    optsAddDesc("add-init-constr", 'L', OPTS_DOUBLE, &add_init_constr, NULL,
                "Add lower bound constraint on the initial state.");


    if (opts(argc, argv) != 0 || opt.help || (*argc != 2 && *argc != 3)){
        if (*argc <= 1)
            fprintf(stderr, "Error: Missing input file(s)\n\n");

        if (*argc > 3){
            for (int i = 0; i < *argc; ++i){
                if (argv[i][0] == '-'){
                    fprintf(stderr, "Error: Unrecognized option '%s'\n",
                            argv[i]);
                }
            }
        }

        usage(argv[0]);
        return -1;
    }

    if (disamb + weak_disamb + no_disamb != 1){
        fprintf(stderr, "Error: One of -d/-w/-n must be specified!\n\n");
        usage(argv[0]);
        return -1;
    }else if (disamb){
        pot_cfg->disambiguation = 1;
        pot_cfg->weak_disambiguation = 0;
    }else if (weak_disamb){
        pot_cfg->disambiguation = 0;
        pot_cfg->weak_disambiguation = 1;
    }else{
        pot_cfg->disambiguation = 0;
        pot_cfg->weak_disambiguation = 0;
    }

    if (obj_init + obj_all_states != 1){
        fprintf(stderr, "Error: One of -I/-A must be specified!\n\n");
        usage(argv[0]);
        return -1;

    }else if (obj_init){
        pot_cfg->obj = PDDL_HPOT_OBJ_INIT;

    }else if (obj_all_states){
        pot_cfg->obj = PDDL_HPOT_OBJ_ALL_STATES;
    }

    if (add_init_constr < 0.){
        pot_cfg->add_init_constr = 0;
    }else{
        pot_cfg->add_init_constr = 1;
        pot_cfg->init_constr_coef = add_init_constr;
    }


    if (*argc == 2){
        BOR_INFO(err, "Input file: '%s'", argv[1]);
        if (pddlFiles1(&opt.files, argv[1], err) != 0)
            BOR_TRACE_RET(err, -1);
    }else{ // *argc == 3
        BOR_INFO(err, "Input files: '%s' and '%s'", argv[1], argv[2]);
        if (pddlFiles(&opt.files, argv[1], argv[2], err) != 0)
            BOR_TRACE_RET(err, -1);
    }
    BOR_INFO(err, "PDDL files: '%s' '%s'\n",
             opt.files.domain_pddl, opt.files.problem_pddl);

    return 0;
}

static void printPotentials(const pddl_fdr_t *fdr,
                            const pddl_hpot_t *hpot,
                            FILE *fout)
{
    fprintf(fout, "%d\n", hpot->pot_size);
    for (int pi = 0; pi < hpot->pot_size; ++pi){
        const double *w = hpot->pot[pi];
        fprintf(fout, "begin_potentials\n");
        for (int fi = 0; fi < fdr->var.global_id_size; ++fi){
            const pddl_fdr_val_t *fval = fdr->var.global_id_to_val[fi];
            fprintf(fout, "%d %d %.20f\n",
                    fval->var_id, fval->val_id, w[fi]);
        }
        fprintf(fout, "end_potentials\n");
    }
}

int main(int argc, char *argv[])
{
    pddl_hpot_config_t hpot_cfg = PDDL_HPOT_CONFIG_INIT;

    bor_err_t err = BOR_ERR_INIT;
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);

    if (readOpts(&argc, argv, &hpot_cfg, &err) != 0){
        borErrPrint(&err, 1, stderr);
        return -1;
    }

    // Parse PDDL
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = 1;
    pddl_t pddl;
    if (pddlInit(&pddl, opt.files.domain_pddl, opt.files.problem_pddl,
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

    // Prune strips
    if (!strips.has_cond_eff){
        BOR_ISET(rm_fact);
        BOR_ISET(rm_op);
        if (pddlIrrelevanceAnalysis(&strips, &rm_fact, &rm_op, NULL, &err) != 0){
            BOR_INFO2(&err, "Irrelevance analysis failed.");
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }
        if (borISetSize(&rm_fact) > 0 || borISetSize(&rm_op) > 0){
            pddlStripsReduce(&strips, &rm_fact, &rm_op);
            if (borISetSize(&rm_fact) > 0)
                pddlMGroupsReduce(&mgroups, &rm_fact);
        }
        borISetFree(&rm_fact);
        borISetFree(&rm_op);
    }

    /* TODO: fam-groups
    // Find fam-groups
    pddl_famgroup_config_t fam_cfg = PDDL_FAMGROUP_CONFIG_INIT;
    if (pddlFAMGroupsInfer(&mgroups, &strips, &fam_cfg, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }
    */

    // Construct FDR
    pddl_fdr_t fdr;
    unsigned fdr_var_flag = PDDL_FDR_VARS_LARGEST_FIRST;
    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitStrips(&mutex, &strips);
    pddlMutexPairsAddMGroups(&mutex, &mgroups);
    pddlFDRInitFromStrips(&fdr, &strips, &mgroups, &mutex, fdr_var_flag, &err);
    if (pddlPruneFDR(&fdr, &err) != 0){
        BOR_INFO2(&err, "Pruning failed.");
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }

    BOR_INFO(&err, "Number of operators: %d", fdr.op.op_size);
    BOR_INFO(&err, "Number of variables: %d", fdr.var.var_size);
    BOR_INFO(&err, "Number of facts: %d", fdr.var.global_id_size);

    pddl_hpot_t hpot;

    pddlHPotInit(&hpot, &fdr, &hpot_cfg, &err);
    int est = pddlHPotFDRStateEstimate(&hpot, &fdr.var, fdr.init);
    BOR_INFO(&err, "Init state estimate: %d", est);

    // Print out FDR in fast-downward format and potentials
    FILE *fout = stdout;
    if (opt.fdr_out != NULL){
        fout = fopen(opt.fdr_out, "w");
        if (fout == NULL){
            fprintf(stderr, "Error: Could not open '%s'", opt.fdr_out);
            return -1;
        }
    }
    pddlFDRPrintFD(&fdr, NULL, fout);
    printPotentials(&fdr, &hpot, fout);
    if (fout != stdout)
        fclose(fout);

    pddlHPotFree(&hpot);

    optsClear();
    pddlFDRFree(&fdr);
    pddlMutexPairsFree(&mutex);
    pddlMGroupsFree(&mgroups);
    pddlStripsFree(&strips);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    pddlFree(&pddl);
    return 0;
}



