#include <sys/time.h>
#include <sys/resource.h>
#include "pddl/pddl.h"
#include "opts.h"

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
        char *method;
        char *prune;
    } ground;

    struct {
        int compile_away_cond_eff;
    } strips;

    struct {
        char *out;
    } fdr;
} opt = { 0 };

bor_err_t err = BOR_ERR_INIT;
pddl_files_t files;
pddl_t pddl;
pddl_lifted_mgroups_t lifted_mgroups;
pddl_lifted_mgroups_t monotonicity_invariants;
pddl_strips_t strips;


int (*ground_method)(pddl_strips_t *,
                     const pddl_t *,
                     const pddl_ground_config_t *,
                     bor_err_t *) = pddlStripsGround;
pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;

static int selectGroundMethod(const char *method)
{
    if (strcmp(method, "default") == 0){
        ground_method = pddlStripsGround;
    }else if (strcmp(method, "sql") == 0){
        ground_method = pddlStripsGroundSql;
    }else if (strcmp(method, "dl") == 0){
        ground_method = pddlStripsGroundDatalog;
    }else{
        return -1;
    }
    return 0;
}

static int setOpts(int argc, char *argv[])
{
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
    optsAddStr("ground", 'G', &opt.ground.method, "default",
                "Grounding method, one of:\n"
                "  default - default method\n"
                "  sql - sqlite-based method\n"
                "  dl - datalog-based method\n");
    optsAddStr("ground-prune", 0x0, &opt.ground.prune, "all",
                "Use lifted mutex groups for pruning during grounding. "
                "Possible options:\n"
                "  0/n/no/none - disable this step\n"
                "  pre - check only preconditions\n"
                "  dead-end - check only dead-ends\n"
                "  1/all/pre:dead-end - use all\n");

    optsStartGroup("STRIPS:");
    optsAddFlag("ce", 0x0, &opt.strips.compile_away_cond_eff, 0,
                "Compile away conditional effects on the STRIPS level"
                " (recommended instead of --ce-pddl).");

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

    if (selectGroundMethod(opt.ground.method) != 0){
        fprintf(stderr, "Error: Unknown grounding method %s\n",
                opt.ground.method);
        return -1;
    }

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
    pddlCheckSizeTypes(&pddl);

    return 0;
}

static int stepLiftedMGroups(void)
{
    pddlLiftedMGroupsInit(&lifted_mgroups);
    pddlLiftedMGroupsInit(&monotonicity_invariants);

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

static int stepGround(void)
{
    ground_cfg.lifted_mgroups = &lifted_mgroups;
    ground_cfg.prune_op_pre_mutex = 1;
    ground_cfg.prune_op_dead_end = 1;
    // TODO; Config

    if (ground_method(&strips, &pddl, &ground_cfg, &err) != 0){
        BOR_INFO2(&err, "Grounding failed.");
        BOR_TRACE_RET(&err, -1);
    }

    /*
    if (opt.compile_away_cond_eff)
        pddlStripsCompileAwayCondEff(&strips);
    */

    return 0;
}

int main(int argc, char *argv[])
{
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);
    int ret = 0;
    if ((ret = setOpts(argc, argv)) != 0
            || (ret = stepPDDL()) != 0
            || (ret = stepLiftedMGroups()) != 0
            || (ret = stepGround()) != 0){
        if (ret < 0){
            if (borErrIsSet(&err)){
                fprintf(stderr, "Error: ");
                borErrPrint(&err, 1, stderr);
            }
            return -1;
        }
    }

    optsFree();
    return 0;
}
