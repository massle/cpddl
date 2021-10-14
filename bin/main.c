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
    } lmg;

    struct {
        int use_sql;
        int use_dl;
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

    optsStartGroup("Grounding:");
    optsAddFlag("ground-sql", 0x0, &opt.ground.use_sql, 0,
                "Ground using sqlite.");
    optsAddFlag("ground-dl", 0x0, &opt.ground.use_dl, 0,
                "Ground using datalog.");
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

    if (opt.lmg.out != NULL){
        FILE *fout = openFile(opt.lmg.out);
        if (fout == NULL){
            fprintf(stderr, "Error: Could not open '%s'\n", opt.lmg.out);
            return -1;
        }
        BOR_INFO(&err, "Printing lifted mutex groups to '%s'", opt.lmg.out);
        pddlLiftedMGroupsPrint(&pddl, &lifted_mgroups, fout);
        closeFile(fout);
    }

    if (opt.lmg.fd_monotonicity_out != NULL){
        FILE *fout = openFile(opt.lmg.fd_monotonicity_out);
        if (fout == NULL){
            fprintf(stderr, "Error: Could not open '%s'\n",
                    opt.lmg.fd_monotonicity_out);
            return -1;
        }
        BOR_INFO(&err, "Printing monotonicity invariants to '%s'",
                 opt.lmg.fd_monotonicity_out);
        pddlLiftedMGroupsPrint(&pddl, &monotonicity_invariants, fout);
        closeFile(fout);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);
    if (setOpts(argc, argv) != 0
            || stepPDDL() != 0
            || stepLiftedMGroups() != 0){
        if (borErrIsSet(&err)){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
        }
        return -1;
    }
    return 0;
}
