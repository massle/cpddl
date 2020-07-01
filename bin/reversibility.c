#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

struct options {
    int help;
    int not_force_adl;
    int compile_away_cond_eff;
    int compile_away_cond_eff_pddl;

    int lifted_mgroup_max_candidates;
    int lifted_mgroup_max_mgroups;
    int lifted_mgroup_fd;

    int no_ground_prune;
    int no_ground_prune_pre;
    int no_ground_prune_dead_end;

    int fam;
    int fam_fixpoint;
    int fam_fixpoint_no_de;
    int fam_lmg;
    int h2_mgroup;
    int h2_fixpoint;
    int famh2_fixpoint;
    int famh2fwbw_fixpoint;
    int h2fwbw_fixpoint;

    int h2fw;
    int no_dead_end_op;
    int no_h2;
    int no_irr;

    const char *out;

    int use_mutex;
    int max_depth;
    int iterative;
    int list_ops;

    int tmp_op_id;
} opt;

bor_err_t err = BOR_ERR_INIT;
pddl_files_t files;
pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
pddl_t pddl;
pddl_lifted_mgroups_infer_limits_t lifted_mgroups_limits
            = PDDL_LIFTED_MGROUPS_INFER_LIMITS_INIT;
pddl_lifted_mgroups_t lifted_mgroups;
pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;
pddl_strips_t strips;
pddl_mgroups_t mgroups;
pddl_mutex_pairs_t mutex;
bor_iset_t op_ids;

static FILE *openFile(const char *fn)
{
    if (fn == NULL
            || strcmp(fn, "-") == 0
            || strcmp(fn, "stdout") == 0)
        return stdout;
    if (strcmp(fn, "stderr") == 0)
        return stderr;
    FILE *fout = fopen(fn, "w");
    return fout;
}

static void closeFile(FILE *f)
{
    if (f != NULL && f != stdout && f != stderr)
        fclose(f);
}

static void addOpId(const char *l, char s, int val)
{
    borISetAdd(&op_ids, val);
}

static int readOpts(int *argc, char *argv[])
{
    bzero(&opt, sizeof(opt));
    opt.lifted_mgroup_max_candidates = 10000;
    opt.lifted_mgroup_max_mgroups = 10000;
    opt.out = "-";
    opt.max_depth = -1;
    opt.iterative = -1;
    borISetInit(&op_ids);

    pddl_cfg.force_adl = 1;

    optsAddDesc("help", 'h', OPTS_NONE, &opt.help, NULL,
                "Print this help.");
    optsAddDesc("output", 'o', OPTS_STR, &opt.out, NULL,
                "Output filename (default: stdout)");

    optsAddDesc("no-adl", 0x0, OPTS_NONE, &opt.not_force_adl, NULL,
                "Do NOT force :adl requirement if it is not specified in the"
                " domain file.");
    optsAddDesc("ce", 0x0, OPTS_NONE, &opt.compile_away_cond_eff, NULL,
                "Compile away conditional effects on the STRIPS level"
                " (recommended instead of --ce-pddl).");
    optsAddDesc("ce-pddl", 0x0, OPTS_NONE, &opt.compile_away_cond_eff_pddl,
                NULL,
                "Compile away conditional effects on the PDDL level.");

    optsAddDesc("lmg-max-candidates", 0x0, OPTS_INT,
                &opt.lifted_mgroup_max_candidates, NULL,
                "Maximum number of lifted mutex group candidates."
                " (default: 10000)");
    optsAddDesc("lmg-max-mgroups", 0x0, OPTS_INT,
                &opt.lifted_mgroup_max_mgroups, NULL,
                "Maximum number of lifted mutex group. (default: 10000)");
    optsAddDesc("lmg-fd", 0x0, OPTS_NONE, &opt.lifted_mgroup_fd, NULL,
                "Find Fast-Downward type of lifted mutex groups.");

    optsAddDesc("no-ground-prune", 0x0, OPTS_NONE, &opt.no_ground_prune, NULL,
                "Do NOT use lifted mutex groups for pruning during grounding."
                " (default: off)");
    optsAddDesc("no-ground-prune-pre", 0x0, OPTS_NONE,
                &opt.no_ground_prune_pre, NULL,
                "Do NOT use lifted mutex groups for pruning during grounding by"
                " checking preconditions.");
    optsAddDesc("no-ground-prune-dead-end", 0x0, OPTS_NONE,
                &opt.no_ground_prune_dead_end, NULL,
                "Do NOT use lifted mutex groups for pruning of dead-end"
                " operators during grounding.");

    optsAddDesc("fam", 'f', OPTS_NONE, &opt.fam, NULL,
                "Infer fact-alternating mutex groups with ILP-based"
                " algorithm. (default: off)");
    optsAddDesc("fam-fixpoint", 0x0, OPTS_NONE, &opt.fam_fixpoint, NULL,
                "Infer fact-alternating mutex groups with ILP-based"
                " algorithm and use a fixpoint pruning (--fam-lmg also takes"
                " effect if used). (default: off)");
    optsAddDesc("fam-fixpoint-no-de", 0x0, OPTS_NONE, &opt.fam_fixpoint_no_de,
                NULL,
                "Same as --fam-fixpoint, but dead-end operator detection is"
                " disabled. (default: off)");
    optsAddDesc("fam-lmg", 0x0, OPTS_NONE, &opt.fam_lmg, NULL,
                "Use grounded lifted mutex groups as initialization for"
                " fam-group. (default: off)");
    optsAddDesc("h2mg", 0x0, OPTS_NONE, &opt.h2_mgroup, NULL,
                "Infer h^2 based mutex groups. (default: off)");
    optsAddDesc("h2-fixpoint", 0x0, OPTS_NONE, &opt.h2_fixpoint, NULL,
                "Infer h^2 based mutex groups and use a fixpoint pruning."
                " (default: off)");
    optsAddDesc("famh2-fixpoint", 0x0, OPTS_NONE, &opt.famh2_fixpoint, NULL,
                "--fam-fixpoint with h^2 forward pruning. (default: off)");
    optsAddDesc("famh2fwbw-fixpoint", 0x0, OPTS_NONE, &opt.famh2fwbw_fixpoint, NULL,
                "--fam-fixpoint with h^2 forward/backward pruning. (default: off)");
    optsAddDesc("h2fwbw-fixpoint", 0x0, OPTS_NONE, &opt.h2fwbw_fixpoint, NULL,
                "h^2 forward/backward pruning in a fixpoint compuation."
                " (default: off)");

    optsAddDesc("h2fw", 0x0, OPTS_NONE, &opt.h2fw, NULL,
                "Use only forward h^2 for pruning (instead of"
                " forward/backward).");
    optsAddDesc("no-dead-end-op", 0x0, OPTS_NONE, &opt.no_dead_end_op, NULL,
                "Do NOT use fam-groups for detecting dead-end operators.");
    optsAddDesc("no-h2", 0x0, OPTS_NONE, &opt.no_h2, NULL,
                "Do NOT use h^2 for pruning.");
    optsAddDesc("no-irrelevance", 0x0, OPTS_NONE, &opt.no_irr, NULL,
                "Do NOT use irrelevance analysis.");

    optsAddDesc("use-mutex", 'm', OPTS_NONE, &opt.use_mutex, NULL,
                "Use mutexes in inference of reverse plans.");
    optsAddDesc("max-depth", 'd', OPTS_INT, &opt.max_depth, NULL,
                "Basic variant -- max depth.");
    optsAddDesc("iterivate", 'i', OPTS_INT, &opt.iterative, NULL,
                "Iterative variant -- max depth.");
    optsAddDesc("list-ops", 'l', OPTS_NONE, &opt.list_ops, NULL,
                "List all operators.");
    optsAddDesc("op-id", 0x0, OPTS_INT, &opt.tmp_op_id, OPTS_CB(addOpId),
                "Reversibility only for the specified operator(s)");

    if (opts(argc, argv) != 0 || opt.help || (*argc != 3 && *argc != 2)){
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

        fprintf(stderr, "pddl-reversibility is a program for computing"
                        " uniform reversibility.\n");
        fprintf(stderr, "Usage: %s [OPTIONS] domain.pddl problem.pddl\n",
                argv[0]);
        fprintf(stderr, "  OPTIONS:\n");
        optsPrint(stderr, "    ");
        fprintf(stderr, "\n");
        return -1;
    }


    if (opt.fam_fixpoint_no_de)
        opt.fam_fixpoint = 1;

    if (opt.fam && opt.h2_mgroup){
        fprintf(stderr, "Error: --fam and --h2mg cannot be used together.\n");
        return -1;
    }

    if (opt.fam_lmg
            && !opt.fam
            && !opt.fam_fixpoint
            && !opt.famh2_fixpoint
            && !opt.famh2fwbw_fixpoint
            && !opt.h2fwbw_fixpoint){
        fprintf(stderr, "Error: --fam-lmg has no effect: use --fam or"
                        " --fam-fixpoint or --famh2-fixpoint"
                        " or --famh2fwbw-fixpoint or --h2fwbw-fixpoint.\n");
        return -1;
    }

    if (opt.no_ground_prune_pre && opt.no_ground_prune_dead_end)
        opt.no_ground_prune = 1;

    if (opt.max_depth >= 0){
        BOR_INFO(&err, "Max Depth: %d", opt.max_depth);
    }

    if (opt.iterative >= 0){
        BOR_INFO(&err, "Iterative Max Depth: %d", opt.iterative);
    }

    if (opt.max_depth < 0
            && opt.iterative < 0
            && !opt.list_ops){
        fprintf(stderr, "Error: One of -d/-i/-l must be specified.\n");
        return -1;
    }

    if (*argc == 2){
        BOR_INFO(&err, "Input file: '%s'", argv[2]);
        if (pddlFiles1(&files, argv[1], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }else{ // *argc == 3
        BOR_INFO(&err, "Input files: '%s' and '%s'", argv[2], argv[3]);
        if (pddlFiles(&files, argv[1], argv[2], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }

    BOR_INFO(&err, "Use Mutexes: %d", opt.use_mutex);
    BOR_INFO(&err, "Iterative: %d", opt.iterative);

    return 0;
}

static int readPDDL(void)
{
    BOR_INFO2(&err, "Reading PDDL ...");
    BOR_INFO(&err, "PDDL option no-adl: %d", opt.not_force_adl);

    if (opt.not_force_adl)
        pddl_cfg.force_adl = 0;

    if (pddlInit(&pddl, files.domain_pddl, files.problem_pddl,
                 &pddl_cfg, &err) != 0){
        BOR_TRACE_RET(&err, -1);
    }

    pddlNormalize(&pddl);
    if (opt.compile_away_cond_eff_pddl)
        pddlCompileAwayCondEff(&pddl);
    pddlCheckSizeTypes(&pddl);

    BOR_INFO(&err, "Number of PDDL Types: %d", pddl.type.type_size);
    BOR_INFO(&err, "Number of PDDL Objects: %d", pddl.obj.obj_size);
    BOR_INFO(&err, "Number of PDDL Predicates: %d", pddl.pred.pred_size);
    BOR_INFO(&err, "Number of PDDL Functions: %d", pddl.func.pred_size);
    BOR_INFO(&err, "Number of PDDL Actions: %d", pddl.action.action_size);
    BOR_INFO(&err, "PDDL Metric: %d", pddl.metric);
    fflush(stdout);
    fflush(stderr);

    return 0;
}

static int liftedMGroups(void)
{
    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Inference of lifted mutex groups ...");
    BOR_INFO(&err, "Lifted mutex groups option lmg-fd: %d",
             opt.lifted_mgroup_fd);
    BOR_INFO(&err, "Lifted mutex groups option lmg-max-candidates: %d",
             opt.lifted_mgroup_max_candidates);
    BOR_INFO(&err, "Lifted mutex groups option lmg-max-mgroups: %d",
             opt.lifted_mgroup_max_mgroups);

    lifted_mgroups_limits.max_candidates = opt.lifted_mgroup_max_candidates;
    lifted_mgroups_limits.max_mgroups = opt.lifted_mgroup_max_mgroups;
    pddlLiftedMGroupsInit(&lifted_mgroups);
    if (opt.lifted_mgroup_fd){
        pddlLiftedMGroupsInferMonotonicity(&pddl, &lifted_mgroups_limits, NULL,
                                           &lifted_mgroups, &err);
    }else{
        pddlLiftedMGroupsInferFAMGroups(&pddl, &lifted_mgroups_limits,
                                        &lifted_mgroups, &err);
    }
    pddlLiftedMGroupsSetExactlyOne(&pddl, &lifted_mgroups, &err);
    pddlLiftedMGroupsSetStatic(&pddl, &lifted_mgroups, &err);

    return 0;
}


static int groundStrips(void)
{
    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Grounding of STRIPS ...");
    BOR_INFO(&err, "Grounding of STRIPS option no-ground-prune: %d",
             opt.no_ground_prune);
    BOR_INFO(&err, "Grounding of STRIPS option no-ground-prune-pre: %d",
             opt.no_ground_prune_pre);
    BOR_INFO(&err, "Grounding of STRIPS option no-ground-prune-dead-end: %d",
             opt.no_ground_prune_dead_end);

    ground_cfg.lifted_mgroups = &lifted_mgroups;
    ground_cfg.prune_op_pre_mutex = 1;
    ground_cfg.prune_op_dead_end = 1;
    if (opt.no_ground_prune)
        ground_cfg.lifted_mgroups = NULL;
    if (opt.no_ground_prune_pre)
        ground_cfg.prune_op_pre_mutex = 0;
    if (opt.no_ground_prune_dead_end)
        ground_cfg.prune_op_dead_end = 0;

    if (pddlStripsGround(&strips, &pddl, &ground_cfg, &err) != 0){
        BOR_INFO2(&err, "Grounding failed.");
        BOR_TRACE_RET(&err, -1);
    }

    if (opt.compile_away_cond_eff)
        pddlStripsCompileAwayCondEff(&strips);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);

    int count = 0;
    for (int i = 0; i < strips.op.op_size; ++i){
        if (strips.op.op[i]->cond_eff_size > 0)
            ++count;
    }
    BOR_INFO(&err, "Number of Strips Operators"
            " with Conditional Effects: %d", count);
    BOR_INFO(&err, "Goal is unreachable: %d",
            strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    fflush(stdout);
    fflush(stderr);

    return 0;
}

static int groundMGroups(void)
{
    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Grounding mutex groups ...");

    pddlMGroupsGround(&mgroups, &pddl, &lifted_mgroups, &strips);
    pddlMGroupsSetExactlyOne(&mgroups, &strips);
    pddlMGroupsSetGoal(&mgroups, &strips);
    BOR_INFO(&err, "Found %d mutex groups", mgroups.mgroup_size);

    return 0;
}

static int inferMutexGroups(void)
{
    if (!opt.fam && !opt.h2_mgroup)
        return 0;

    if (strips.has_cond_eff){
        BOR_INFO2(&err, "fam-groups and h^2 disabled because the problem has"
                " conditional effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Inference of mutex groups...");

    if (opt.fam){
        pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
        if (!opt.fam_lmg){
            // Clean mgroups if we want only fam-groups
            pddlMGroupsFree(&mgroups);
            pddlMGroupsInitEmpty(&mgroups);
        }
        if (pddlFAMGroupsInfer(&mgroups, &strips, &cfg, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        if (opt.fam_lmg)
            pddlMGroupsRemoveSubsets(&mgroups);
        BOR_INFO(&err, "Found %d fam-groups.", mgroups.mgroup_size);

    }else if (opt.h2_mgroup){
        pddl_mutex_pairs_t mutex;
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2(&strips, &mutex, NULL, NULL, &err) != 0){
            BOR_INFO2(&err, "h^2 fw failed.");
            BOR_TRACE_RET(&err, -1);
        }

        // Clean mgroups if we want only h^2 mutex groups
        pddlMGroupsFree(&mgroups);
        pddlMGroupsInitEmpty(&mgroups);
        BOR_INFO2(&err, "Inference of h^2 mutex groups...");
        pddlMutexPairsInferMutexGroups(&mutex, &mgroups);
        BOR_INFO(&err, "Found %d h^2 mutex groups.", mgroups.mgroup_size);
        pddlMutexPairsFree(&mutex);
    }

    pddlMGroupsSetExactlyOne(&mgroups, &strips);
    pddlMGroupsSetGoal(&mgroups, &strips);

    BOR_INFO2(&err, "Inference of mutex groups DONE.");

    return 0;
}

static void reduceStrips(const bor_iset_t *rm_fact, const bor_iset_t *rm_op)
{
    if (borISetSize(rm_fact) == 0 && borISetSize(rm_op) == 0)
        return;

    pddlStripsReduce(&strips, rm_fact, rm_op);
    if (borISetSize(rm_fact) > 0){
        pddlMutexPairsReduce(&mutex, rm_fact);

        pddlMGroupsReduce(&mgroups, rm_fact);
        pddlMGroupsSetExactlyOne(&mgroups, &strips);
        pddlMGroupsSetGoal(&mgroups, &strips);
    }
}

static int pruneStripsFixpointFAMGroups(void)
{
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "fam-groups disabled because the problem has"
                        " conditional effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups...");

    pddl_mgroups_t mgs;
    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    int orig_fact_size, orig_op_size;
    pddlMGroupsInitEmpty(&mgs);
    pddlMutexPairsInitStrips(&mutex, &strips);
    do {
        orig_fact_size = strips.fact.fact_size;
        orig_op_size = strips.op.op_size;

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        if (pddlIrrelevanceAnalysis(&strips, &rm_fact, &rm_op,
                                    NULL, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        reduceStrips(&rm_fact, &rm_op);

        pddlMGroupsFree(&mgs);
        if (opt.fam_lmg){
            pddlMGroupsInitCopy(&mgs, &mgroups);
        }else{
            pddlMGroupsInitEmpty(&mgs);
        }
        pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
        if (pddlFAMGroupsInfer(&mgs, &strips, &cfg, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        if (opt.fam_lmg)
            pddlMGroupsRemoveSubsets(&mgs);
        BOR_INFO(&err, "Found %d fam-groups.", mgs.mgroup_size);

        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &strips);
        pddlMutexPairsAddMGroups(&mutex, &mgs);

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        BOR_INFO2(&err, "Pruning unreachable operators with inferred"
                        " mutex groups...");
        if (pddlStripsFindUnreachableOps(&strips, &mutex, &rm_op, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }

        if (opt.fam_fixpoint_no_de){
            BOR_INFO2(&err, "Pruning of dead-end operators disabled.");
        }else{
            BOR_INFO2(&err, "Pruning dead-end operators ...");
            int unreachable_size = borISetSize(&rm_op);
            pddlFAMGroupsDeadEndOps(&mgs, &strips, &rm_op);
            BOR_INFO(&err, "Pruning dead-end operators done. Dead end ops: %d",
                     borISetSize(&rm_op) - unreachable_size);
        }

        reduceStrips(&rm_fact, &rm_op);
    } while (strips.op.op_size != orig_op_size
                || strips.fact.fact_size != orig_fact_size);

    pddlMGroupsFree(&mgroups);
    pddlMGroupsInitCopy(&mgroups, &mgs);
    pddlMGroupsFree(&mgs);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    if (opt.h2_mgroup){
        pddlMGroupsFree(&mgroups);
        pddlMGroupsInitEmpty(&mgroups);
        BOR_INFO2(&err, "Inference of h^2 mutex groups...");
        pddlMutexPairsInferMutexGroups(&mutex, &mgroups);
        BOR_INFO(&err, "Found %d h^2 mutex groups.", mgroups.mgroup_size);
    }

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);
    BOR_INFO(&err, "Goal is unreachable: %d", strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    BOR_INFO(&err, "Mutex pairs after reduction: %d", mutex.num_mutex_pairs);
    BOR_INFO(&err, "Mutex groups after reduction: %d", mgroups.mgroup_size);
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups DONE.");
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static int pruneStripsFixpointH2(void)
{
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "h^2 disabled because the problem has conditional"
                        " effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Fixpoint pruning using h^2...");

    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    int orig_fact_size, orig_op_size;
    pddlMutexPairsInitStrips(&mutex, &strips);
    do {
        orig_fact_size = strips.fact.fact_size;
        orig_op_size = strips.op.op_size;

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        if (pddlIrrelevanceAnalysis(&strips, &rm_fact, &rm_op,
                                    NULL, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        reduceStrips(&rm_fact, &rm_op);

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2(&strips, &mutex, &rm_fact, &rm_op, &err) != 0){
            BOR_INFO2(&err, "h^2 fw failed.");
            BOR_TRACE_RET(&err, -1);
        }

        reduceStrips(&rm_fact, &rm_op);
    } while (strips.op.op_size != orig_op_size
                || strips.fact.fact_size != orig_fact_size);

    pddlMGroupsFree(&mgroups);
    pddlMGroupsInitEmpty(&mgroups);
    BOR_INFO2(&err, "Inference of h^2 mutex groups...");
    pddlMutexPairsInferMutexGroups(&mutex, &mgroups);
    BOR_INFO(&err, "Found %d h^2 mutex groups.", mgroups.mgroup_size);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);
    BOR_INFO(&err, "Goal is unreachable: %d", strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    BOR_INFO(&err, "Mutex pairs after reduction: %d", mutex.num_mutex_pairs);
    BOR_INFO(&err, "Mutex groups after reduction: %d", mgroups.mgroup_size);
    BOR_INFO2(&err, "Fixpoint pruning using h^2 DONE.");
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static int pruneStripsFixpointFAMH2(void)
{
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "fam-groups disabled because the problem has"
                        " conditional effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups and h^2...");

    pddl_mgroups_t mgs;
    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    int orig_fact_size, orig_op_size;
    pddlMGroupsInitEmpty(&mgs);
    pddlMutexPairsInitStrips(&mutex, &strips);
    do {
        orig_fact_size = strips.fact.fact_size;
        orig_op_size = strips.op.op_size;

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        if (pddlIrrelevanceAnalysis(&strips, &rm_fact, &rm_op,
                                    NULL, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        reduceStrips(&rm_fact, &rm_op);

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2(&strips, &mutex, &rm_fact, &rm_op, &err) != 0){
            BOR_INFO2(&err, "h^2 fw failed.");
            BOR_TRACE_RET(&err, -1);
        }

        reduceStrips(&rm_fact, &rm_op);

        pddlMGroupsFree(&mgs);
        if (opt.fam_lmg){
            pddlMGroupsInitCopy(&mgs, &mgroups);
        }else{
            pddlMGroupsInitEmpty(&mgs);
        }
        pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
        if (pddlFAMGroupsInfer(&mgs, &strips, &cfg, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        if (opt.fam_lmg)
            pddlMGroupsRemoveSubsets(&mgs);
        BOR_INFO(&err, "Found %d fam-groups.", mgs.mgroup_size);

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        BOR_INFO2(&err, "Pruning dead-end operators ...");
        int unreachable_size = borISetSize(&rm_op);
        pddlFAMGroupsDeadEndOps(&mgs, &strips, &rm_op);
        BOR_INFO(&err, "Pruning dead-end operators done. Dead end ops: %d",
                 borISetSize(&rm_op) - unreachable_size);

        reduceStrips(&rm_fact, &rm_op);
    } while (strips.op.op_size != orig_op_size
                || strips.fact.fact_size != orig_fact_size);

    pddlMGroupsFree(&mgroups);
    pddlMGroupsInitCopy(&mgroups, &mgs);
    pddlMGroupsFree(&mgs);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    if (opt.h2_mgroup){
        pddlMGroupsFree(&mgroups);
        pddlMGroupsInitEmpty(&mgroups);
        BOR_INFO2(&err, "Inference of h^2 mutex groups...");
        pddlMutexPairsInferMutexGroups(&mutex, &mgroups);
        BOR_INFO(&err, "Found %d h^2 mutex groups.", mgroups.mgroup_size);
    }

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);
    BOR_INFO(&err, "Goal is unreachable: %d", strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    BOR_INFO(&err, "Mutex pairs after reduction: %d", mutex.num_mutex_pairs);
    BOR_INFO(&err, "Mutex groups after reduction: %d", mgroups.mgroup_size);
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups and h^2 DONE.");
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static int pruneStripsFixpointFAMH2FwBw(void)
{
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "fam-groups disabled because the problem has"
                        " conditional effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups and h^2 fw/bw...");

    pddl_mgroups_t mgs;
    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    int orig_fact_size, orig_op_size;
    pddlMGroupsInitEmpty(&mgs);
    pddlMutexPairsInitStrips(&mutex, &strips);
    do {
        orig_fact_size = strips.fact.fact_size;
        orig_op_size = strips.op.op_size;

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        if (pddlIrrelevanceAnalysis(&strips, &rm_fact, &rm_op,
                                    NULL, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        reduceStrips(&rm_fact, &rm_op);

        pddlMGroupsFree(&mgs);
        if (opt.fam_lmg){
            pddlMGroupsInitCopy(&mgs, &mgroups);
        }else{
            pddlMGroupsInitEmpty(&mgs);
        }
        pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
        if (pddlFAMGroupsInfer(&mgs, &strips, &cfg, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        if (opt.fam_lmg)
            pddlMGroupsRemoveSubsets(&mgs);
        BOR_INFO(&err, "Found %d fam-groups.", mgs.mgroup_size);

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        pddl_mg_strips_t mg_strips;
        pddlMGStripsInit(&mg_strips, &strips, &mgs);
        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &mutex,
                       &rm_fact, &rm_op, &err) != 0){
            BOR_INFO2(&err, "h^2 fw/bw failed.");
            BOR_TRACE_RET(&err, -1);
        }
        pddlMGStripsFree(&mg_strips);

        BOR_INFO2(&err, "Pruning dead-end operators ...");
        int unreachable_size = borISetSize(&rm_op);
        pddlFAMGroupsDeadEndOps(&mgs, &strips, &rm_op);
        BOR_INFO(&err, "Pruning dead-end operators done. Dead end ops: %d",
                 borISetSize(&rm_op) - unreachable_size);
        reduceStrips(&rm_fact, &rm_op);

    } while (strips.op.op_size != orig_op_size
                || strips.fact.fact_size != orig_fact_size);

    pddlMGroupsFree(&mgroups);
    pddlMGroupsInitCopy(&mgroups, &mgs);
    pddlMGroupsFree(&mgs);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    if (opt.h2_mgroup){
        pddlMGroupsFree(&mgroups);
        pddlMGroupsInitEmpty(&mgroups);
        BOR_INFO2(&err, "Inference of h^2 mutex groups...");
        pddlMutexPairsInferMutexGroups(&mutex, &mgroups);
        BOR_INFO(&err, "Found %d h^2 mutex groups.", mgroups.mgroup_size);
    }

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);
    BOR_INFO(&err, "Goal is unreachable: %d", strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    BOR_INFO(&err, "Mutex pairs after reduction: %d", mutex.num_mutex_pairs);
    BOR_INFO(&err, "Mutex groups after reduction: %d", mgroups.mgroup_size);
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups and h^2 fw/bw DONE.");
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static int pruneStripsFixpointH2FwBw(void)
{
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "fam-groups disabled because the problem has"
                        " conditional effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups and h^2 fw/bw...");

    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);
    int orig_fact_size, orig_op_size;
    pddlMutexPairsInitStrips(&mutex, &strips);
    do {
        orig_fact_size = strips.fact.fact_size;
        orig_op_size = strips.op.op_size;

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        if (pddlIrrelevanceAnalysis(&strips, &rm_fact, &rm_op,
                                    NULL, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        reduceStrips(&rm_fact, &rm_op);

        borISetEmpty(&rm_fact);
        borISetEmpty(&rm_op);
        pddl_mg_strips_t mg_strips;
        pddlMGStripsInit(&mg_strips, &strips, &mgroups);
        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &mutex,
                       &rm_fact, &rm_op, &err) != 0){
            BOR_INFO2(&err, "h^2 fw/bw failed.");
            BOR_TRACE_RET(&err, -1);
        }
        pddlMGStripsFree(&mg_strips);
        reduceStrips(&rm_fact, &rm_op);

    } while (strips.op.op_size != orig_op_size
                || strips.fact.fact_size != orig_fact_size);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);
    BOR_INFO(&err, "Goal is unreachable: %d", strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    BOR_INFO(&err, "Mutex pairs after reduction: %d", mutex.num_mutex_pairs);
    BOR_INFO(&err, "Mutex groups after reduction: %d", mgroups.mgroup_size);
    BOR_INFO2(&err, "Fixpoint pruning using fam-groups and h^2 fw/bw DONE.");
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static int pruneStrips(void)
{
    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Pruning of STRIPS ...");

    BOR_ISET(rm_fact);
    BOR_ISET(rm_op);

    pddlMutexPairsInitStrips(&mutex, &strips);
    pddlMutexPairsAddMGroups(&mutex, &mgroups);

    if (mutex.num_mutex_pairs > 0){
        BOR_INFO2(&err, "Pruning unreachable operators with inferred"
                        " mutex groups...");
        if (pddlStripsFindUnreachableOps(&strips, &mutex, &rm_op, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
    }

    if (opt.no_dead_end_op){
        BOR_INFO2(&err, "Pruning dead-end operators disabled");

    }else{
        BOR_INFO2(&err, "Pruning dead-end operators ...");
        pddlFAMGroupsDeadEndOps(&mgroups, &strips, &rm_op);
        BOR_INFO(&err, "Pruning dead-end operators done. Dead end ops: %d",
                 borISetSize(&rm_op));
    }

    if (opt.no_h2){
        BOR_INFO2(&err, "h^2 disabled");

    }else if (strips.has_cond_eff){
        BOR_INFO2(&err, "h^2 disabled because the problem has conditional"
                        " effects.");

    }else{
        if (opt.h2fw){
            if (pddlH2(&strips, &mutex, &rm_fact, &rm_op, &err) != 0){
                BOR_INFO2(&err, "h^2 fw failed.");
                BOR_TRACE_RET(&err, -1);
            }
        }else if (!opt.no_h2){
            pddl_mg_strips_t mg_strips;
            pddlMGStripsInit(&mg_strips, &strips, &mgroups);
            if (pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &mutex,
                        &rm_fact, &rm_op, &err) != 0){
                BOR_INFO2(&err, "h^2 fw/bw failed.");
                BOR_TRACE_RET(&err, -1);
            }
            pddlMGStripsFree(&mg_strips);
        }
    }

    if (strips.has_cond_eff){
        BOR_INFO2(&err, "irrelevance analysis disabled because the problem"
                        " has conditional effects.");

    }else if (!opt.no_irr){
        BOR_ISET(irr_fact);
        BOR_ISET(irr_op);
        BOR_ISET(static_fact);
        if (pddlIrrelevanceAnalysis(&strips, &irr_fact, &irr_op,
                                    &static_fact, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        borISetUnion(&rm_fact, &irr_fact);
        borISetUnion(&rm_op, &irr_op);

        borISetFree(&irr_fact);
        borISetFree(&irr_op);
        borISetFree(&static_fact);
    }

    reduceStrips(&rm_fact, &rm_op);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
    BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);

    int count = 0;
    for (int i = 0; i < strips.op.op_size; ++i){
        if (strips.op.op[i]->cond_eff_size > 0)
            ++count;
    }
    BOR_INFO(&err, "Number of Strips Operators with Conditional Effects: %d",
             count);
    BOR_INFO(&err, "Goal is unreachable: %d", strips.goal_is_unreachable);
    BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
    BOR_INFO(&err, "Mutex pairs after reduction: %d", mutex.num_mutex_pairs);
    BOR_INFO(&err, "Mutex groups after reduction: %d", mgroups.mgroup_size);
    fflush(stdout);
    fflush(stderr);

    return 0;
}

static int mgroupsAndPruning(void)
{
    if (opt.fam_fixpoint)
        return pruneStripsFixpointFAMGroups();
    if (opt.h2_fixpoint)
        return pruneStripsFixpointH2();
    if (opt.famh2_fixpoint)
        return pruneStripsFixpointFAMH2();
    if (opt.famh2fwbw_fixpoint)
        return pruneStripsFixpointFAMH2FwBw();
    if (opt.h2fwbw_fixpoint)
        return pruneStripsFixpointH2FwBw();

    if (inferMutexGroups() != 0)
        return -1;
    if (pruneStrips() != 0)
        return -1;

    return 0;
}

static void reversibilityIterativeDepth(int *skip, int max_depth, FILE *fout)
{
    int op_id;
    BOR_ISET_FOR_EACH(&op_ids, op_id){
        if (op_id < 0 || op_id >= strips.op.op_size)
            continue;
        if (skip[op_id])
            continue;

        const pddl_strips_op_t *op = strips.op.op[op_id];

        pddl_reversibility_uniform_t rev;
        pddlReversibilityUniformInit(&rev);
        const pddl_mutex_pairs_t *m = NULL;
        if (opt.use_mutex)
            m = &mutex;
        pddlReversibilityUniformInfer(&rev, &strips.op, op, max_depth, m);
        pddlReversibilityUniformSort(&rev);
        for (int i = 0; i < rev.plan_size; ++i){
            if (rev.plan[i].reversible_op_id == op->id
                    && borISetSize(&rev.plan[i].formula.pos) == 0
                    && borISetSize(&rev.plan[i].formula.neg) == 0){
                skip[op_id] = 1;
            }
            if (borIArrSize(&rev.plan[i].plan) == max_depth){
                pddlReversePlanUniformPrint(rev.plan + i, &strips.op, fout);
            }
        }
        pddlReversibilityUniformFree(&rev);
    }
}

static void reversibilityIterative(FILE *fout, int max_depth)
{
    BOR_INFO(&err, "Computing reverse plans iteratively. max-depth: %d",
                   max_depth);
    int *skip = BOR_CALLOC_ARR(int, strips.op.op_size);
    for (int depth = 1; depth <= max_depth; ++depth){
        BOR_INFO(&err, "Computing for max-depth: %d", depth);
        reversibilityIterativeDepth(skip, depth, fout);
    }
    BOR_FREE(skip);
    BOR_INFO2(&err, "Reverse plans computed.");
}

static void reversibilitySimple(FILE *fout, int max_depth)
{
    BOR_INFO(&err, "Computing reverse plans. max-depth: %d", max_depth);
    int op_id;
    BOR_ISET_FOR_EACH(&op_ids, op_id){
        if (op_id < 0 || op_id >= strips.op.op_size)
            continue;
        const pddl_strips_op_t *op = strips.op.op[op_id];

        pddl_reversibility_uniform_t rev;
        pddlReversibilityUniformInit(&rev);
        const pddl_mutex_pairs_t *m = NULL;
        if (opt.use_mutex)
            m = &mutex;
        pddlReversibilityUniformInfer(&rev, &strips.op, op, max_depth, m);
        pddlReversibilityUniformSort(&rev);
        pddlReversibilityUniformPrint(&rev, &strips.op, fout);
        pddlReversibilityUniformFree(&rev);
    }
    BOR_INFO2(&err, "Reverse plans computed.");
}

static void listOps(FILE *fout)
{
    BOR_INFO2(&err, "Printing operators...");
    for (int op_id = 0; op_id < strips.op.op_size; ++op_id){
        const pddl_strips_op_t *op = strips.op.op[op_id];
        fprintf(fout, "%d:'%s'\n", op_id, op->name);
    }
}

static int reversibility(void)
{
    BOR_INFO(&err, "Output file: '%s'", opt.out);
    FILE *fout = openFile(opt.out);

    if (borISetSize(&op_ids) == 0){
        for (int op_id = 0; op_id < strips.op.op_size; ++op_id)
            borISetAdd(&op_ids, op_id);
    }

    if (opt.list_ops){
        listOps(fout);
    }else if (opt.iterative >= 0){
        reversibilityIterative(fout, opt.iterative);
    }else if (opt.max_depth >= 0){
        reversibilitySimple(fout, opt.max_depth);
    }
    closeFile(fout);
    return 0;
}

int main(int argc, char *argv[])
{
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);

    if (readOpts(&argc, argv) != 0
            || readPDDL() != 0
            || liftedMGroups() != 0
            || groundStrips() != 0
            || groundMGroups() != 0
            || mgroupsAndPruning() != 0
            || reversibility() != 0){
        if (borErrIsSet(&err)){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
        }
        return -1;
    }

    optsClear();
    pddlMutexPairsFree(&mutex);
    pddlMGroupsFree(&mgroups);
    pddlStripsFree(&strips);
    pddlLiftedMGroupsFree(&lifted_mgroups);
    pddlFree(&pddl);
    borISetFree(&op_ids);
    return 0;
}



