#include <sys/time.h>
#include <sys/resource.h>
#include "pddl/pddl.h"
#include "opts.h"
#include "process_strips.h"

struct options {
    int help;
    int max_mem;

    struct {
        int force_adl;
        int compile_away_cond_eff;
    } pddl;

    struct {
        int max_candidates;
        int max_mgroups;
        int fd;
        int fd_monotonicity;
        int enable;
        char *out;
        char *fd_monotonicity_out;
        int stop;
    } lmg;

    struct {
        int (*method_fn)(pddl_strips_t *,
                         const pddl_t *,
                         const pddl_ground_config_t *,
                         bor_err_t *);

        int mgroup;
        int mgroup_remove_subsets;
        char *mgroup_out;
    } ground;

    struct {
        int compile_away_cond_eff;
    } strips;

    struct {
        int fam;
        int h2;
        int fam_lmg;
        int fam_maximal;
        float fam_time_limit;
        int fam_limit;
        int remove_subsets;
        char *out;
    } mg;

    struct {
        char *out;
    } fdr;
} opt = { 0 };

bor_err_t err = BOR_ERR_INIT;
pddl_files_t files;
pddl_t pddl;
int pddl_set = 0;
pddl_lifted_mgroups_t lifted_mgroups;
int lifted_mgroups_set = 0;
pddl_lifted_mgroups_t monotonicity_invariants;
int monotonicity_invariants_set = 0;
pddl_strips_t strips;
int strips_set = 0;
pddl_mgroups_t mgroup;
pddl_mutex_pairs_t mutex;

pddl_process_strips_t process_strips;
pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;


static int optSetGround(const char *tag)
{
    if (strcmp(tag, "default") == 0){
        opt.ground.method_fn = pddlStripsGround;

    }else if (strcmp(tag, "sql") == 0){
        opt.ground.method_fn = pddlStripsGroundSql;

    }else if (strcmp(tag, "dl") == 0){
        opt.ground.method_fn = pddlStripsGroundDatalog;

    }else if (strcmp(tag, "prune-pre") == 0){
        ground_cfg.lifted_mgroups = &lifted_mgroups;
        ground_cfg.prune_op_pre_mutex = 1;

    }else if (strcmp(tag, "prune-dead-end") == 0){
        ground_cfg.lifted_mgroups = &lifted_mgroups;
        ground_cfg.prune_op_dead_end = 1;

    }else if (strcmp(tag, "prune-all") == 0){
        ground_cfg.lifted_mgroups = &lifted_mgroups;
        ground_cfg.prune_op_pre_mutex = 1;
        ground_cfg.prune_op_dead_end = 1;

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
    char *s = BOR_STRDUP(tag);
    char *next = s;
    char *cur;
    if ((cur = strsep(&next, ",")) != NULL){
        if (strcmp(cur, "irr") == 0 || strcmp(cur, "irrelevance") == 0){
            pddlProcessStripsAddIrrelevance(&process_strips);
            if (next != NULL){
                fprintf(stderr, "Error: Invalid argument '%s'\n", next);
                return -1;
            }

        }else if (strcmp(cur, "fam-dead-end") == 0){
            pddlProcessStripsAddFAMGroupsDeadEndOps(&process_strips);
            if (next != NULL){
                fprintf(stderr, "Error: Invalid argument '%s'\n", next);
                return -1;
            }

        }else if (strcmp(cur, "dedup") == 0){
            pddlProcessStripsAddDeduplicateOps(&process_strips);
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
            pddlProcessStripsAddH2Fw(&process_strips, time_limit);

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
            pddlProcessStripsAddH2FwBw(&process_strips, time_limit);

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
            pddlProcessStripsAddH3Fw(&process_strips, time_limit, excess_mem);

        }else{
            fprintf(stderr, "Error: Invalid argument '%s'\n", cur);
            return -1;
        }
    }
    BOR_FREE(s);
    return 0;
}

static int optProcessStripsH2(int enabled)
{
    return optsProcessTags("irr:fam-dead-end:h2fwbw:irr:dedup", optProcessStrips);
}

static int setOpts(int argc, char *argv[])
{
    pddlProcessStripsInit(&process_strips);
    ground_cfg.lifted_mgroups = NULL;
    ground_cfg.prune_op_pre_mutex = 0;
    ground_cfg.prune_op_dead_end = 0;
    ground_cfg.remove_static_facts = 1;

    opt.ground.method_fn = pddlStripsGround;

    optsAddFlag("help", 'h', &opt.help, 0, "Print this help.");
    optsAddInt("max-mem", 0x0, &opt.max_mem, 0,
               "Maximum memory in MB if >0.");

    optsStartGroup("PDDL:");
    optsAddFlag("force-adl", 0x0, &opt.pddl.force_adl, 1,
                "Force :adl requirement if it is not specified in the"
                " domain file.");
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

    optsStartGroup("Grounding:");
    optsAddTags("ground", 'G', "default:prune-all",
                optSetGround,
                "Grounding method, combination (delimited by ':') of:\n"
                "  default - default grounding method\n"
                "  sql - sqlite-based grounding method\n"
                "  dl - datalog-based grounding method\n"
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
    optsAddStr("fdr-out", 'o', &opt.fdr.out, NULL,
               "Output filename for FDR encoding of the task.");

    if (opts(&argc, argv) != 0)
        return -1;

    if (opt.help){
        optsPrint(stderr);
        return -1;
    }

    // implications
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
        if (pddlFiles1(&files, argv[1], &err) != 0)
            BOR_TRACE_RET(&err, -1);
    }else{ // argc == 3
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

static FILE *openFile(const char *fn)
{
    if (strcmp(fn, "-") == 0
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

#define PRINT_TO_FILE(OUT, S, CMD) \
    do { \
    if ((OUT) != NULL){ \
        FILE *fout = openFile((OUT)); \
        if (fout != NULL){ \
            BOR_INFO(&err, "Printing %s to %s ...", (S), (OUT)); \
            CMD; \
            closeFile(fout); \
        }else{ \
            BOR_ERR_RET(&err, -1, "Could not open '%s'", (OUT)); \
        } \
    } \
    } while (0) 


static int stepPDDL(void)
{
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = opt.pddl.force_adl;
    pddl_cfg.normalize = 1;
    pddl_cfg.compile_away_cond_eff = opt.pddl.compile_away_cond_eff;

    if (pddlInit(&pddl, files.domain_pddl, files.problem_pddl,
                 &pddl_cfg, &err) != 0){
        BOR_TRACE_RET(&err, -1);
    }
    pddl_set = 1;
    pddlCheckSizeTypes(&pddl);

    return 0;
}

static int stepLiftedMGroups(void)
{
    pddlLiftedMGroupsInit(&lifted_mgroups);
    lifted_mgroups_set = 1;
    pddlLiftedMGroupsInit(&monotonicity_invariants);
    monotonicity_invariants_set = 1;

    if (!opt.lmg.enable){
        BOR_INFO2(&err, "Inference of lifted mutex groups turned off");
        return 0;
    }

    pddl_lifted_mgroups_infer_limits_t lifted_mgroups_limits
            = PDDL_LIFTED_MGROUPS_INFER_LIMITS_INIT;
    lifted_mgroups_limits.max_candidates = opt.lmg.max_candidates;
    lifted_mgroups_limits.max_mgroups = opt.lmg.max_mgroups;

    if (opt.lmg.fd){
        pddl_lifted_mgroups_t *mono = NULL;
        if (opt.lmg.fd_monotonicity)
            mono = &monotonicity_invariants;
        pddlLiftedMGroupsInferMonotonicity(&pddl, &lifted_mgroups_limits, mono,
                                           &lifted_mgroups, &err);
    }else{
        pddlLiftedMGroupsInferFAMGroups(&pddl, &lifted_mgroups_limits,
                                        &lifted_mgroups, &err);
    }
    pddlLiftedMGroupsSetExactlyOne(&pddl, &lifted_mgroups, &err);
    pddlLiftedMGroupsSetStatic(&pddl, &lifted_mgroups, &err);

    PRINT_TO_FILE(opt.lmg.out, "lifted mutex groups",
                  pddlLiftedMGroupsPrint(&pddl, &lifted_mgroups, fout));
    PRINT_TO_FILE(opt.lmg.fd_monotonicity_out, "monotonicity invariants",
                  pddlLiftedMGroupsPrint(&pddl, &monotonicity_invariants, fout));

    return opt.lmg.stop;
}

/* TODO
static int prunePDDL(void)
{
    if (opt.lifted_endomorphism){
        pddl_endomorphism_config_t cfg = PDDL_ENDOMORPHISM_CONFIG_INIT;
        if (opt.lifted_endomorphism_ignore_costs)
            cfg.ignore_costs = 1;
        BOR_ISET(redundant_objs);
        pddlEndomorphismLifted(&pddl, &lifted_mgroups, &cfg,
                               &redundant_objs, &err);
        if (borISetSize(&redundant_objs) > 0){
            pddlRemoveObjs(&pddl, &redundant_objs, &err);
            // If we removed anything, we need to infer mutex groups again
            pddlLiftedMGroupsFree(&lifted_mgroups);
            liftedMGroups();
        }
        if (opt.lifted_endomorphism_costs_then_wo_costs){
            cfg.ignore_costs = 1;
            borISetEmpty(&redundant_objs);
            pddlEndomorphismLifted(&pddl, &lifted_mgroups, &cfg,
                    &redundant_objs, &err);
            if (borISetSize(&redundant_objs) > 0){
                pddlRemoveObjs(&pddl, &redundant_objs, &err);
                // If we removed anything, we need to infer mutex groups again
                pddlLiftedMGroupsFree(&lifted_mgroups);
                liftedMGroups();
            }
        }
        borISetFree(&redundant_objs);
    }

    if (opt.pddl_domain_out != NULL){
        FILE *fout = fopen(opt.pddl_domain_out, "w");
        if (fout != NULL){
            pddlPrintPDDLDomain(&pddl, fout);
            fclose(fout);
        }else{
            BOR_ERR_RET(&err, -1, "Could not open '%s'", opt.pddl_domain_out);
        }
    }

    if (opt.pddl_problem_out != NULL){
        FILE *fout = fopen(opt.pddl_problem_out, "w");
        if (fout != NULL){
            pddlPrintPDDLProblem(&pddl, fout);
            fclose(fout);
        }else{
            BOR_ERR_RET(&err, -1, "Could not open '%s'", opt.pddl_problem_out);
        }
    }

    return 0;
}
*/

static void stripsCompileAwayCondEff(void)
{
    if (!opt.strips.compile_away_cond_eff)
        return;


    BOR_INFO_PREFIX_PUSH(&err, "STRIPS CE: ");
    if (!strips.has_cond_eff){
        BOR_INFO2(&err, "The task has no conditional effects.");
        BOR_INFO_PREFIX_POP(&err);
        return;
    }

    BOR_INFO2(&err, "Compiling away conditional effects ...");
    pddlStripsCompileAwayCondEff(&strips);
    BOR_INFO2(&err, "Conditional effects compiled away.");
    pddlStripsLogInfo(&strips, &err);
    BOR_INFO_PREFIX_POP(&err);
}

static int stepGround(void)
{
    if (opt.ground.method_fn(&strips, &pddl, &ground_cfg, &err) != 0){
        BOR_INFO2(&err, "Grounding failed.");
        BOR_TRACE_RET(&err, -1);
    }

    stripsCompileAwayCondEff();

    pddlMGroupsInitEmpty(&mgroup);
    pddlMutexPairsInitStrips(&mutex, &strips);
    strips_set = 1;

    return 0;
}

static int stepGroundMGroups(void)
{
    if (!opt.ground.mgroup){
        BOR_INFO2(&err, "Grounding of lifted mutex groups disabled.");
        return 0;
    }

    BOR_INFO_PREFIX_PUSH(&err, "Ground LMG: ");
    BOR_INFO2(&err, "Grounding of lifted mutex groups ...");
    pddlMGroupsGround(&mgroup, &pddl, &lifted_mgroups, &strips);
    if (opt.ground.mgroup_remove_subsets)
        pddlMGroupsRemoveSubsets(&mgroup);
    pddlMGroupsSetExactlyOne(&mgroup, &strips);
    pddlMGroupsSetGoal(&mgroup, &strips);
    BOR_INFO(&err, "Found %d mutex groups", mgroup.mgroup_size);
    pddlMutexPairsAddMGroups(&mutex, &mgroup);
    BOR_INFO(&err, "Found %d mutex pairs", mutex.num_mutex_pairs);
    BOR_INFO_PREFIX_POP(&err);


    PRINT_TO_FILE(opt.ground.mgroup_out, "grounded mutex groups",
                  pddlMGroupsPrint(&pddl, &strips, &mgroup, fout));

    return 0;
}

static int stepInferMGroups(void)
{
    if (!opt.mg.fam && !opt.mg.h2){
        BOR_INFO2(&err, "Inference of mutex groups disabled.");
        return 0;
    }

    BOR_INFO_PREFIX_PUSH(&err, "MG: ");
    if (opt.mg.fam){
        pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
        cfg.maximal = opt.mg.fam_maximal;
        cfg.limit = opt.mg.fam_limit;
        cfg.time_limit = opt.mg.fam_time_limit;
        if (!opt.mg.fam_lmg){
            pddlMGroupsFree(&mgroup);
            pddlMGroupsInitEmpty(&mgroup);
        }
        BOR_INFO(&err, "Inference of fam-groups starting with %d fam-groups",
                 mgroup.mgroup_size);
        if (pddlFAMGroupsInfer(&mgroup, &strips, &cfg, &err) != 0){
            BOR_TRACE_RET(&err, -1);
        }
        if (opt.mg.remove_subsets)
            pddlMGroupsRemoveSubsets(&mgroup);

    }else if (opt.mg.h2){
        pddl_mutex_pairs_t mutex;
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2(&strips, &mutex, NULL, NULL, 0., &err) != 0){
            BOR_INFO2(&err, "h^2 fw failed.");
            BOR_TRACE_RET(&err, -1);
        }

        pddlMGroupsFree(&mgroup);
        pddlMGroupsInitEmpty(&mgroup);
        pddlMutexPairsInferMutexGroups(&mutex, &mgroup, &err);
        pddlMutexPairsFree(&mutex);
    }

    BOR_INFO(&err, "Found %d mutex groups", mgroup.mgroup_size);

    pddlMGroupsSetExactlyOne(&mgroup, &strips);
    pddlMGroupsSetGoal(&mgroup, &strips);

    pddlMutexPairsAddMGroups(&mutex, &mgroup);
    BOR_INFO(&err, "%d mutex pairs so far", mutex.num_mutex_pairs);
    BOR_INFO_PREFIX_POP(&err);

    PRINT_TO_FILE(opt.mg.out, "mutex groups",
                  pddlMGroupsPrint(&pddl, &strips, &mgroup, fout));

    return 0;
}

static int stepProcessStrips(void)
{
    return pddlProcessStripsExecute(&process_strips, &strips, &mgroup, &mutex, &err);
}

static int stepFDR(void)
{
    // TODO
    return 0;
}

static int stepAStar(void)
{
    // TODO
    return 0;
}

static int stepSymba(void)
{
    // TODO
    return 0;
}

void freeData(void)
{
    if (strips_set){
        pddlStripsFree(&strips);
        pddlMGroupsFree(&mgroup);
        pddlMutexPairsFree(&mutex);
    }
    if (monotonicity_invariants_set)
        pddlLiftedMGroupsFree(&monotonicity_invariants);
    if (lifted_mgroups_set)
        pddlLiftedMGroupsFree(&lifted_mgroups);
    if (pddl_set)
        pddlFree(&pddl);
    optsFree();
}

int main(int argc, char *argv[])
{
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);
    int ret = 0;
    if ((ret = setOpts(argc, argv)) != 0
            || (ret = stepPDDL()) != 0
            || (ret = stepLiftedMGroups()) != 0
            || (ret = stepGround()) != 0
            || (ret = stepGroundMGroups()) != 0
            || (ret = stepInferMGroups()) != 0
            || (ret = stepProcessStrips()) != 0
            || (ret = stepFDR()) != 0
            || (ret = stepAStar()) != 0
            || (ret = stepSymba()) != 0){
        if (ret < 0){
            if (borErrIsSet(&err)){
                fprintf(stderr, "Error: ");
                borErrPrint(&err, 1, stderr);
            }
            freeData();
            return -1;
        }
    }

    pddlProcessStripsFree(&process_strips);
    freeData();
    return 0;
}
