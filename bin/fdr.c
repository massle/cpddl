#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

struct options {
    int help;
    int max_mem;
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
    float fam_max_time;
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

    unsigned fdr_var_method;

    int endomorphism_fdr;
    int endomorphism_mg_strips;
    int endomorphism_ts;
    int endomorphism_fdr_ts;

    int num_sym_gen;

    const char *fdr_out;
    const char *lifted_mgroup_out;
    const char *mgroup_out;
    const char *mgroup_pre_out;

    int op_mutex_ts;
    int op_mutex_op_fact;
    int op_mutex_hm_op;
    int op_mutex_prune;
    const char *op_mutex_out;

    int pot;

    int mgroups_split_invertible;
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
unsigned fdr_var_flag = PDDL_FDR_VARS_ESSENTIAL_FIRST;
unsigned fdr_flag = 0u;
pddl_endomorphism_config_t endomorphism_cfg = PDDL_ENDOMORPHISM_CONFIG_INIT;
pddl_hpot_config_t pot_cfg = PDDL_HPOT_CONFIG_INIT;

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

static void usage(const char *bin)
{
    fprintf(stderr, "pddl-fdr is a program for translating PDDL into"
            " FDR.\n");
    fprintf(stderr, "Usage: %s [OPTIONS] domain.pddl problem.pddl\n", bin);
    fprintf(stderr, "  OPTIONS:\n");
    optsPrint(stderr, "    ");
    fprintf(stderr, "\n");
}

static void setFDRVarLargest(const char *ln, const char *sn)
{
    opt.fdr_var_method = PDDL_FDR_VARS_LARGEST_FIRST;
}

static void setFDRVarEssential(const char *ln, const char *sn)
{
    opt.fdr_var_method = PDDL_FDR_VARS_ESSENTIAL_FIRST;
}

static void setFDRVarLargestMulti(const char *ln, const char *sn)
{
    opt.fdr_var_method = PDDL_FDR_VARS_LARGEST_FIRST_MULTI;
}

static int setPot(const char *_spec)
{
    // TODO
    char *spec = BOR_STRDUP(_spec);
    if (spec == NULL)
        return -1;
    const char *end = spec + strlen(spec);
    char *o = spec;
    while (o <= end){
        char *e;
        for (e = o; *e != 0x0 && *e != ':'; ++e);
        *e = 0x0;

        if (strcmp(o, "disamb") == 0){
            pot_cfg.disambiguation = 1;
            pot_cfg.weak_disambiguation = 0;

        }else if (strcmp(o, "weak-disamb") == 0){
            pot_cfg.disambiguation = 0;
            pot_cfg.weak_disambiguation = 1;

        }else if (strcmp(o, "no-disamb") == 0){
            pot_cfg.disambiguation = 0;
            pot_cfg.weak_disambiguation = 0;

        }else if (strcmp(o, "init") == 0){
            pot_cfg.obj = PDDL_HPOT_OBJ_INIT;
            pot_cfg.add_init_constr = 0;
            pot_cfg.init_constr_coef = 0;

        }else if (strcmp(o, "all") == 0){
            pot_cfg.obj = PDDL_HPOT_OBJ_ALL_STATES;

        }else if (strcmp(o, "Max(init,all)") == 0){
            pot_cfg.obj = PDDL_HPOT_OBJ_MAX_INIT_ALL_STATES;

        }else if (strcmp(o, "+init") == 0){
            pot_cfg.add_init_constr = 1;
            pot_cfg.init_constr_coef = 1;

        }else if (strcmp(o, "-init") == 0){
            pot_cfg.add_init_constr = 0;
            pot_cfg.init_constr_coef = 0;

        }else{
            fprintf(stderr, "Error: Unknown pot specification: '%s'\n", o);
            fprintf(stderr, "\n");
            return -1;
        }

        o = e + 1;
    }

    if (spec != NULL)
        BOR_FREE(spec);

    opt.pot = 1;
    return 0;
}

static const char *potObjName(int obj)
{
    switch(obj){
        case PDDL_HPOT_OBJ_INIT:
            return "init";
        case PDDL_HPOT_OBJ_ALL_STATES:
            return "all";
        case PDDL_HPOT_OBJ_SAMPLES_MAX:
            return "samples-max";
        case PDDL_HPOT_OBJ_SAMPLES_SUM:
            return "samples-sum";
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX:
            return "all-states-mutex";
        case PDDL_HPOT_OBJ_DIVERSE:
            return "diverse";
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED:
            return "all-states-mutex-cond";
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND:
            return "all-states-mutex-cond-rand";
        case PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND2:
            return "all-states-mutex-cond-rand2";
        case PDDL_HPOT_OBJ_MAX_INIT_ALL_STATES:
            return "Max(init,all)";
    }
    return "unknown";
}

static int readOpts(int *argc, char *argv[])
{
    const char *pot_spec = NULL;
    bzero(&opt, sizeof(opt));
    opt.lifted_mgroup_max_candidates = 10000;
    opt.lifted_mgroup_max_mgroups = 10000;
    opt.fdr_out = "-";
    opt.fdr_var_method = PDDL_FDR_VARS_LARGEST_FIRST;
    opt.fam_max_time = -1.;
    opt.op_mutex_ts = -1;
    opt.op_mutex_op_fact = -1;
    opt.op_mutex_hm_op = -1;

    pddl_cfg.force_adl = 1;
    endomorphism_cfg.num_threads = 1;
    endomorphism_cfg.run_in_subprocess = 1;

    optsAddDesc("help", 'h', OPTS_NONE, &opt.help, NULL,
                "Print this help.");
    optsAddDesc("max-mem", 'm', OPTS_INT, &opt.max_mem, NULL,
                "Maximum memory in MB (default: 0, i.e., no limit)");
    optsAddDesc("output", 'o', OPTS_STR, &opt.fdr_out, NULL,
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
    optsAddDesc("lmg-out", 0x0, OPTS_STR, &opt.lifted_mgroup_out, NULL,
                "Output filename for infered lifted mutex groups."
                " (default: none)");

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
    optsAddDesc("fam-max-time", 0x0, OPTS_FLOAT, &opt.fam_max_time, NULL,
                "Maximum time for inference of fam-groups. (default: off)");
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

    optsAddDesc("mg-out", 0x0, OPTS_STR, &opt.mgroup_out, NULL,
                "Output filename for found mutex groups (after pruning)."
                " (default: none)");
    optsAddDesc("mg-pre-out", 0x0, OPTS_STR, &opt.mgroup_pre_out, NULL,
                "Output filename for the mutex groups found before pruning."
                " (default: none)");

    optsAddDesc("var-largest", 0x0, OPTS_NONE, NULL,
                OPTS_CB(setFDRVarLargest),
                "Allocate FDR variables with largest-first algorithm"
                " (default).");
    optsAddDesc("var-essential", 0x0, OPTS_NONE, NULL,
                OPTS_CB(setFDRVarEssential),
                "Allocate FDR variables with the essential-first algorithm.");
    optsAddDesc("var-largest-multi", 0x0, OPTS_NONE, NULL,
                OPTS_CB(setFDRVarLargestMulti),
                "Allocate FDR variables with largest-first algorithm and"
                " encode one strips fact as multiple fdr values if in more"
                " mutex groups.");

    optsAddDesc("em-fdr", 0x0, OPTS_NONE, &opt.endomorphism_fdr, NULL,
                "Prune operators with endomorphism on FDR. (default: off)");
    optsAddDesc("em-mg-strips", 0x0, OPTS_NONE, &opt.endomorphism_mg_strips,
                NULL,
                "Prune operators with endomorphism on MG-Strips."
                " (default: off)");
    optsAddDesc("em-ts", 0x0, OPTS_NONE, &opt.endomorphism_ts, NULL,
                "Prune operators with endomorphism on factored transition"
                " system. (default: off)");
    optsAddDesc("em-fdr-ts", 0x0, OPTS_NONE, &opt.endomorphism_fdr_ts, NULL,
                "Endomorphism first on FDR and then on factored transition"
                " system. If the inference on FDR fails (because of memory"
                " or time limit) the inference of TS is skipped."
                " (default: off)");
    optsAddDesc("em-max-time", 0x0, OPTS_FLOAT,
                &endomorphism_cfg.max_time, NULL,
                "Maximum overall time in seconds for the endomorphism"
                " inference. (default: 3600.)");
    optsAddDesc("em-max-search-time", 0x0, OPTS_FLOAT,
                &endomorphism_cfg.max_search_time, NULL,
                "Maximum search time in seconds for the endomorphism"
                " inference. (default: 3600.)");

    optsAddDesc("num-sym-gen", 0x0, OPTS_NONE, &opt.num_sym_gen, NULL,
                "Print number of symmetry generators inferred on PDG."
                " (default: off)");

    optsAddDesc("opm-ts", 0x0, OPTS_INT, &opt.op_mutex_ts, NULL,
                "Infer op-mutexes using abstractions. (default: off)");
    optsAddDesc("opm-op-fact", 0x0, OPTS_INT, &opt.op_mutex_op_fact, NULL,
                "Infer op-mutexes using op-fact compilation. (default: off)");
    optsAddDesc("opm-hm-op", 0x0, OPTS_INT, &opt.op_mutex_hm_op, NULL,
                "Infer op-mutexes using h^m from each operator. (default: off)");
    optsAddDesc("opm-prune", 0x0, OPTS_NONE, &opt.op_mutex_prune, NULL,
                "Enable pruning using inferred op-mutexes and symmetries."
                " (default: off)");
    optsAddDesc("opm-out", 0x0, OPTS_STR, &opt.op_mutex_out, NULL,
                "Output filename for op-mutexes (default: no output)");

    optsAddDesc("pot", 0x0, OPTS_NONE, &opt.pot, NULL,
                "Shorthand for --pot-spec 'disamb:all:+init'");
    optsAddDesc("pot-spec", 0x0, OPTS_STR, &pot_spec, NULL,
                "Generate potentials according to the specification."
                " TODO");

    optsAddDesc("mgroups-split-invertible", 0x0, OPTS_NONE,
                &opt.mgroups_split_invertible, NULL,
                "Split mutex groups using invertible facts. (default: off)");

    if (opts(argc, argv) != 0 || opt.help || (*argc != 3 && *argc != 2)){
        if (*argc <= 1){
            fprintf(stderr, "Error: Missing input file(s)\n\n");
            fprintf(stderr, "\n");
        }

        if (*argc > 3){
            for (int i = 0; i < *argc; ++i){
                if (argv[i][0] == '-'){
                    fprintf(stderr, "Error: Unrecognized option '%s'\n",
                            argv[i]);
                    fprintf(stderr, "\n");
                }
            }
        }
        usage(argv[0]);
        return -1;
    }

    if (pot_spec != NULL && setPot(pot_spec) != 0){
        usage(argv[0]);
        return -1;
    }


    if (opt.fam_fixpoint_no_de)
        opt.fam_fixpoint = 1;

    if (opt.fam && opt.h2_mgroup){
        fprintf(stderr, "Error: --fam and --h2mg cannot be used together.\n");
        fprintf(stderr, "\n");
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
        fprintf(stderr, "\n");
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

    if (opt.max_mem > 0){
        struct rlimit mem_limit;
        mem_limit.rlim_cur
            = mem_limit.rlim_max = opt.max_mem * 1024UL * 1024UL;
        setrlimit(RLIMIT_AS, &mem_limit);
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

    if (opt.lifted_mgroup_out != NULL){
        FILE *fout = openFile(opt.lifted_mgroup_out);
        if (fout == NULL){
            fprintf(stderr, "Error: Could not open '%s'\n",
                    opt.lifted_mgroup_out);
            return -1;
        }
        BOR_INFO(&err, "Printing lifted mutex groups to '%s'",
                 opt.lifted_mgroup_out);
        pddlLiftedMGroupsPrint(&pddl, &lifted_mgroups, fout);
        closeFile(fout);
    }

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

    if (opt.mgroup_pre_out != NULL){
        FILE *fout = openFile(opt.mgroup_pre_out);
        if (fout == NULL){
            fprintf(stderr, "Error: Could not open '%s'\n", opt.mgroup_pre_out);
            return -1;
        }
        BOR_INFO(&err, "Printing mutex groups to '%s'", opt.mgroup_pre_out);
        pddlMGroupsPrint(&pddl, &strips, &mgroups, fout);
        closeFile(fout);
    }

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
        cfg.time_limit = opt.fam_max_time;
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
    }
}

static void deduplicateOps(void)
{
    int num_ops = strips.op.op_size;
    pddlStripsOpsDeduplicate(&strips.op);
    BOR_INFO(&err, "Deduplication of operators removed %d operators",
             num_ops - strips.op.op_size);
}

static int pruneEndomorphismFDR(const pddl_endomorphism_config_t *cfg,
                                bor_iset_t *redundant_op)
{
    int ret = 0;
    BOR_INFO2(&err, "Redundant operators using endomorphism on FDR ...");
    pddl_fdr_t fdr;
    pddlFDRInitFromStrips(&fdr, &strips, &mgroups, &mutex,
                          fdr_var_flag, 0, &err);
    ret = pddlEndomorphismFDRRedundantOps(&fdr, cfg, redundant_op, &err);
    pddlFDRFree(&fdr);
    BOR_INFO2(&err, "Redundant operators using endomorphism on FDR DONE");
    return ret;
}

static int pruneEndomorphismMGStrips(const pddl_endomorphism_config_t *cfg,
                                     bor_iset_t *redundant_op)
{
    int ret = 0;
    BOR_INFO2(&err, "Redundant operators using endomorphism on MG-Strips ...");
    pddl_mg_strips_t mg_strips;
    pddlMGStripsInit(&mg_strips, &strips, &mgroups);
    ret = pddlEndomorphismMGStripsRedundantOps(&mg_strips, cfg, redundant_op,
                                               &err);
    pddlMGStripsFree(&mg_strips);
    BOR_INFO2(&err, "Redundant operators using endomorphism on MG-Strips DONE");
    return ret;
}

static int pruneEndomorphismTS(const pddl_endomorphism_config_t *cfg,
                               bor_iset_t *redundant_op)
{
    int ret = 0;
    BOR_INFO2(&err, "Redundant operators using endomorphism on TSs ...");
    pddl_mg_strips_t mg_strips;
    pddlMGStripsInit(&mg_strips, &strips, &mgroups);

    pddl_trans_systems_t tss;
    pddl_mutex_pairs_t mg_mutex;
    pddlMutexPairsInitStrips(&mg_mutex, &mg_strips.strips);
    pddlMutexPairsAddMGroups(&mg_mutex, &mg_strips.mg);
    pddlH2(&mg_strips.strips, &mg_mutex, NULL, NULL, 0., &err);
    pddlTransSystemsInit(&tss, &mg_strips, &mg_mutex);
    ret = pddlEndomorphismTransSystemRedundantOps(&tss, cfg, redundant_op,
                                                  &err);
    pddlTransSystemsFree(&tss);
    pddlMutexPairsFree(&mg_mutex);
    pddlMGStripsFree(&mg_strips);
    BOR_INFO2(&err, "Redundant operators using endomorphism on TSs DONE");
    return ret;
}

static int pruneEndomorphismFDRTS(const pddl_endomorphism_config_t *cfg,
                                  bor_iset_t *redundant_op)
{
    int ret = pruneEndomorphismFDR(cfg, redundant_op);

    if (ret == 0){
        BOR_ISET(redundant2);
        int ret2 = pruneEndomorphismTS(cfg, &redundant2);
        if (ret2 == 0
                && borISetSize(&redundant2) > borISetSize(redundant_op)){
            borISetEmpty(redundant_op);
            borISetUnion(redundant_op, &redundant2);
        }
        borISetFree(&redundant2);
    }else{
        BOR_INFO2(&err, "Endomorphism on factored TS skipped, because"
                        " endomorphism on FDR failed");
    }

    return ret;
}

static void _pruneEndomorphism(int (*f)(const pddl_endomorphism_config_t *cfg,
                                        bor_iset_t *redundant_op))
{
    BOR_ISET(redundant_op);
    BOR_ISET(_rm_fact);
    int num_ops = strips.op.op_size;
    f(&endomorphism_cfg, &redundant_op);
    if (borISetSize(&redundant_op) > 0)
        reduceStrips(&_rm_fact, &redundant_op);
    int removed = num_ops - strips.op.op_size;
    BOR_INFO(&err, "Removed %d endomorphism redundant operators, remain %d"
                   " operators",
             removed, strips.op.op_size);
    borISetFree(&redundant_op);
}

static void pruneEndomorphism(void)
{
    if (strips.op.op_size == 0)
        return;

    if (opt.endomorphism_ts)
        _pruneEndomorphism(pruneEndomorphismTS);
    if (opt.endomorphism_fdr)
        _pruneEndomorphism(pruneEndomorphismFDR);
    if (opt.endomorphism_mg_strips)
        _pruneEndomorphism(pruneEndomorphismMGStrips);
    if (opt.endomorphism_fdr_ts)
        _pruneEndomorphism(pruneEndomorphismFDRTS);
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
        cfg.time_limit = opt.fam_max_time;
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

        pruneEndomorphism();
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

    deduplicateOps();

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
        if (pddlH2(&strips, &mutex, &rm_fact, &rm_op, 0., &err) != 0){
            BOR_INFO2(&err, "h^2 fw failed.");
            BOR_TRACE_RET(&err, -1);
        }

        reduceStrips(&rm_fact, &rm_op);

        pruneEndomorphism();
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

    deduplicateOps();

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
        if (pddlH2(&strips, &mutex, &rm_fact, &rm_op, 0., &err) != 0){
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
        cfg.time_limit = opt.fam_max_time;
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

        pruneEndomorphism();
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

    deduplicateOps();

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
        cfg.time_limit = opt.fam_max_time;
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
                       &rm_fact, &rm_op, 0., &err) != 0){
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

        pruneEndomorphism();

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

    deduplicateOps();

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
                       &rm_fact, &rm_op, 0., &err) != 0){
            BOR_INFO2(&err, "h^2 fw/bw failed.");
            BOR_TRACE_RET(&err, -1);
        }
        pddlMGStripsFree(&mg_strips);
        reduceStrips(&rm_fact, &rm_op);

        pruneEndomorphism();

    } while (strips.op.op_size != orig_op_size
                || strips.fact.fact_size != orig_fact_size);

    borISetEmpty(&rm_fact);
    borISetEmpty(&rm_op);
    pddlUnreachableInMGroupsDTGs(&strips, &mgroups, &rm_fact, &rm_op, &err);
    reduceStrips(&rm_fact, &rm_op);

    borISetFree(&rm_fact);
    borISetFree(&rm_op);

    deduplicateOps();

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
            if (pddlH2(&strips, &mutex, &rm_fact, &rm_op, 0., &err) != 0){
                BOR_INFO2(&err, "h^2 fw failed.");
                BOR_TRACE_RET(&err, -1);
            }
        }else if (!opt.no_h2){
            pddl_mg_strips_t mg_strips;
            pddlMGStripsInit(&mg_strips, &strips, &mgroups);
            if (pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &mutex,
                        &rm_fact, &rm_op, 0., &err) != 0){
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

    deduplicateOps();

    pruneEndomorphism();

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

    if (opt.mgroup_out != NULL){
        FILE *fout = openFile(opt.mgroup_out);
        if (fout == NULL){
            fprintf(stderr, "Error: Could not open '%s'\n", opt.mgroup_out);
            return -1;
        }
        BOR_INFO(&err, "Printing mutex groups to '%s'", opt.mgroup_out);
        pddlMGroupsPrint(&pddl, &strips, &mgroups, fout);
        closeFile(fout);
    }

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

static int opMutex(void)
{
    if (opt.op_mutex_ts < 0
            && opt.op_mutex_op_fact < 1
            && opt.op_mutex_hm_op < 1){
        return 0;
    }

    BOR_INFO2(&err, "");
    BOR_INFO(&err, "Operator Mutexes [ts: %d, op-fact: %d, hm-op: %d,"
                   " prune: %d, output: '%s']",
             opt.op_mutex_ts,
             opt.op_mutex_op_fact,
             opt.op_mutex_hm_op,
             opt.op_mutex_prune,
             (opt.op_mutex_out == NULL ? "" : opt.op_mutex_out));

    pddl_mg_strips_t mg_strips;
    pddlMGStripsInit(&mg_strips, &strips, &mgroups);
    BOR_INFO(&err, "Created MG-Strips with %d facts, %d ops, %d mgroups,"
                   " input mgroups: %d",
             mg_strips.strips.fact.fact_size,
             mg_strips.strips.op.op_size,
             mg_strips.mg.mgroup_size,
             mgroups.mgroup_size);

    pddl_mutex_pairs_t mg_mutex;
    pddlMutexPairsInitStrips(&mg_mutex, &mg_strips.strips);
    pddlMutexPairsAddMGroups(&mg_mutex, &mg_strips.mg);
    pddlH2(&mg_strips.strips, &mg_mutex, NULL, NULL, 0., &err);

    pddl_op_mutex_pairs_t opm;
    pddlOpMutexPairsInit(&opm, &mg_strips.strips);
    int ret;
    size_t max_mem = 0;
    if (opt.op_mutex_ts > 0){
        ret = pddlOpMutexInferTransSystems(&opm, &mg_strips, &mg_mutex,
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
                                           &mg_strips.strips, &mg_mutex,
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

    if (opm.num_op_mutex_pairs > 0
            && opt.op_mutex_prune){
        BOR_INFO2(&err, "Computing symmetries on PDG");
        pddl_strips_sym_t sym;
        pddlStripsSymInitPDG(&sym, &strips);
        BOR_INFO(&err, "  Symmetry generators: %d", sym.gen_size);
        BOR_ISET(redundant);
        pddlOpMutexSymRedundantFixpoint(&redundant, &mg_strips.strips,
                                        &sym, &opm, &err);
        if (borISetSize(&redundant) > 0){
            pddlStripsReduce(&strips, NULL, &redundant);
            BOR_INFO(&err, "Number of Strips Operators: %d",
                     strips.op.op_size);
        }
        borISetFree(&redundant);
        pddlStripsSymFree(&sym);
    }

    pddlOpMutexPairsFree(&opm);
    pddlMutexPairsFree(&mg_mutex);
    pddlMGStripsFree(&mg_strips);
    BOR_INFO2(&err, "Operator Mutexes DONE");
    BOR_INFO2(&err, "");
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

static int toFDR(void)
{
    if (opt.num_sym_gen){
        pddl_strips_sym_t sym;
        pddlStripsSymInitPDG(&sym, &strips);
        BOR_INFO(&err, "Symmetry generators: %d", sym.gen_size);
        pddlStripsSymFree(&sym);
    }

    if (opt.mgroups_split_invertible){
        BOR_INFO2(&err, "Splitting mutex groups using invertible facts...");
        BOR_INFO_PREFIX_PUSH(&err, "Split mgroups: ");
        pddlMGroupsRemoveSmall(&mgroups, 1);
        BOR_INFO(&err, "Number of mutex groups >1 before splitting: %d"
                       " covering %d facts",
                 mgroups.mgroup_size,
                 pddlMGroupsNumCoveredFacts(&mgroups));
        BOR_ISET(unreachable_ops);
        pddlStripsFindUnreachableOps(&strips, &mutex, &unreachable_ops, &err);
        pddlStripsReduce(&strips, NULL, &unreachable_ops);
        pddlStripsRemoveUselessDelEffs(&strips, &mutex, NULL, &err);
        borISetFree(&unreachable_ops);

        BOR_ISET(invertible_facts);
        pddlRSEInvertibleFacts(&strips, &mgroups, &invertible_facts, &err);
        BOR_INFO(&err, "Found %d invertible facts",
                 borISetSize(&invertible_facts));
        if (borISetSize(&invertible_facts) > 0){
            pddl_mgroups_t mgs;
            pddlMGroupsSplitByIntersection(&mgs, &mgroups, &invertible_facts);
            pddlMGroupsFree(&mgroups);
            mgroups = mgs;
            pddlMGroupsRemoveSmall(&mgroups, 1);
            BOR_INFO(&err, "Number of mutex groups >1 after splitting: %d"
                           " covering %d facts",
                     mgroups.mgroup_size,
                     pddlMGroupsNumCoveredFacts(&mgroups));
        }
        borISetFree(&invertible_facts);
        BOR_INFO_PREFIX_POP(&err);
    }

    BOR_INFO2(&err, "");
    BOR_INFO2(&err, "Translating to FDR ...");
    BOR_INFO(&err, "Output file: '%s'", opt.fdr_out);

    fdr_var_flag = opt.fdr_var_method;
    FILE *fout = openFile(opt.fdr_out);
    if (fout == NULL){
        fprintf(stderr, "Error: Could not open file '%s'\n", opt.fdr_out);
        return -1;
    }

    pddl_fdr_t fdr;
    pddl_black_mgroups_config_t black_mg_cfg = PDDL_BLACK_MGROUPS_CONFIG_INIT;
    pddlBlackFDRInitFromStrips(&fdr, &strips, &mgroups, &mutex,
                               &black_mg_cfg, &err);
    /* TODO */
    pddlFDRInitFromStrips(&fdr, &strips, &mgroups, &mutex, fdr_var_flag,
                          fdr_flag, &err);
    pddlFDRPrintFD(&fdr, &mgroups, 1, fout);

    if (opt.pot){
        BOR_INFO2(&err, "");
        BOR_INFO(&err, "Potential heuristics [disamb: %d, weak-diamb: %d,"
                       " obj: %s(%x), add-init-constr: %d,"
                       " init-constr-coef: %.2f, num-samples: %d,"
                       " samples-use-mutex: %d, samples-random-walk: %d,"
                       " all-states-mutex-size: %d]",
                 pot_cfg.disambiguation,
                 pot_cfg.weak_disambiguation,
                 potObjName(pot_cfg.obj),
                 pot_cfg.obj,
                 pot_cfg.add_init_constr,
                 pot_cfg.init_constr_coef,
                 pot_cfg.num_samples,
                 pot_cfg.samples_use_mutex,
                 pot_cfg.samples_random_walk,
                 pot_cfg.all_states_mutex_size);
        BOR_INFO_PREFIX_PUSH(&err, "Pot: ");
        pddl_hpot_t hpot;
        if (pddlHPotInit(&hpot, &fdr, &pot_cfg, &err) != 0){
            BOR_INFO2(&err, "Cannot find potential heuristic");
            BOR_INFO_PREFIX_POP(&err);
            return -1;
        }
        int est = pddlHPotFDRStateEstimate(&hpot, &fdr.var, fdr.init);
        BOR_INFO(&err, "Init state estimate: %d", est);
        printPotentials(&fdr, &hpot, fout);
        pddlHPotFree(&hpot);
        BOR_INFO_PREFIX_POP(&err);
    }

    pddlFDRFree(&fdr);

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
            || opMutex() != 0
            || toFDR() != 0){
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



