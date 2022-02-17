#include <signal.h>
#include "pddl/pddl.h"
#include "opts.h"
#include "options.h"
#include "process_strips.h"
#include "report.h"
#include "lifted_planner.h"
#include "print_to_file.h"


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
int astar_search_started = 0;
int astar_terminate = 0;


static int stepPDDL(void)
{
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = opt.pddl.force_adl;
    pddl_cfg.normalize = 1;
    pddl_cfg.remove_empty_types = opt.pddl.remove_empty_types;
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
    if (lifted_mgroups_set)
        pddlLiftedMGroupsFree(&lifted_mgroups);
    if (monotonicity_invariants_set)
        pddlLiftedMGroupsFree(&monotonicity_invariants);
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

    PRINT_TO_FILE(&err, opt.lmg.out, "lifted mutex groups",
                  pddlLiftedMGroupsPrint(&pddl, &lifted_mgroups, fout));
    PRINT_TO_FILE(&err, opt.lmg.fd_monotonicity_out, "monotonicity invariants",
                  pddlLiftedMGroupsPrint(&pddl, &monotonicity_invariants, fout));

    return opt.lmg.stop;
}

static int stepLiftedEndomorph(void)
{
    if (!opt.lifted_endomorph.enable){
        BOR_INFO2(&err, "Inference of lifted endomorphisms turned off");
        return 0;
    }

    BOR_INFO_PREFIX_PUSH(&err, "LENDO: ");
    int ret = 0;
    pddl_endomorphism_config_t cfg = PDDL_ENDOMORPHISM_CONFIG_INIT;
    cfg.ignore_costs = opt.lifted_endomorph.ignore_costs;
    BOR_ISET(redundant_objs);
    pddlEndomorphismLifted(&pddl, &lifted_mgroups, &cfg,
            &redundant_objs, NULL, &err);
    if (borISetSize(&redundant_objs) > 0){
        pddlRemoveObjs(&pddl, &redundant_objs, &err);
        if (opt.lmg.enable)
            ret = stepLiftedMGroups();
    }
    borISetFree(&redundant_objs);

    BOR_INFO_PREFIX_POP(&err);
    return ret;
}

static int stepLiftedPlanner(void)
{
    if (!opt.lifted_planner.enable)
        return 0;
    return liftedPlanner(&pddl, &err);
}

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


    PRINT_TO_FILE(&err, opt.ground.mgroup_out, "grounded mutex groups",
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

    PRINT_TO_FILE(&err, opt.mg.out, "mutex groups",
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

static int stepRedBlackFDR(void)
{
    if (!opt.rb_fdr.enable)
        return 0;
    pddl_fdr_t fdr[opt.rb_fdr.cfg.mgroup.num_solutions];
    int num = pddlRedBlackFDRInitFromStrips(fdr, &strips, &mgroup, &mutex,
                                            &opt.rb_fdr.cfg, &err);
    for (int i = 0; i < num; ++i){
        if (opt.fdr.order_vars_cg){
            pddlFDRReorderVarsCG(fdr + i);
            BOR_INFO(&err, "FDR[%d]: variables reordered using causal graph.", i);
        }
    }

    for (int i = 0; i < num && opt.rb_fdr.out != NULL; ++i){
        if (i > 0){
            char fn[1024];
            sprintf(fn, "%s.%d", opt.rb_fdr.out, i);
            PRINT_TO_FILE(&err, fn, "FDR", pddlFDRPrintFD(fdr + i, &mgroup, 1, fout));
        }else{
            PRINT_TO_FILE(&err, opt.rb_fdr.out, "FDR",
                          pddlFDRPrintFD(fdr, &mgroup, 1, fout));
        }
    }

    for (int i = 0; i < num; ++i)
        pddlFDRFree(fdr + i);
    return 1;
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
    PRINT_TO_FILE(&err, opt.fdr.out, "FDR", pddlFDRPrintFD(&fdr, &mgroup, 1, fout));

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

static void printAStarStat(const pddl_search_astar_t *astar, bor_err_t *err)
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

static int stepAStar(void)
{
    if (!opt.astar.enable)
        return 0;

    BOR_INFO_PREFIX_PUSH(&err, "A*: ");
    // TODO
    pddl_heur_t *heur = pddlHeurBlind();
    pddl_search_astar_t *astar;
    astar = pddlSearchAStar(&fdr, heur, &err);
    int ret = pddlSearchAStarInitStep(astar);
    astar_search_started = 1;

    bor_timer_t info_timer;
    borTimerStart(&info_timer);
    for (int step = 1; ret == PDDL_SEARCH_CONT; ++step){
        if (astar_terminate){
            printAStarStat(astar, &err);
            BOR_INFO2(&err, "Search aborted.");
            pddlSearchAStarDel(astar);
            pddlHeurDel(heur);
            BOR_INFO_PREFIX_POP(&err);
            return -1;
        }

        ret = pddlSearchAStarStep(astar);
        // TODO: parametrize
        if (step >= 100){
            borTimerStop(&info_timer);
            if (borTimerElapsedInSF(&info_timer) >= 1.){
                printAStarStat(astar, &err);
                borTimerStart(&info_timer);
            }
            step = 0;
        }
    }
    printAStarStat(astar, &err);

    if (ret == PDDL_SEARCH_UNSOLVABLE){
        BOR_INFO2(&err, "Problem is unsolvable.");

    }else if (ret == PDDL_SEARCH_FOUND){
        BOR_INFO2(&err, "Plan found.");
        pddl_plan_t plan;
        pddlPlanInit(&plan);
        pddlPlanLoadBacktrack(&plan, astar->goal_state_id, &astar->state_space);
        BOR_INFO(&err, "Plan Cost: %d", plan.cost);
        BOR_INFO(&err, "Plan Length: %d", plan.length);
        PRINT_TO_FILE(&err, opt.astar.plan_out, "plan",
                      pddlPlanPrint(&plan, &fdr.op, fout));
        pddlPlanFree(&plan);
    }else{
        BOR_FATAL("Unkown return status: %d", ret);
    }

    if (astar_terminate){
        BOR_INFO2(&err, "Search aborted.");
        pddlSearchAStarDel(astar);
        pddlHeurDel(heur);
        BOR_INFO_PREFIX_POP(&err);
        return -1;
    }

    pddlSearchAStarDel(astar);
    pddlHeurDel(heur);
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
            || (ret = stepLiftedEndomorph()) != 0
            || (ret = stepLiftedPlanner()) != 0
            || (ret = stepGround()) != 0
            || (ret = stepGroundMGroups()) != 0
            || (ret = stepInferMGroups()) != 0
            || (ret = stepProcessStrips()) != 0
            || (ret = stepRedBlackFDR()) != 0
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
