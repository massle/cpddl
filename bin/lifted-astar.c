#include <signal.h>
#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

volatile sig_atomic_t terminate = 0;
volatile sig_atomic_t search_started = 0;

void sigHandlerTerminate(int signal)
{
    fprintf(stderr, "Received %s signal\n", strsignal(signal));
    fflush(stderr);
    if (search_started){
        terminate = 1;
    }else{
        exit(-1);
    }
}

struct options {
    int help;
    char *out;
    pddl_files_t files;
} opt;

static void usage(const char *name)
{
    fprintf(stderr, "pddl-astar\n");
    fprintf(stderr, "Usage: %s [OPTIONS] [domain.pddl] problem.pddl\n", name);
    fprintf(stderr, "  OPTIONS:\n");
    optsPrint(stderr, "    ");
    fprintf(stderr, "\n");
}

static int readOpts(int *argc,
                    char *argv[],
                    pddl_hpot_config_t *pot_cfg,
                    bor_err_t *err)
{
    optsAddDesc("help", 'h', OPTS_NONE, &opt.help, NULL,
                "Print this help.");
    optsAddDesc("output", 'o', OPTS_STR, &opt.out, NULL,
                "Output filename (default: stdout)");

    if (opts(argc, argv) != 0 || opt.help || (*argc != 2 && *argc != 3)){
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

        usage(argv[0]);
        return -1;
    }


    if (*argc == 2){
        BOR_INFO(err, "Input file: '%s'", argv[1]);
        if (pddlFiles1(&opt.files, argv[1], err) != 0)
            BOR_TRACE_RET(err, -1);
    }else{ // *argc == 3
        BOR_INFO(err, "Input files: '%s' and '%s'", argv[1], argv[2]);
        if (pddlFiles(&opt.files, argv[1], argv[2], err) != 0)
            BOR_TRACE_RET(err, -1);
    }
    BOR_INFO(err, "PDDL files: '%s' '%s'\n",
             opt.files.domain_pddl, opt.files.problem_pddl);

    return 0;
}

static pddl_homomorphism_heur_t *_heurCollapseAllExceptOneType(
                                            const pddl_t *pddl,
                                            int except,
                                            bor_err_t *err)
{
    pddl_homomorphism_config_t homo_cfg = PDDL_HOMOMORPHISM_CONFIG_INIT;
    for (int type = 0; type < pddl->type.type_size; ++type){
        if (type == except)
            continue;
        if (pddlTypesIsMinimal(&pddl->type, type))
            borISetAdd(&homo_cfg.collapse_types, type);
    }
    pddl_homomorphism_heur_t *heur;
    if ((heur = pddlHomomorphismHeurLMCut(pddl, &homo_cfg, err)) == NULL){
        fprintf(stderr, "Error: ");
        borErrPrint(err, 1, stderr);
        return NULL;
    }
    return heur;
}

static pddl_homomorphism_heur_t *heurCollapseAllExceptOneType(const pddl_t *pddl,
                                                              bor_err_t *err)
{
    pddl_homomorphism_heur_t *heur = NULL;
    int best_hval = -1;
    for (int type = 0; type < pddl->type.type_size; ++type){
        if (pddlTypesIsMinimal(&pddl->type, type)
                && pddlTypeNumObjs(&pddl->type, type) > 1){
            pddl_homomorphism_heur_t *h;
            h = _heurCollapseAllExceptOneType(pddl, type, err);
            if (h == NULL)
                continue;

            int hval = pddlHomomorphismHeurEvalGroundInit(h);
            BOR_INFO(err, "Homomorph heur: Heuristic value for the init: %d",
                     hval);
            if (hval > best_hval && hval != PDDL_COST_DEAD_END){
                if (heur != NULL)
                    pddlHomomorphismHeurDel(heur);
                heur = h;
                best_hval = hval;
            }else{
                pddlHomomorphismHeurDel(h);
            }
        }
    }
    return heur;
}

static void printSearchStat(const pddl_search_lifted_t *astar, bor_err_t *err)
{
    pddl_search_stat_t stat;
    pddlSearchLiftedStat(astar, &stat);
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

static void printPlan(const pddl_lifted_plan_t *plan, FILE *fout)
{
    fprintf(fout, ";; Cost: %d\n", plan->plan_cost);
    fprintf(fout, ";; Length: %d\n", plan->plan_len);
    for (int i = 0; i < plan->plan_len; ++i){
        fprintf(fout, "(%s)\n", plan->plan[i]);
    }
}

int main(int argc, char *argv[])
{
    pddl_hpot_config_t hpot_cfg = PDDL_HPOT_CONFIG_INIT;

    signal(SIGINT, sigHandlerTerminate);
    signal(SIGTERM, sigHandlerTerminate);

    bor_err_t err = BOR_ERR_INIT;
    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);

    if (readOpts(&argc, argv, &hpot_cfg, &err) != 0){
        borErrPrint(&err, 1, stderr);
        return -1;
    }

    // Parse PDDL
    pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
    pddl_cfg.force_adl = 1;
    pddl_t pddl;
    if (pddlInit(&pddl, opt.files.domain_pddl, opt.files.problem_pddl,
                 &pddl_cfg, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }
    pddlNormalize(&pddl);
    pddlCheckSizeTypes(&pddl);
    //pddlPrintDebug(&pddl, stderr);


    pddl_homomorphism_heur_t *heur;
    heur = heurCollapseAllExceptOneType(&pddl, &err);
    if (heur == NULL)
        return -1;

    pddl_search_lifted_t *astar;
    astar = pddlSearchLiftedAStar(&pddl, heur, &err);
    int ret = pddlSearchLiftedInitStep(astar);
    search_started = 1;

    bor_timer_t info_timer;
    borTimerStart(&info_timer);
    for (int step = 1; ret == PDDL_SEARCH_CONT; ++step){
        if (terminate){
            printSearchStat(astar, &err);
            BOR_INFO2(&err, "Search aborted.");
            exit(-1);
        }

        ret = pddlSearchLiftedStep(astar);
        if (step >= 100){
            borTimerStop(&info_timer);
            if (borTimerElapsedInSF(&info_timer) >= 1.){
                printSearchStat(astar, &err);
                borTimerStart(&info_timer);
            }
            step = 0;
        }
    }
    printSearchStat(astar, &err);

    if (ret == PDDL_SEARCH_UNSOLVABLE){
        BOR_INFO2(&err, "Problem is unsolvable.");

    }else if (ret == PDDL_SEARCH_FOUND){
        BOR_INFO2(&err, "Plan found.");
        const pddl_lifted_plan_t *plan = pddlSearchLiftedPlan(astar);
        BOR_INFO(&err, "Plan Cost: %d", plan->plan_cost);
        BOR_INFO(&err, "Plan Length: %d", plan->plan_len);
        if (opt.out == NULL || strcmp(opt.out, "-") == 0){
            printPlan(plan, stdout);
        }else{
            FILE *fout;
            if ((fout = fopen(opt.out, "w")) != NULL){
                printPlan(plan, fout);
                fclose(fout);
            }else{
                BOR_ERR(&err, "Could not open file '%s'", opt.out);
                fprintf(stderr, "Error: ");
                borErrPrint(&err, 1, stderr);
                return -1;
            }
        }
    }else{
        BOR_FATAL("Unkown return status: %d", ret);
    }

    if (terminate){
        BOR_INFO2(&err, "Search aborted.");
        exit(-1);
    }

    pddlSearchLiftedDel(astar);

    optsClear();
    //pddlLiftedMGroupsFree(&lifted_mgroups);
    pddlFree(&pddl);
    return 0;
}
