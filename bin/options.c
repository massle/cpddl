#include <sys/time.h>
#include <sys/resource.h>
#include "options.h"
#include "opts.h"

options_t opt = { 0 };

static int optSetGround(const char *tag)
{
    if (strcmp(tag, "default") == 0){
        opt.ground.method_fn = pddlStripsGround;

    }else if (strcmp(tag, "sql") == 0){
        opt.ground.method_fn = pddlStripsGroundSql;

    }else if (strcmp(tag, "dl") == 0){
        opt.ground.method_fn = pddlStripsGroundDatalog;

    }else if (strcmp(tag, "prune-pre") == 0){
        opt.ground.cfg.prune_op_pre_mutex = 1;

    }else if (strcmp(tag, "prune-dead-end") == 0){
        opt.ground.cfg.prune_op_dead_end = 1;

    }else if (strcmp(tag, "prune-all") == 0){
        opt.ground.cfg.prune_op_pre_mutex = 1;
        opt.ground.cfg.prune_op_dead_end = 1;

    }else{
        fprintf(stderr, "Error: Unknown ground config tag '%s'\n", tag);
        return -1;
    }
    return 0;
}

static int optSetMGroup(const char *tag)
{
    if (strcmp(tag, "0") == 0
            || strcmp(tag, "n") == 0
            || strcmp(tag, "none") == 0){
        opt.mg.fam = 0;
        opt.mg.h2 = 0;

    }else if (strcmp(tag, "fam") == 0){
        opt.mg.fam = 1;
        opt.mg.h2 = 0;

    }else if (strcmp(tag, "h2") == 0){
        opt.mg.fam = 0;
        opt.mg.h2 = 1;

    }else{
        fprintf(stderr, "Error: Unknown mgroup config tag '%s'\n", tag);
        return -1;
    }
    return 0;
}

static int optProcessStrips(const char *tag)
{
    char *s = PDDL_STRDUP(tag);
    char *next = s;
    char *cur;
    if ((cur = strsep(&next, ",")) != NULL){
        if (strcmp(cur, "irr") == 0 || strcmp(cur, "irrelevance") == 0){
            pddlProcessStripsAddIrrelevance(&opt.strips.process);
            if (next != NULL){
                fprintf(stderr, "Error: Invalid argument '%s'\n", next);
                return -1;
            }

        }else if (strcmp(cur, "fam-dead-end") == 0){
            pddlProcessStripsAddFAMGroupsDeadEndOps(&opt.strips.process);
            if (next != NULL){
                fprintf(stderr, "Error: Invalid argument '%s'\n", next);
                return -1;
            }

        }else if (strcmp(cur, "dedup") == 0){
            pddlProcessStripsAddDeduplicateOps(&opt.strips.process);
            if (next != NULL){
                fprintf(stderr, "Error: Invalid argument '%s'\n", next);
                return -1;
            }

        }else if (strcmp(cur, "endo") == 0
                    || strcmp(cur, "endomorph") == 0
                    || strcmp(cur, "endomorphism") == 0){
            // TODO

        }else if (strcmp(cur, "opm") == 0 || strcmp(cur, "op-mutex") == 0){
            // TODO

        }else if (strcmp(cur, "h2fw") == 0){
            float time_limit = 0.f;
            while ((cur = strsep(&next, ",")) != NULL){
                if (strncmp(cur, "time=", 5) == 0){
                    time_limit = strtof(cur + 5, NULL);
                }else{
                    fprintf(stderr, "Error: Invalid argument '%s'\n", cur);
                    return -1;
                }
            }
            pddlProcessStripsAddH2Fw(&opt.strips.process, time_limit);

        }else if (strcmp(cur, "h2fwbw") == 0){
            float time_limit = 0.f;
            while ((cur = strsep(&next, ",")) != NULL){
                if (strncmp(cur, "time=", 5) == 0){
                    time_limit = strtof(cur + 5, NULL);
                }else{
                    fprintf(stderr, "Error: Invalid argument '%s'\n", cur);
                    return -1;
                }
            }
            pddlProcessStripsAddH2FwBw(&opt.strips.process, time_limit);

        }else if (strcmp(cur, "h3fw") == 0){
            float time_limit = 0.f;
            size_t excess_mem = 0;
            while ((cur = strsep(&next, ",")) != NULL){
                if (strncmp(cur, "time=", 5) == 0){
                    time_limit = strtof(cur + 5, NULL);
                }else if (strncmp(cur, "excess-mem=", 11) == 0){
                    excess_mem = strtol(cur + 11, NULL, 10);
                }else{
                    fprintf(stderr, "Error: Invalid argument '%s'\n", cur);
                    return -1;
                }
            }
            pddlProcessStripsAddH3Fw(&opt.strips.process, time_limit, excess_mem);

        }else{
            fprintf(stderr, "Error: Invalid argument '%s'\n", cur);
            return -1;
        }
    }
    PDDL_FREE(s);
    return 0;
}

static int optProcessStripsH2(int enabled)
{
    return optsProcessTags("irr:fam-dead-end:h2fwbw:irr:dedup", optProcessStrips);
}

static int optFDRLargestFirst(int enabled)
{
    opt.fdr.var_flag = PDDL_FDR_VARS_LARGEST_FIRST;
    return 0;
}

static int optFDREssentialFirst(int enabled)
{
    opt.fdr.var_flag = PDDL_FDR_VARS_ESSENTIAL_FIRST;
    return 0;
}

static int optLiftedPlanner(const char *tag)
{
    if (strcmp(tag, "astar") == 0){
        opt.lifted_planner.enable = 1;
        opt.lifted_planner.search_fn = pddlSearchLiftedAStar;

    }else if (strcmp(tag, "gbfs") == 0){
        opt.lifted_planner.enable = 1;
        opt.lifted_planner.search_fn = pddlSearchLiftedGBFS;

    }else if (strcmp(tag, "lazy") == 0){
        opt.lifted_planner.enable = 1;
        opt.lifted_planner.search_fn = pddlSearchLiftedLazy;

    }else{
        fprintf(stderr, "Error: Unknown --lplan option '%s'\n", tag);
        return -1;
    }
    return 0;
}

static int optLiftedPlannerHeur(const char *tag)
{
    if (strcmp(tag, "lmc") == 0){
        opt.lifted_planner.heur_fn = pddlHomomorphismHeurLMCut;

    }else if (strcmp(tag, "ff") == 0 || strcmp(tag, "hff") == 0){
        opt.lifted_planner.heur_fn = pddlHomomorphismHeurHFF;

    }else if (strcmp(tag, "types") == 0){
        opt.lifted_planner.homomorph_cfg.type = PDDL_HOMOMORPHISM_TYPES;

    }else if (strcmp(tag, "rnd-objs") == 0){
        opt.lifted_planner.homomorph_cfg.type = PDDL_HOMOMORPHISM_RAND_OBJS;

    }else if (strcmp(tag, "gaif") == 0 || strcmp(tag, "gaifman") == 0){
        opt.lifted_planner.homomorph_cfg.type = PDDL_HOMOMORPHISM_GAIFMAN;
    }else if (strcmp(tag, "rpg") == 0){
        opt.lifted_planner.homomorph_cfg.type = PDDL_HOMOMORPHISM_RPG;

    }else if (strcmp(tag, "endomorph") == 0){
        opt.lifted_planner.homomorph_cfg.use_endomorphism = 1;

    }else if (strcmp(tag, "endomorph-ignore-costs") == 0){
        opt.lifted_planner.homomorph_cfg.endomorphism_cfg.ignore_costs = 1;

    }else if (strncmp(tag, "rm-ratio=", 9) == 0){
        opt.lifted_planner.homomorph_cfg.rm_ratio = atof(tag + 9);

    }else if (strncmp(tag, "seed=", 5) == 0){
        opt.lifted_planner.homomorph_cfg.random_seed = atoi(tag + 5);

    }else if (strcmp(tag, "no-goal") == 0){
        opt.lifted_planner.homomorph_cfg.keep_goal_objs = 0;

    }else if (strncmp(tag, "samples=", 8) == 0){
        opt.lifted_planner.homomorph_samples = atoi(tag + 8);

    }else if (strncmp(tag, "rpg-max-depth=", 14) == 0){
        opt.lifted_planner.homomorph_cfg.rpg_max_depth = atoi(tag + 14);

    }else{
        fprintf(stderr, "Error: Unknown --lplan-heur option '%s'\n", tag);
        return -1;
    }
    return 0;
}

static int optGroundPlannerHeur(const char *tag)
{
    if (strcmp(tag, "blind") == 0){
        opt.ground_planner.heur_fn0 = pddlHeurBlind;

    }else if (strncmp(tag, "pot-state", 9) == 0){
        opt.ground_planner.heur_fn2 = pddlHeurPotState;

    }else if (strncmp(tag, "pot", 3) == 0){
        opt.ground_planner.heur_fn_pot = pddlHeurPot;

    }else if (strncmp(tag, "lmc", 3) == 0){
        opt.ground_planner.heur_fn2 = pddlHeurLMCut;

    }else if (strcmp(tag, "hmax") == 0){
        opt.ground_planner.heur_fn2 = pddlHeurHMax;

    }else if (strcmp(tag, "hadd") == 0){
        opt.ground_planner.heur_fn2 = pddlHeurHAdd;

    }else if (strcmp(tag, "hff") == 0){
        opt.ground_planner.heur_fn2 = pddlHeurHFF;

    }else if (strncmp(tag, "flow", 4) == 0){
        opt.ground_planner.heur_fn2 = pddlHeurFlow;

    }else{
        fprintf(stderr, "Error: Unknown --gplan-heur option '%s'\n", tag);
        return -1;
    }
    return 0;
}

int setOptions(int argc, char *argv[], pddl_err_t *err)
{
    opts_params_t *params;

    opt.lifted_planner.search_fn = NULL;
    opt.lifted_planner.heur_fn = NULL;
    pddl_homomorphism_config_t _homomorph_cfg = PDDL_HOMOMORPHISM_CONFIG_INIT;
    opt.lifted_planner.homomorph_cfg = _homomorph_cfg;
    opt.lifted_planner.homomorph_samples = 1;

    pddlProcessStripsInit(&opt.strips.process);

    opt.ground.cfg.lifted_mgroups = NULL;
    opt.ground.cfg.prune_op_pre_mutex = 0;
    opt.ground.cfg.prune_op_dead_end = 0;
    opt.ground.cfg.remove_static_facts = 1;
    opt.ground.method_fn = pddlStripsGround;

    pddl_red_black_fdr_config_t _rb_cfg = PDDL_RED_BLACK_FDR_CONFIG_INIT;
    opt.rb_fdr.cfg = _rb_cfg;

    opt.fdr.var_flag = PDDL_FDR_VARS_LARGEST_FIRST;


    optsAddFlag("help", 'h', &opt.help, 0, "Print this help.");
    optsAddInt("max-mem", 0x0, &opt.max_mem, 0,
               "Maximum memory in MB if >0.");

    optsStartGroup("PDDL:");
    optsAddFlag("force-adl", 0x0, &opt.pddl.force_adl, 1,
                "Force :adl requirement if it is not specified in the"
                " domain file.");
    optsAddFlag("remove-empty-types", 0x0, &opt.pddl.remove_empty_types, 1,
                "Remove empty types");
    optsAddFlag("pddl-ce", 0x0, &opt.pddl.compile_away_cond_eff, 0,
                "Compile away conditional effects on the PDDL level.");

    optsStartGroup("Lifted Mutex Groups:");
    optsAddFlag("lmg", 0x0, &opt.lmg.enable, 1,
                "Enabled inference of lifted mutex groups.");
    optsAddInt("lmg-max-candidates", 0x0, &opt.lmg.max_candidates, 10000,
               "Maximum number of lifted mutex group candidates.");
    optsAddInt("lmg-max-mgroups", 0x0, &opt.lmg.max_mgroups, 10000,
               "Maximum number of lifted mutex group.");
    optsAddFlag("lmg-fd", 0x0, &opt.lmg.fd, 0,
                "Find Fast-Downward type of lifted mutex groups.");
    optsAddFlag("lmg-fd-mono", 0x0, &opt.lmg.fd_monotonicity, 0,
                "Find Fast-Downward monotonicit invariants; implies --lmg-fd.");
    optsAddStr("lmg-out", 0x0, &opt.lmg.out, NULL,
                "Output filename for infered lifted mutex groups.");
    optsAddStr("lmg-fd-mono-out", 0x0, &opt.lmg.fd_monotonicity_out, NULL,
                "Output filename for infered monotonicity invariants.");
    optsAddFlag("lmg-stop", 0x0, &opt.lmg.stop, 0,
                "Stop after inferring lifted mutex groups.");

    optsStartGroup("Lifted Endomorphisms:");
    optsAddFlag("lendo", 0x0, &opt.lifted_endomorph.enable, 0,
                "Enable pruning od PDDL using lifted endomorphisms.");
    optsAddFlag("lendo-ignore-costs", 0x0, &opt.lifted_endomorph.ignore_costs, 0,
                "Ignore costs of actions when inferring lifted endomorphisms.");

    optsStartGroup("Lifted Planner:");
    optsAddTags("lplan", 0x0, NULL, optLiftedPlanner,
                "Enables lifted planner. Possible values: astar, gbfs, lazy");
    optsAddTags("lplan-heur", 0x0, NULL, optLiftedPlannerHeur,
                "Sets up heuristics for lifted planner.\n"
                " TODO: List of options");
    optsAddStr("lplan-out", 0x0, &opt.lifted_planner.plan_out, NULL,
               "Output filename for the found plan.");
    optsAddStr("lplan-o", 0x0, &opt.lifted_planner.plan_out, NULL,
               "Alias for --lplan-out");

    optsStartGroup("Grounding:");
    optsAddTags("ground", 'G', "default:prune-all",
                optSetGround,
                "Grounding method, combination (delimited by ':') of:\n"
                "  default - default grounding method\n"
                "  sql - sqlite-based grounding method\n"
                "  dl - datalog-based grounding method\n"
                // TODO
                "  prune-pre - prune by checking only preconditions\n"
                "  prune-dead-end - prune by checking only dead-ends\n"
                "  prune-all - alias for prune-pre:prune-dead-end\n");
    optsAddFlag("ground-lmg", 0x0, &opt.ground.mgroup, 1,
                "Ground lifted mutex groups.");
    optsAddFlag("ground-lmg-remove-subsets", 0x0,
                &opt.ground.mgroup_remove_subsets, 1,
                "After grounding lifted mutex groups, remove subsets.");
    optsAddStr("ground-mg-out", 0x0, &opt.ground.mgroup_out, NULL,
                "Output filename for grounded mutex groups.");

    optsStartGroup("STRIPS:");
    optsAddFlag("ce", 0x0, &opt.strips.compile_away_cond_eff, 0,
                "Compile away conditional effects on the STRIPS level"
                " (recommended instead of --pddl-ce).");
    optsAddStr("strips-as-py", 0x0, &opt.strips.py_out, NULL,
               "Output filename for STRIPS in python format.");
    optsAddFlag("strips-stop", 0x0, &opt.strips.stop, 0,
                "Stop after grounding to STRIPS.");

    optsStartGroup("Mutex Groups:");
    optsAddTags("mg", 0x0, "none", optSetMGroup,
                "Method for inference of mutex groups, one of:\n"
                "  0/n/none - no mutex groups will be inferred on STRIPS level\n"
                "  fam - fact-alternating mutex groups\n"
                "  h2 - mutex groups from h^2 mutexes\n");
    optsAddStr("mg-out", 0x0, &opt.mg.out, NULL,
                "Output filename for infered mutex groups.");
    optsAddFlag("mg-remove-subsets", 0x0, &opt.mg.remove_subsets, 1,
                "Remove subsets of the inferred mutex groups.");
    optsAddFlag("fam-lmg", 0x0, &opt.mg.fam_lmg, 1,
                "Use lifted mutex groups as initial set for inference of"
                " fam-groups.");
    optsAddFlag("fam-maximal", 0x0, &opt.mg.fam_maximal, 1,
                "Infer only maximal fam-groups"
                " (see also --no-mg-remove-subsets).");
    optsAddFlt("fam-time-limit", 0x0, &opt.mg.fam_time_limit, -1.,
                "Set time limit in seconds for the inference of fam-groups.");
    optsAddInt("fam-limit", 0x0, &opt.mg.fam_limit, -1,
                "Set limit on the number of inferred fam-groups.");

    optsStartGroup("Process STRIPS:");
    optsAddTags("process-strips", 'P', NULL, optProcessStrips,
"(Post-)Process STRIPS. Each option adds a post-processing step:\n"
"  irr/irrelevance - irrelevance analysis\n"
"  fam-dead-end - use fam-groups to remove dead-end operators\n"
"  h2fw - h^2 in forward direction, time=x sets time limit to x seconds\n"
"  h2fwbw - h^2 in forward and backward direction, time=x sets time limit to x seconds\n"
"  h3fw - h^3 in forward direction, time=x sets time limit to x seconds,"
" and excess-mem=x sets excess memory to x MB\n"
);
    optsAddFlagFn("h2", 0x0, optProcessStripsH2,
                  "Alias for -P irr:fam-dead-end:h2fwbw:irr:dedup");


    optsStartGroup("Finite Domain Representation:");
    optsAddFlagFn("fdr-largest", 0x0, optFDRLargestFirst,
                  "Sort FDR variables with largest first.");
    optsAddFlagFn("fdr-essential", 0x0, optFDREssentialFirst,
                  "Sort FDR variables with essential first.");
    optsAddFlagFn("fdr-ess", 0x0, optFDREssentialFirst,
                  "Alias for --fdr-essential.");
    optsAddFlag("fdr-order-vars-cg", 0x0, &opt.fdr.order_vars_cg, 1,
                "Order FDR variables using causal graph.");
    optsAddStr("fdr-out", 'o', &opt.fdr.out, NULL,
               "Output filename for FDR encoding of the task.");
    optsAddFlag("fdr-pretty-print-vars", 0x0, &opt.fdr.pretty_print_vars, 0,
                "Log FDR variables.");
    optsAddFlag("fdr-pretty-print-cg", 0x0, &opt.fdr.pretty_print_cg, 0,
                "Log FDR causal graph.");

    optsStartGroup("Red-Black FDR:");
    optsAddFlag("rb-fdr", 0x0, &opt.rb_fdr.enable, 0,
                "Compute red-black FDR encoding of the task.");
    optsAddInt("rb-fdr-size", 0x0, &opt.rb_fdr.cfg.mgroup.num_solutions, 1,
               "Number of different encodings to compute.");
    optsAddFlag("rb-fdr-relaxed-plan", 0x0,
                &opt.rb_fdr.cfg.mgroup.weight_facts_with_relaxed_plan, 0,
                "Weight facts using relaxed plan.");
    optsAddFlag("rb-fdr-conflicts", 0x0,
                &opt.rb_fdr.cfg.mgroup.weight_facts_with_conflicts, 0,
                "Weight facts with conflicts in relaxed plan.");
    optsAddStr("rb-fdr-out", 0x0, &opt.rb_fdr.out, NULL,
               "Output filename for the red-black FDR task.");

    optsStartGroup("Grounded Planner:");
    params = optsAddParams("gplan", 0x0,
                           "Enables grounded planner."
                           " Possible values: astar, gbfs, lazy");
    optsParamsAddFlag(params, "astar", (void *)&opt.ground_planner.use_astar);
    optsParamsAddFlag(params, "gbfs", (void *)&opt.ground_planner.use_gbfs);
    optsParamsAddFlag(params, "lazy", (void *)&opt.ground_planner.use_lazy);
    optsAddTags("gplan-heur", 0x0, NULL, optGroundPlannerHeur,
                "Sets up heuristics for grounded planner.\n"
                " TODO: List of options");
    optsAddStr("gplan-out", 0x0, &opt.ground_planner.plan_out, NULL,
               "Output filename for the found plan.");
    optsAddStr("gplan-o", 0x0, &opt.ground_planner.plan_out, NULL,
               "Alias for --gplan-out");

    optsStartGroup("Reversibility:");
    optsAddInt("reversibility-max-depth", 0x0, &opt.reversibility.max_depth, 1,
               "Maximum depth when searching for reversible plans"
               " (also see --report-reversibility*).");
    optsAddFlag("reversibility-use-mutex", 0x0, &opt.reversibility.use_mutex, 0,
                "Use mutexes when search for reversible plans"
               " (also see --report-reversibility*).");

    optsStartGroup("Reports:");
    optsAddFlag("report-lmg", 0x0, &opt.report.lmg, 0,
                "Create report of lifted mutex groups.");
    optsAddFlag("report-reversibility-simple", 0x0,
                &opt.report.reversibility_simple, 0,
                "Compute reversibility with the \"simple\" method.");
    optsAddFlag("report-reversibility-iterative", 0x0,
                &opt.report.reversibility_iterative, 0,
                "Compute reversibility with the \"iterative\" method.");

    if (opts(&argc, argv) != 0)
        return -1;

    if (opt.help){
        optsPrint(stderr);
        return -1;
    }

    if (opt.lmg.fd_monotonicity)
        opt.lmg.fd = 1;

    if (argc != 3 && argc != 2){
        for (int i = 1; i < argc; ++i){
            fprintf(stderr, "Error: Unrecognized argument: %s\n", argv[i]);
        }
        optsPrint(stderr);
        return -1;
    }

    if (argc == 2){
        if (pddlFiles1(&opt.files, argv[1], err) != 0)
            PDDL_TRACE_RET(err, -1);
    }else{ // argc == 3
        if (pddlFiles(&opt.files, argv[1], argv[2], err) != 0)
            PDDL_TRACE_RET(err, -1);
    }

    if (opt.max_mem > 0){
        struct rlimit mem_limit;
        mem_limit.rlim_cur
            = mem_limit.rlim_max = opt.max_mem * 1024UL * 1024UL;
        setrlimit(RLIMIT_AS, &mem_limit);
    }

    if (opt.ground_planner.use_astar){
        opt.ground_planner.enable = 1;
        opt.ground_planner.search_fn = pddlSearchAStar;
        strcpy(opt.ground_planner.log_prefix, "A*: ");

    }else if (opt.ground_planner.use_gbfs){
        fprintf(stderr, "Error: gbfs not implemented yet!\n");
        exit(-1);
        opt.ground_planner.enable = 1;
        //opt.ground_planner.search_fn = pddlSearchLiftedGBFS;
        strcpy(opt.ground_planner.log_prefix, "GBFS: ");

    }else if (opt.ground_planner.use_lazy){
        opt.ground_planner.enable = 1;
        opt.ground_planner.search_fn = pddlSearchLazy;
        strcpy(opt.ground_planner.log_prefix, "Lazy: ");
    }

    return 0;
}
