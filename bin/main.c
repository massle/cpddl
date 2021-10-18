#include "pddl/pddl.h"
#include "opts.h"
#include "options.h"
#include "process_strips.h"
#include "report.h"


bor_err_t err = BOR_ERR_INIT;
pddl_t pddl;
int pddl_set = 0;
pddl_lifted_mgroups_t lifted_mgroups;
int lifted_mgroups_set = 0;
pddl_lifted_mgroups_t monotonicity_invariants;
int monotonicity_invariants_set = 0;
pddl_strips_t strips;
int strips_set = 0;
pddl_fdr_t fdr;
int fdr_set = 0;
pddl_mgroups_t mgroup;
pddl_mutex_pairs_t mutex;


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

    if (pddlInit(&pddl, opt.files.domain_pddl, opt.files.problem_pddl,
                 &pddl_cfg, &err) != 0){
        BOR_TRACE_RET(&err, -1);
    }
    pddl_set = 1;
    pddlCheckSizeTypes(&pddl);

    return 0;
}

static int stepReportLiftedMGroups(void)
{
    if (!opt.report.lmg)
        return 0;
    reportLiftedMGroups(&pddl, &err);
    return 1;
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
    if (lifted_mgroups_set
            && (opt.ground.cfg.prune_op_dead_end
                    || opt.ground.cfg.prune_op_pre_mutex)){
        opt.ground.cfg.lifted_mgroups = &lifted_mgroups;
    }

    if (opt.ground.method_fn(&strips, &pddl, &opt.ground.cfg, &err) != 0){
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
    int ret =  pddlProcessStripsExecute(&opt.strips.process, &strips,
                                        &mgroup, &mutex, &err);
    pddlProcessStripsFree(&opt.strips.process);
    return ret;
}

static int processFDR(pddl_fdr_t *fdr, int fdr_id)
{
    int fnout_size = strlen(opt.fdr.out);
    char fnout[fnout_size + 5];
    if (fdr_id == 0){
        sprintf(fnout, "%s", opt.fdr.out);
    }else{
        sprintf(fnout, "%s.%d", opt.fdr.out, fdr_id + 1);
    }

    /* TODO
    if (opt.order_vars_cg){
        pddlFDRReorderVarsCG(fdr);
        BOR_INFO2(&err, "FDR variables reordered using causal graph.");
    }
    */

    // TODO
    //pddlRedBlackCheck(fdr, &err);

    /*
    BOR_INFO(&err, "Output file: '%s'", fnout);
    FILE *fout = openFile(fnout);
    if (fout == NULL){
        fprintf(stderr, "Error: Could not open file '%s'\n", opt.fdr_out);
        return -1;
    }

    */


    if (opt.fdr.out != NULL)
        PRINT_TO_FILE(fnout, "FDR", pddlFDRPrintFD(fdr, &mgroup, 1, fout));


    /* TODO
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
        pddl_pot_solutions_t pot;
        if (pddlHPot(&pot, fdr, &pot_cfg, &err) != 0){
            BOR_INFO2(&err, "Cannot find potential heuristic");
            BOR_INFO_PREFIX_POP(&err);
            return -1;
        }
        int est = pddlPotSolutionsEvalMaxFDRState(&pot, &fdr->var, fdr->init);
        BOR_INFO(&err, "Init state estimate: %d", est);
        printPotentials(fdr, &pot, fout);
        pddlPotSolutionsFree(&pot);
        BOR_INFO_PREFIX_POP(&err);
    }
    */
    return 0;
}

static int stepFDR(void)
{
    pddlFDRInitFromStrips(&fdr, &strips, &mgroup, &mutex,
                          opt.fdr.var_flag, opt.fdr.flag, &err);
    fdr_set = 1;

    if (opt.fdr.order_vars_cg){
        pddlFDRReorderVarsCG(&fdr);
        BOR_INFO2(&err, "FDR variables reordered using causal graph.");
    }
    PRINT_TO_FILE(opt.fdr.out, "FDR", pddlFDRPrintFD(&fdr, &mgroup, 1, fout));

    if (opt.fdr.pretty_print_vars)
        pddlFDRVarsPrintTable(&fdr.var, 150, NULL, &err);
    if (opt.fdr.pretty_print_cg){
        pddl_cg_t cg;
        pddlCGInit(&cg, &fdr.var, &fdr.op, 0);
        pddlCGPrintAsciiGraph(&cg, NULL, &err);
        pddlCGFree(&cg);
    }
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
    if (fdr_set)
        pddlFDRFree(&fdr);
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
    if ((ret = setOptions(argc, argv, &err)) != 0
            || (ret = stepPDDL()) != 0
            || (ret = stepReportLiftedMGroups()) != 0
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

    freeData();
    return 0;
}
