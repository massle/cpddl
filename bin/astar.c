#include <signal.h>
#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

volatile sig_atomic_t terminate = 0;
volatile sig_atomic_t search_started = 0;

void sigHandlerTerminate(int signal)
{
    fprintf(stderr, "Received %s signal\n", strsignal(signal));
    fflush(stderr);
    if (search_started){
        terminate = 1;
    }else{
        exit(-1);
    }
}

struct options {
    int help;
    int fd_fam_groups;
    int fam_groups;
    char *out;
    pddl_files_t files;
    char *heur_spec;
} opt;

static void usage(const char *name)
{
    fprintf(stderr, "pddl-astar\n");
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
    opt.heur_spec = NULL;

    optsAddDesc("help", 'h', OPTS_NONE, &opt.help, NULL,
                "Print this help.");
    optsAddDesc("output", 'o', OPTS_STR, &opt.out, NULL,
                "Output filename (default: stdout)");
    optsAddDesc("fam-groups", 'f', OPTS_NONE, &opt.fam_groups, NULL,
                "Use LP to infer all maximal fam-groups.");
    optsAddDesc("fd", 0x0, OPTS_NONE, &opt.fd_fam_groups, NULL,
                "Use Fast-Downward fam-groups.");
    optsAddDesc("heur", 'H', OPTS_STR, &opt.heur_spec, NULL,
                "Specify heuristic.");

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

    if (opt.heur_spec == NULL){
        fprintf(stderr, "Error: Heuristic (--heur/-H) must be specified.\n\n");
        usage(argv[0]);
        exit(-1);
    }
    BOR_INFO(err, "Heuristic: '%s'", opt.heur_spec);

    return 0;
}

static void printSearchStat(const pddl_search_astar_t *astar, bor_err_t *err)
{
    pddl_search_stat_t stat;
    pddlSearchAStarStat(astar, &stat);
    BOR_INFO(err, "Search steps: %lu, expand: %lu, eval: %lu,"
                  " gen: %lu, open: %lu, closed: %lu,"
                  " reopen: %lu, de: %lu, f: %d",
                  stat.steps,
                  stat.expanded,
                  stat.evaluated,
                  stat.generated,
                  stat.open,
                  stat.closed,
                  stat.reopen,
                  stat.dead_end,
                  stat.last_f_value);
}

static pddl_heur_t *createHeur(const pddl_fdr_t *fdr,
                               bor_err_t *err)
{
    if (strcmp(opt.heur_spec, "blind") == 0){
        return pddlHeurBlind();

    }else if (strncmp(opt.heur_spec, "pot-state", 9) == 0){
        return pddlHeurPotState(fdr, err);

    }else if (strncmp(opt.heur_spec, "pot", 3) == 0){
        pddl_hpot_config_t cfg = PDDL_HPOT_CONFIG_INIT;
        return pddlHeurPot(fdr, &cfg, err);

    }else if (strncmp(opt.heur_spec, "lmc", 3) == 0){
        return pddlHeurLMCut(fdr, err);

    }else{
        fprintf(stderr, "Error: Unkown '%s' heuristic\n", opt.heur_spec);
        exit(-1);
    }
}

int main(int argc, char *argv[])
{
    pddl_hpot_config_t hpot_cfg = PDDL_HPOT_CONFIG_INIT;

    signal(SIGINT, sigHandlerTerminate);
    signal(SIGTERM, sigHandlerTerminate);

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
    if (opt.fd_fam_groups){
        pddlLiftedMGroupsInferMonotonicity(&pddl, &lifted_mgroups_limits,
                                           NULL, &lifted_mgroups, &err);
    }else{
        pddlLiftedMGroupsInferFAMGroups(&pddl, &lifted_mgroups_limits,
                                        &lifted_mgroups, &err);
    }
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
    //if (strips.has_cond_eff)
    //    pddlStripsCompileAwayCondEff(&strips);
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "Has conditional effects -- terminating...");
        return -1;
    }

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

    // Find fam-groups
    if (opt.fam_groups){
        pddl_famgroup_config_t fam_cfg = PDDL_FAMGROUP_CONFIG_INIT;
        if (pddlFAMGroupsInfer(&mgroups, &strips, &fam_cfg, &err) != 0){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }
    }

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

    pddl_heur_t *heur = createHeur(&fdr, &err);

    pddl_search_astar_t *astar;
    astar = pddlSearchAStar(&fdr, heur, &err);
    int ret = pddlSearchAStarInitStep(astar);
    search_started = 1;

    bor_timer_t info_timer;
    borTimerStart(&info_timer);
    for (int step = 1; ret == PDDL_SEARCH_CONT; ++step){
        if (terminate){
            printSearchStat(astar, &err);
            BOR_INFO2(&err, "Search aborted.");
            exit(-1);
        }

        ret = pddlSearchAStarStep(astar);
        if (step >= 100){
            borTimerStop(&info_timer);
            if (borTimerElapsedInSF(&info_timer) >= 1.){
                printSearchStat(astar, &err);
                borTimerStart(&info_timer);
            }
            step = 0;
        }
    }
    printSearchStat(astar, &err);

    if (ret == PDDL_SEARCH_UNSOLVABLE){
        BOR_INFO2(&err, "Problem is unsolvable.");

    }else if (ret == PDDL_SEARCH_FOUND){
        BOR_INFO2(&err, "Plan found.");
        pddl_plan_t plan;
        pddlPlanInit(&plan);
        pddlPlanLoadBacktrack(&plan, astar->goal_state_id, &astar->state_space);
        BOR_INFO(&err, "Plan Cost: %d", plan.cost);
        BOR_INFO(&err, "Plan Length: %d", plan.length);
        if (opt.out == NULL || strcmp(opt.out, "-") == 0){
            pddlPlanPrint(&plan, &fdr.op, stdout);
        }else{
            FILE *fout;
            if ((fout = fopen(opt.out, "w")) != NULL){
                pddlPlanPrint(&plan, &fdr.op, fout);
                fclose(fout);
            }else{
                BOR_ERR(&err, "Could not open file '%s'", opt.out);
                fprintf(stderr, "Error: ");
                borErrPrint(&err, 1, stderr);
                return -1;
            }
        }
        pddlPlanFree(&plan);
    }else{
        BOR_FATAL("Unkown return status: %d", ret);
    }

    if (terminate){
        BOR_INFO2(&err, "Search aborted.");
        exit(-1);
    }

    pddlSearchAStarDel(astar);
    pddlHeurDel(heur);

    optsClear();
    pddlFDRFree(&fdr);
    pddlMutexPairsFree(&mutex);
    pddlMGroupsFree(&mgroups);
    pddlStripsFree(&strips);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    pddlFree(&pddl);
    return 0;
}
