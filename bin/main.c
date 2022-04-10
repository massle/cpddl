#include <signal.h>
#include "pddl/pddl.h"
#include "opts.h"
#include "options.h"
#include "process_strips.h"
#include "report.h"
#include "lifted_planner.h"
#include "print_to_file.h"


pddl_err_t err = PDDL_ERR_INIT;
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
int search_started = 0;
int search_terminate = 0;


static int stepPDDL(void)
{
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = opt.pddl.force_adl;
    pddl_cfg.normalize = 1;
    pddl_cfg.remove_empty_types = opt.pddl.remove_empty_types;
    pddl_cfg.compile_away_cond_eff = opt.pddl.compile_away_cond_eff;

    if (pddlInit(&pddl, opt.files.domain_pddl, opt.files.problem_pddl,
                 &pddl_cfg, &err) != 0){
        PDDL_TRACE_RET(&err, -1);
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
        PDDL_INFO2(&err, "Inference of lifted mutex groups disabled");
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
        PDDL_INFO2(&err, "Lifted endomorphisms disabled");
        return 0;
    }

    PDDL_CTX(&err, "lend", "LENDO");
    int ret = 0;
    pddl_endomorphism_config_t cfg = PDDL_ENDOMORPHISM_CONFIG_INIT;
    cfg.ignore_costs = opt.lifted_endomorph.ignore_costs;
    PDDL_ISET(redundant_objs);
    pddlEndomorphismLifted(&pddl, &lifted_mgroups, &cfg,
            &redundant_objs, NULL, &err);
    if (pddlISetSize(&redundant_objs) > 0){
        pddlRemoveObjs(&pddl, &redundant_objs, &err);
        if (opt.lmg.enable)
            ret = stepLiftedMGroups();
    }
    pddlISetFree(&redundant_objs);

    PDDL_CTXEND(&err);
    return ret;
}

static int stepLiftedPlanner(void)
{
    if (opt.lifted_planner.search == LIFTED_PLAN_NONE)
        return 0;
    return liftedPlanner(&pddl, &err);
}

static void stripsCompileAwayCondEff(void)
{
    if (!opt.strips.compile_away_cond_eff)
        return;


    PDDL_CTX(&err, "strips_ce", "STRIPS CE");
    if (!strips.has_cond_eff){
        PDDL_INFO2(&err, "The task has no conditional effects.");
        PDDL_CTXEND(&err);
        return;
    }

    PDDL_INFO2(&err, "Compiling away conditional effects ...");
    pddlStripsCompileAwayCondEff(&strips);
    PDDL_INFO2(&err, "Conditional effects compiled away.");
    pddlStripsLogInfo(&strips, &err);
    PDDL_CTXEND(&err);
}

static int stepGround(void)
{
    if (lifted_mgroups_set
            && (opt.ground.cfg.prune_op_dead_end
                    || opt.ground.cfg.prune_op_pre_mutex)){
        opt.ground.cfg.lifted_mgroups = &lifted_mgroups;
    }

    int ret = -1;
    if (opt.ground.method == GROUND_TRIE){
        ret = pddlStripsGround(&strips, &pddl, &opt.ground.cfg, &err);
    }else if (opt.ground.method == GROUND_SQL){
        ret = pddlStripsGroundSql(&strips, &pddl, &opt.ground.cfg, &err);
    }else if (opt.ground.method == GROUND_DL){
        ret = pddlStripsGroundDatalog(&strips, &pddl, &opt.ground.cfg, &err);
    }
    if (ret != 0){
        PDDL_INFO2(&err, "Grounding failed.");
        PDDL_TRACE_RET(&err, -1);
    }

    stripsCompileAwayCondEff();

    pddlMGroupsInitEmpty(&mgroup);
    pddlMutexPairsInitStrips(&mutex, &strips);
    strips_set = 1;

    PRINT_TO_FILE(&err, opt.strips.py_out, "STRIPS as python",
                  pddlStripsPrintPython(&strips, fout));

    return opt.strips.stop;
}

static int stepReportMGroups(void)
{
    if (opt.report.mgroups){
        PDDL_CTX(&err, "report_mgs", "Report MGroups");
        reportMGroups(&pddl, &strips, &err);
        PDDL_CTXEND(&err);
        return 1;
    }
    return 0;
}

static int stepGroundMGroups(void)
{
    if (!opt.ground.mgroup){
        PDDL_INFO2(&err, "Grounding of lifted mutex groups disabled.");
        return 0;
    }

    PDDL_CTX(&err, "ground_lmg", "Ground LMG");
    PDDL_INFO2(&err, "Grounding of lifted mutex groups ...");
    pddlMGroupsGround(&mgroup, &pddl, &lifted_mgroups, &strips);
    if (opt.ground.mgroup_remove_subsets)
        pddlMGroupsRemoveSubsets(&mgroup);
    pddlMGroupsSetExactlyOne(&mgroup, &strips);
    pddlMGroupsSetGoal(&mgroup, &strips);
    PDDL_INFO(&err, "Found %d mutex groups", mgroup.mgroup_size);
    pddlMutexPairsAddMGroups(&mutex, &mgroup);
    PDDL_INFO(&err, "Found %d mutex pairs", mutex.num_mutex_pairs);
    PDDL_CTXEND(&err);


    PRINT_TO_FILE(&err, opt.ground.mgroup_out, "grounded mutex groups",
                  pddlMGroupsPrint(&pddl, &strips, &mgroup, fout));

    return 0;
}

static int stepInferMGroups(void)
{
    if (opt.mg.method == MG_NONE){
        PDDL_INFO2(&err, "Inference of mutex groups disabled.");
        return 0;
    }

    PDDL_CTX(&err, "mg", "MG");
    if (opt.mg.method == MG_FAM){
        pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
        cfg.maximal = opt.mg.fam_maximal;
        cfg.limit = opt.mg.fam_limit;
        cfg.time_limit = opt.mg.fam_time_limit;
        if (!opt.mg.fam_lmg){
            pddlMGroupsFree(&mgroup);
            pddlMGroupsInitEmpty(&mgroup);
        }
        PDDL_INFO(&err, "Inference of fam-groups starting with %d fam-groups",
                 mgroup.mgroup_size);
        if (pddlFAMGroupsInfer(&mgroup, &strips, &cfg, &err) != 0){
            PDDL_TRACE_RET(&err, -1);
        }
        if (opt.mg.remove_subsets)
            pddlMGroupsRemoveSubsets(&mgroup);

    }else if (opt.mg.method == MG_H2){
        pddl_mutex_pairs_t mutex;
        pddlMutexPairsInitStrips(&mutex, &strips);
        if (pddlH2(&strips, &mutex, NULL, NULL, 0., &err) != 0){
            PDDL_INFO2(&err, "h^2 fw failed.");
            PDDL_TRACE_RET(&err, -1);
        }

        pddlMGroupsFree(&mgroup);
        pddlMGroupsInitEmpty(&mgroup);
        pddlMutexPairsInferMutexGroups(&mutex, &mgroup, &err);
        pddlMutexPairsFree(&mutex);
    }

    PDDL_INFO(&err, "Found %d mutex groups", mgroup.mgroup_size);

    pddlMGroupsSetExactlyOne(&mgroup, &strips);
    pddlMGroupsSetGoal(&mgroup, &strips);

    pddlMutexPairsAddMGroups(&mutex, &mgroup);
    PDDL_INFO(&err, "%d mutex pairs so far", mutex.num_mutex_pairs);
    PDDL_CTXEND(&err);

    PRINT_TO_FILE(&err, opt.mg.out, "mutex groups",
                  pddlMGroupsPrint(&pddl, &strips, &mgroup, fout));

    if (opt.mg.cover_number){
        PDDL_INFO2(&err, "Computing mutex group cover number");
        int num = pddlMGroupsCoverNumber(&mgroup, strips.fact.fact_size);
        PDDL_INFO(&err, "Mutex group cover number: %d", num);
    }

    return 0;
}

static int stepProcessStrips(void)
{
    int ret =  pddlProcessStripsExecute(&opt.strips.process, &strips,
                                        &mgroup, &mutex, &err);
    pddlProcessStripsFree(&opt.strips.process);
    return ret;
}

static void reversibilityIterativeDepth(int *skip, int max_depth, FILE *fout)
{
    for (int op_id = 0; op_id < strips.op.op_size; ++op_id){
        if (skip[op_id])
            continue;

        const pddl_strips_op_t *op = strips.op.op[op_id];

        pddl_reversibility_uniform_t rev;
        pddlReversibilityUniformInit(&rev);
        const pddl_mutex_pairs_t *m = NULL;
        if (opt.reversibility.use_mutex)
            m = &mutex;
        pddlReversibilityUniformInfer(&rev, &strips.op, op, max_depth, m);
        pddlReversibilityUniformSort(&rev);
        for (int i = 0; i < rev.plan_size; ++i){
            if (rev.plan[i].reversible_op_id == op->id
                    && pddlISetSize(&rev.plan[i].formula.pos) == 0
                    && pddlISetSize(&rev.plan[i].formula.neg) == 0){
                skip[op_id] = 1;
            }
            if (pddlIArrSize(&rev.plan[i].plan) == max_depth){
                pddlReversePlanUniformPrint(rev.plan + i, &strips.op, fout);
            }
        }
        pddlReversibilityUniformFree(&rev);
    }
}

static int stepReportReversibility(void)
{
    if (!opt.report.reversibility_simple && !opt.report.reversibility_iterative)
        return 0;

    if (opt.report.reversibility_simple){
        int max_depth = opt.reversibility.max_depth;
        PDDL_INFO(&err, "Computing reverse plans. max-depth: %d", max_depth);
        for (int op_id = 0; op_id < strips.op.op_size; ++op_id){
            const pddl_strips_op_t *op = strips.op.op[op_id];

            pddl_reversibility_uniform_t rev;
            pddlReversibilityUniformInit(&rev);
            const pddl_mutex_pairs_t *m = NULL;
            if (opt.reversibility.use_mutex)
                m = &mutex;
            pddlReversibilityUniformInfer(&rev, &strips.op, op, max_depth, m);
            pddlReversibilityUniformSort(&rev);
            pddlReversibilityUniformPrint(&rev, &strips.op, stdout);
            pddlReversibilityUniformFree(&rev);
        }
        PDDL_INFO2(&err, "Reverse plans computed.");

    }else{
        int max_depth = opt.reversibility.max_depth;
        PDDL_INFO(&err, "Computing reverse plans iteratively. max-depth: %d",
                  max_depth);
        int *skip = PDDL_CALLOC_ARR(int, strips.op.op_size);
        for (int depth = 1; depth <= max_depth; ++depth){
            PDDL_INFO(&err, "Computing for max-depth: %d", depth);
            reversibilityIterativeDepth(skip, depth, stdout);
        }
        PDDL_FREE(skip);
        PDDL_INFO2(&err, "Reverse plans computed.");
    }

    return 1;
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
            PDDL_INFO(&err, "FDR[%d]: variables reordered using causal graph.", i);
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

static void printPotentials(const pddl_fdr_t *fdr,
                            const pddl_pot_solutions_t *pot,
                            FILE *fout)
{
    fprintf(fout, "%d\n", pot->sol_size);
    for (int pi = 0; pi < pot->sol_size; ++pi){
        const double *w = pot->sol[pi].pot;
        fprintf(fout, "begin_potentials\n");
        for (int fi = 0; fi < fdr->var.global_id_size; ++fi){
            const pddl_fdr_val_t *fval = fdr->var.global_id_to_val[fi];
            fprintf(fout, "%d %d %.20f\n",
                    fval->var_id, fval->val_id, w[fi]);
        }
        fprintf(fout, "end_potentials\n");
    }
}

static int stepFDR(void)
{
    pddlFDRInitFromStrips(&fdr, &strips, &mgroup, &mutex,
                          opt.fdr.var_flag, opt.fdr.flag, &err);
    fdr_set = 1;

    if (opt.fdr.order_vars_cg){
        pddlFDRReorderVarsCG(&fdr);
        PDDL_INFO2(&err, "FDR variables reordered using causal graph.");
    }

    if (opt.fdr.to_tnf || opt.fdr.to_tnf_multiply){
        PDDL_CTX(&err, "fdr_to_tnf", "FDR-to-TNF");
        if (opt.fdr.to_tnf){
            PDDL_INFO(&err, "Constructing TNF (ops: %d)", fdr.op.op_size);
        }else if (opt.fdr.to_tnf_multiply){
            PDDL_INFO(&err, "Constructing TNF-multiply (ops: %d)", fdr.op.op_size);
        }

        pddl_mg_strips_t mg_strips;
        pddl_mutex_pairs_t fdr_mutex;
        pddlMGStripsInitFDR(&mg_strips, &fdr);
        pddlMutexPairsInitStrips(&fdr_mutex, &mg_strips.strips);
        pddlMutexPairsAddMGroups(&fdr_mutex, &mg_strips.mg);
        pddlH2(&mg_strips.strips, &fdr_mutex, NULL, NULL, 0., &err);

        pddl_fdr_t fdr_old = fdr;
        unsigned flags = 0;
        if (opt.fdr.to_tnf_multiply)
            flags = PDDL_FDR_TNF_MULTIPLY_OPS;
        if (pddlFDRInitTransitionNormalForm(&fdr, &fdr_old, &fdr_mutex,
                                            flags, &err) != 0){
            pddlMutexPairsFree(&fdr_mutex);
            pddlMGStripsFree(&mg_strips);
            PDDL_CTXEND(&err);
            PDDL_TRACE_RET(&err, -1);
        }
        if (opt.fdr.to_tnf){
            PDDL_INFO(&err, "Constructed TNF, ops: %d", fdr.op.op_size);
        }else if (opt.fdr.to_tnf_multiply){
            PDDL_INFO(&err, "Constructed TNF-multiply, ops: %d", fdr.op.op_size);
        }

        pddlMutexPairsFree(&fdr_mutex);
        pddlMGStripsFree(&mg_strips);
        pddlFDRFree(&fdr_old);
        PDDL_CTXEND(&err);
    }


    PRINT_TO_FILE(&err, opt.fdr.out, "FDR", pddlFDRPrintFD(&fdr, &mgroup, 1, fout));

    if (opt.fdr.pot){
        pddl_pot_solutions_t pot;
        pddlPotSolutionsInit(&pot);
        if (pddlHPot(&pot, &fdr, &opt.fdr.pot_cfg, &err) != 0){
            PDDL_ERR_RET2(&err, -1, "Cannot find potential heuristic");
            return -1;
        }
        APPEND_TO_FILE(&err, opt.fdr.out, "FDR Pot",
                       printPotentials(&fdr, &pot, fout));
        pddlPotSolutionsFree(&pot);
    }

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

static void printSearchStat(const pddl_search_t *astar, pddl_err_t *err)
{
    pddl_search_stat_t stat;
    pddlSearchStat(astar, &stat);
    PDDL_INFO(err, "Search steps: %lu, expand: %lu, eval: %lu,"
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


static int stepGroundPlanner(void)
{
    if (opt.ground_planner.search == GROUND_PLAN_NONE)
        return 0;

    PDDL_CTX(&err, "gplan", "GPLAN");
    pddl_heur_t *heur = NULL;
    switch (opt.ground_planner.heur){
        case GROUND_PLAN_HEUR_LMC:
            PDDL_INFO2(&err, "Heuristic: lmc");
            heur = pddlHeurLMCut(&fdr, &err);
            break;
        case GROUND_PLAN_HEUR_MAX:
            PDDL_INFO2(&err, "Heuristic: hmax");
            heur = pddlHeurHMax(&fdr, &err);
            break;
        case GROUND_PLAN_HEUR_ADD:
            PDDL_INFO2(&err, "Heuristic: hadd");
            heur = pddlHeurHAdd(&fdr, &err);
            break;
        case GROUND_PLAN_HEUR_FF:
            PDDL_INFO2(&err, "Heuristic: hff");
            heur = pddlHeurHFF(&fdr, &err);
            break;
        case GROUND_PLAN_HEUR_FLOW:
            PDDL_INFO2(&err, "Heuristic: flow");
            heur = pddlHeurFlow(&fdr, &err);
            break;
        case GROUND_PLAN_HEUR_POT:
            PDDL_INFO2(&err, "Heuristic: pot");
            heur = pddlHeurPot(&fdr, &opt.ground_planner.pot_cfg, &err);
            break;
        case GROUND_PLAN_HEUR_BLIND:
        default:
            PDDL_INFO2(&err, "Heuristic: blind");
            heur = pddlHeurBlind();
    }

    pddl_search_t *search = NULL;
    switch (opt.ground_planner.search){
        case GROUND_PLAN_ASTAR:
            PDDL_INFO2(&err, "Search: astar");
            search = pddlSearchAStar(&fdr, heur, &err);
            break;
        case GROUND_PLAN_GBFS:
            PDDL_FATAL2("Error: gbfs not implemented yet!\n");
            break;
        case GROUND_PLAN_LAZY:
            PDDL_INFO2(&err, "Search: lazy");
            search = pddlSearchLazy(&fdr, heur, &err);
            break;
        default:
            PDDL_FATAL("Unknown planner %d", opt.ground_planner.search);
    }

    int ret = pddlSearchInitStep(search);
    search_started = 1;

    pddl_timer_t info_timer;
    pddlTimerStart(&info_timer);
    for (int step = 1; ret == PDDL_SEARCH_CONT; ++step){
        if (search_terminate){
            printSearchStat(search, &err);
            PDDL_INFO2(&err, "Search aborted.");
            pddlSearchDel(search);
            pddlHeurDel(heur);
            PDDL_CTXEND(&err);
            return -1;
        }

        ret = pddlSearchStep(search);
        // TODO: parametrize
        if (step >= 100){
            pddlTimerStop(&info_timer);
            if (pddlTimerElapsedInSF(&info_timer) >= 1.){
                printSearchStat(search, &err);
                pddlTimerStart(&info_timer);
            }
            step = 0;
        }
    }
    printSearchStat(search, &err);

    if (ret == PDDL_SEARCH_UNSOLVABLE){
        PDDL_INFO2(&err, "Problem is unsolvable.");

    }else if (ret == PDDL_SEARCH_FOUND){
        PDDL_INFO2(&err, "Plan found.");
        pddl_plan_t plan;
        pddlPlanInit(&plan);
        pddlSearchExtractPlan(search, &plan);
        PDDL_INFO(&err, "Plan Cost: %d", plan.cost);
        PDDL_INFO(&err, "Plan Length: %d", plan.length);
        PRINT_TO_FILE(&err, opt.ground_planner.plan_out, "plan",
                      pddlPlanPrint(&plan, &fdr.op, fout));
        pddlPlanFree(&plan);
    }else{
        PDDL_FATAL("Unkown return status: %d", ret);
    }

    if (search_terminate){
        PDDL_INFO2(&err, "Search aborted.");
        pddlSearchDel(search);
        pddlHeurDel(heur);
        PDDL_CTXEND(&err);
        return -1;
    }

    pddlSearchDel(search);
    pddlHeurDel(heur);
    return 0;
}

static int fdrHasTNFOps(const pddl_fdr_t *fdr)
{
    for (int oi = 0; oi < fdr->op.op_size; ++oi){
        const pddl_fdr_op_t *op = fdr->op.op[oi];
        for (int i = 0; i < op->eff.fact_size; ++i){
            if (!pddlFDRPartStateIsSet(&op->pre, op->eff.fact[i].var))
                return 0;
        }
        for (int cei = 0; cei < op->cond_eff_size; ++cei){
            const pddl_fdr_op_cond_eff_t *ce = op->cond_eff + cei;
            for (int i = 0; i < ce->eff.fact_size; ++i){
                if (!pddlFDRPartStateIsSet(&ce->pre, ce->eff.fact[i].var))
                    return 0;
            }
        }
    }

    return 1;
}

static void symbaPlanPrint(const pddl_fdr_t *fdr,
                           const pddl_iarr_t *plan,
                           int cost,
                           FILE *fout)
{
    fprintf(fout, ";; Cost: %d\n", cost);
    fprintf(fout, ";; Length: %d\n", pddlIArrSize(plan));
    int op_id;
    PDDL_IARR_FOR_EACH(plan, op_id){
        const pddl_fdr_op_t *op = fdr->op.op[op_id];
        fprintf(fout, "(%s) ;; cost: %ld\n", op->name, (long)op->cost);
    }
}

static int stepSymba(void)
{
    if (opt.symba.search == SYMBA_NONE)
        return 0;

    PDDL_CTX(&err, "symba", "SYMBA");
    if (opt.symba.cfg.fw.use_pot_heur){
        if (fdrHasTNFOps(&fdr)){
            PDDL_INFO2(&err, "fw: Using consistent potential heuristic");
        }else{
            opt.symba.cfg.fw.use_pot_heur = 0;
            opt.symba.cfg.fw.use_pot_heur_inconsistent = 1;
            PDDL_INFO2(&err, "fw: Using inconsistent potential heuristic");
        }
    }else{
        PDDL_INFO2(&err, "fw: Using blind heuristic");
    }

    if (opt.symba.cfg.bw.use_pot_heur_inconsistent){
        PDDL_INFO2(&err, "bw: Using inconsistent potential heuristic");
    }else{
        PDDL_INFO2(&err, "bw: Using blind heuristic");
    }

    if (opt.symba.search == SYMBA_FW){
        opt.symba.cfg.fw.enabled = 1;
        opt.symba.cfg.bw.enabled = 0;
    }else if (opt.symba.search == SYMBA_BW){
        opt.symba.cfg.fw.enabled = 0;
        opt.symba.cfg.bw.enabled = 1;
    }else{
        opt.symba.cfg.fw.enabled = 1;
        opt.symba.cfg.bw.enabled = 1;
    }

    pddl_symbolic_task_t *task;
    if ((task = pddlSymbolicTaskNew(&fdr, &opt.symba.cfg, &err)) == NULL)
        PDDL_TRACE_RET(&err, -1);

    PDDL_IARR(plan);
    int res;
    if (opt.symba.search == SYMBA_FWBW
            && opt.symba.bw_off_if_constr_failed
            && pddlSymbolicTaskGoalConstrFailed(task)){
        PDDL_INFO2(&err, "Switching to fw-only search.");
        res = pddlSymbolicTaskSearchFw(task, &plan, &err);
    }else{
        res = pddlSymbolicTaskSearch(task, &plan, &err);
    }


    if (res == PDDL_SYMBOLIC_PLAN_FOUND){
        int cost = 0;
        int op;
        PDDL_IARR_FOR_EACH(&plan, op)
            cost += fdr.op.op[op]->cost;
        PDDL_INFO(&err, "Plan Cost: %d", cost);
        PDDL_INFO(&err, "Plan Length: %d", pddlIArrSize(&plan));
        PRINT_TO_FILE(&err, opt.symba.out, "plan",
                      symbaPlanPrint(&fdr, &plan, cost, fout));
    }

    pddlIArrFree(&plan);
    pddlSymbolicTaskDel(task);

    PDDL_CTXEND(&err);
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
    pddlErrWarnEnable(&err, stderr);
    pddlErrInfoEnable(&err, stderr);
    FILE *propout = fopen("log.prop", "w");
    pddlErrPropEnable(&err, propout);
    int ret = 0;
    if ((ret = setOptions(argc, argv, &err)) != 0
            || (ret = stepPDDL()) != 0
            || (ret = stepReportLiftedMGroups()) != 0
            || (ret = stepLiftedMGroups()) != 0
            || (ret = stepLiftedEndomorph()) != 0
            || (ret = stepLiftedPlanner()) != 0
            || (ret = stepGround()) != 0
            || (ret = stepReportMGroups()) != 0
            || (ret = stepGroundMGroups()) != 0
            || (ret = stepInferMGroups()) != 0
            || (ret = stepProcessStrips()) != 0
            || (ret = stepReportReversibility()) != 0
            || (ret = stepRedBlackFDR()) != 0
            || (ret = stepFDR()) != 0
            || (ret = stepGroundPlanner()) != 0
            || (ret = stepSymba()) != 0){
        if (ret < 0){
            if (pddlErrIsSet(&err)){
                fprintf(stderr, "Error: ");
                pddlErrPrint(&err, 1, stderr);
            }
            freeData();
            return -1;
        }
    }

    freeData();
    return 0;
}
