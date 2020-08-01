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
    int fam_lmg;
    int h2_mgroup;

    int hm;
    float hm_time_limit;
    int hm_excess_mem;

    int op_mutex_ts;
    int op_mutex_op_fact;
    int op_mutex_hm_op;
    const char *op_mutex_out;
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

static int readOpts(int *argc, char *argv[])
{
    bzero(&opt, sizeof(opt));
    opt.lifted_mgroup_max_candidates = 10000;
    opt.lifted_mgroup_max_mgroups = 10000;
    opt.op_mutex_ts = -1;
    opt.op_mutex_op_fact = -1;
    opt.op_mutex_hm_op = -1;
    opt.hm = 2;
    opt.hm_time_limit = 0;
    opt.hm_excess_mem = 0;

    pddl_cfg.force_adl = 1;

    optsAddDesc("help", 'h', OPTS_NONE, &opt.help, NULL,
                "Print this help.");
    optsAddDesc("output", 'o', OPTS_STR, &opt.op_mutex_out, NULL,
                "Output filename (default: no output)");

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
    optsAddDesc("fam-lmg", 0x0, OPTS_NONE, &opt.fam_lmg, NULL,
                "Use grounded lifted mutex groups as initialization for"
                " fam-group. (default: off)");
    optsAddDesc("h2mg", 0x0, OPTS_NONE, &opt.h2_mgroup, NULL,
                "Infer h^2 based mutex groups. (default: off)");

    optsAddDesc("hm", 0x0, OPTS_INT, &opt.hm, NULL,
                "Infer mutexes from h^m as input to op-mutex inference"
                " methods. Values <=1 disable h^m. (default: 2)");
    optsAddDesc("hm-time-limit", 0x0, OPTS_FLOAT, &opt.hm_time_limit, NULL,
                "Time limit in seconds for the h^m method."
                " (default: 0 == disabled)");
    optsAddDesc("hm-excess-mem", 0x0, OPTS_INT, &opt.hm_excess_mem, NULL,
                "Excess memory in MB for the h^m method."
                " (default: 0 == disabled)");
    optsAddDesc("opm-ts", 0x0, OPTS_INT, &opt.op_mutex_ts, NULL,
                "Infer op-mutexes using abstractions. (default: off)");
    optsAddDesc("opm-op-fact", 0x0, OPTS_INT, &opt.op_mutex_op_fact, NULL,
                "Infer op-mutexes using op-fact compilation. (default: off)");
    optsAddDesc("opm-hm-op", 0x0, OPTS_INT, &opt.op_mutex_hm_op, NULL,
                "Infer op-mutexes using h^m from each operator. (default: off)");

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

        fprintf(stderr, "pddl-op-mutex is a program for generating operator"
                        " mutexes.\n");
        fprintf(stderr, "Usage: %s [OPTIONS] domain.pddl problem.pddl\n"
                        "       %s [OPTIONS] problem.pddl\n"
                        "       %s [OPTIONS] problem\n",
                argv[0], argv[0], argv[0]);
        fprintf(stderr, "  OPTIONS:\n");
        optsPrint(stderr, "    ");
        fprintf(stderr, "\n");
        return -1;
    }

    if (opt.op_mutex_ts <= 0
            && opt.op_mutex_op_fact <= 1
            && opt.op_mutex_hm_op <= 1){
        fprintf(stderr, "Error: One of --opm-ts/--opm-op-fact/--opm-hm-op"
                        " must be specified\n");
        return -1;
    }

    if (opt.fam && opt.h2_mgroup){
        fprintf(stderr, "Error: --fam and --h2mg cannot be used together.\n");
        return -1;
    }

    if (opt.fam_lmg && !opt.fam){
        fprintf(stderr, "Error: --fam-lmg has no effect: use --fam too \n");
        return -1;
    }

    if (opt.no_ground_prune_pre && opt.no_ground_prune_dead_end)
        opt.no_ground_prune = 1;

    if (*argc == 2){
        BOR_INFO(&err, "Input file: '%s'", argv[1]);
        if (pddlFiles1(&files, argv[1], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }else{ // *argc == 3
        BOR_INFO(&err, "Input files: '%s' and '%s'", argv[1], argv[2]);
        if (pddlFiles(&files, argv[1], argv[2], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }

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

    pddlMGroupsRemoveSubsets(&mgroups);
    pddlMGroupsRemoveSmall(&mgroups, 1);
    pddlMGroupsSortUniq(&mgroups);
    BOR_INFO(&err, "Found %d maximal non-unit mutex groups, %d exactly-one",
             mgroups.mgroup_size,
             pddlMGroupsNumExactlyOne(&mgroups));

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
        if (pddlH2(&strips, &mutex, NULL, NULL, 0., &err) != 0){
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
        pddlMGroupsRemoveSubsets(&mgroups);
        pddlMGroupsRemoveSmall(&mgroups, 1);
        pddlMGroupsSortUniq(&mgroups);
    }
}

static int pruneStripsFixpointH2FwBw(void)
{
    if (strips.has_cond_eff){
        BOR_INFO2(&err, "h^2 disabled because the problem has"
                        " conditional effects.");
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Fixpoint pruning using h^2 fw/bw...");

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
        BOR_INFO(&err, "  MG-Strips created for h^2 fw/bw with %d facts,"
                       " %d ops, and %d mgroups",
                mg_strips.strips.fact.fact_size,
                mg_strips.strips.op.op_size,
                mg_strips.mg.mgroup_size);
        pddlMutexPairsFree(&mutex);
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &mutex,
                       &rm_fact, &rm_op, 0., &err) != 0){
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
    BOR_INFO2(&err, "Fixpoint pruning h^2 fw/bw DONE.");
    fflush(stdout);
    fflush(stderr);
    return 0;
}


static int mgroupsAndPruning(void)
{
    if (pruneStripsFixpointH2FwBw() != 0)
        return -1;
    if (inferMutexGroups() != 0)
        return -1;
    return 0;
}

static int inferOpMutex(void)
{
    pddl_mg_strips_t mg_strips;
    pddlMGStripsInit(&mg_strips, &strips, &mgroups);
    BOR_INFO(&err, "Created MG-Strips with %d facts, %d ops, %d mgroups,"
                   " input mgroups: %d",
             mg_strips.strips.fact.fact_size,
             mg_strips.strips.op.op_size,
             mg_strips.mg.mgroup_size,
             mgroups.mgroup_size);

    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitStrips(&mutex, &mg_strips.strips);
    pddlMutexPairsAddMGroups(&mutex, &mg_strips.mg);
    if (opt.hm >= 2){
        size_t excess_mem = 1024L * 1024L * opt.hm_excess_mem;
        if (pddlHm(opt.hm, &mg_strips.strips, &mutex, NULL, NULL,
                   opt.hm_time_limit, excess_mem, &err) != 0){
            BOR_INFO2(&err, "h^2 failed.");
            BOR_TRACE_RET(&err, -1);
        }
    }

    pddl_op_mutex_pairs_t opm;
    pddlOpMutexPairsInit(&opm, &mg_strips.strips);
    int ret;
    size_t max_mem = 0;
    if (opt.op_mutex_ts > 0){
        ret = pddlOpMutexInferTransSystems(&opm, &mg_strips, &mutex,
                                           opt.op_mutex_ts, max_mem, 1, &err);
        if (ret < 0)
            BOR_TRACE_RET(&err, ret);
    }

    if (opt.op_mutex_op_fact > 1){
        ret = pddlOpMutexInferHmOpFactCompilation(&opm, opt.op_mutex_op_fact,
                                                  &mg_strips.strips, &err);
        if (ret < 0)
            BOR_TRACE_RET(&err, ret);
    }

    if (opt.op_mutex_hm_op > 1){
        ret = pddlOpMutexInferHmFromEachOp(&opm, opt.op_mutex_hm_op,
                                           &mg_strips.strips, &mutex,
                                           NULL, &err);
        if (ret < 0)
            BOR_TRACE_RET(&err, ret);
    }

    if (opt.op_mutex_out != NULL){
        FILE *fout = openFile(opt.op_mutex_out);
        if (fout == NULL){
            pddlOpMutexPairsFree(&opm);
            pddlMGStripsFree(&mg_strips);
            BOR_ERR_RET(&err, -1, "Could not open file '%s'\n",
                        opt.op_mutex_out);
        }
        int o1, o2;
        PDDL_OP_MUTEX_PAIRS_FOR_EACH(&opm, o1, o2)
            fprintf(fout, "%d %d\n", o1, o2);
        closeFile(fout);
    }

    pddlOpMutexPairsFree(&opm);
    pddlMutexPairsFree(&mutex);
    pddlMGStripsFree(&mg_strips);
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
            || inferOpMutex() != 0){
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
    return 0;
}
