#include <signal.h>
#include "pddl/pddl.h"
#include "options.h"
#include "print_to_file.h"

static int lifted_search_started = 0;
static int lifted_terminate = 0;

static void liftedPlannerSigHandlerTerminate(int signal)
{
    if (lifted_search_started && lifted_terminate){
        fprintf(stderr, "Received second %s signal\n", strsignal(signal));
        fprintf(stderr, "Forced Exit\n");
        fflush(stderr);
        exit(-1);
    }

    fprintf(stderr, "Received %s signal\n", strsignal(signal));
    fflush(stderr);
    if (lifted_search_started){
        lifted_terminate = 1;
    }else{
        exit(-1);
    }
}

static pddl_homomorphism_heur_t *
    _liftedPlannerHeurCollapseAllExceptOneType(const pddl_t *pddl,
                                               int except,
                                               bor_err_t *err)
{
    pddl_homomorphism_config_t homo_cfg = opt.lifted_planner.homomorph_cfg;
    for (int type = 0; type < pddl->type.type_size; ++type){
        if (type == except)
            continue;
        if (pddlTypesIsMinimal(&pddl->type, type))
            borISetAdd(&homo_cfg.collapse_types, type);
    }
    pddl_homomorphism_heur_t *heur;
    if ((heur = opt.lifted_planner.heur_fn(pddl, &homo_cfg, err)) == NULL){
        fprintf(stderr, "Error: ");
        borErrPrint(err, 1, stderr);
        return NULL;
    }
    return heur;
}

static pddl_homomorphism_heur_t *
    liftedPlannerHeurCollapseAllExceptOneType(const pddl_t *pddl,
                                              bor_err_t *err)
{
    pddl_homomorphism_heur_t *heur = NULL;
    int best_hval = -1;
    for (int type = 0; type < pddl->type.type_size; ++type){
        if (pddlTypesIsMinimal(&pddl->type, type)
                && pddlTypeNumObjs(&pddl->type, type) > 1){
            pddl_homomorphism_heur_t *h;
            h = _liftedPlannerHeurCollapseAllExceptOneType(pddl, type, err);
            if (h == NULL)
                continue;

            int hval = pddlHomomorphismHeurEvalGroundInit(h);
            BOR_INFO(err, "Homomorph heur: Heuristic value for the init: %d", hval);
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

static pddl_homomorphism_heur_t *
    _liftedPlannerHeurCollapseRandom(const pddl_t *pddl,
                                     int seed,
                                     bor_err_t *err)
{
    pddl_homomorphism_config_t homo_cfg = opt.lifted_planner.homomorph_cfg;
    homo_cfg.random_seed = seed;
    pddl_homomorphism_heur_t *heur;
    if ((heur = opt.lifted_planner.heur_fn(pddl, &homo_cfg, err)) == NULL){
        fprintf(stderr, "Error: ");
        borErrPrint(err, 1, stderr);
        return NULL;
    }
    return heur;
}

static pddl_homomorphism_heur_t *
    liftedPlannerHeurCollapseRandom(const pddl_t *pddl, bor_err_t *err)
{
    int seed = opt.lifted_planner.homomorph_cfg.random_seed;
    pddl_homomorphism_heur_t *heur = NULL;
    int best_hval = -1;
    for (int i = 0; i < opt.lifted_planner.homomorph_samples; ++i){
        pddl_homomorphism_heur_t *h;
        h = _liftedPlannerHeurCollapseRandom(pddl, seed, err);
        int hval = pddlHomomorphismHeurEvalGroundInit(h);
        BOR_INFO(err, "Homomorph heur: Heuristic value for the init: %d", hval);
        if (hval > best_hval && hval != PDDL_COST_DEAD_END){
            if (heur != NULL)
                pddlHomomorphismHeurDel(heur);
            heur = h;
            best_hval = hval;
        }else{
            pddlHomomorphismHeurDel(h);
        }
        ++seed;
    }
    return heur;
}

int liftedPlanner(const pddl_t *pddl, bor_err_t *err)
{
    void (*old_sigint)(int);
    void (*old_sigterm)(int);
    old_sigint = signal(SIGINT, liftedPlannerSigHandlerTerminate);
    old_sigterm = signal(SIGTERM, liftedPlannerSigHandlerTerminate);

    BOR_INFO_PREFIX_PUSH(err, "LPLAN: ");
    pddl_homomorphism_heur_t *heur = NULL;
    if (opt.lifted_planner.heur_fn != NULL){
        if (opt.lifted_planner.heur_fn == pddlHomomorphismHeurLMCut){
            BOR_INFO2(err, "cfg.heur = lmc");
        }else if (opt.lifted_planner.heur_fn == pddlHomomorphismHeurHFF){
            BOR_INFO2(err, "cfg.heur = ff");
        }else{
            BOR_INFO2(err, "cfg.heur = unkown !!");
        }
        pddlHomomorphismConfigLog(&opt.lifted_planner.homomorph_cfg,
                                  "cfg.heur.homomorph.", err);
        BOR_INFO(err, "cfg.heur.homomorph_samples = %d",
                 opt.lifted_planner.homomorph_samples);

        if ((opt.lifted_planner.homomorph_cfg.type & 0xfu)
                    == PDDL_HOMOMORPHISM_TYPES){
            heur = liftedPlannerHeurCollapseAllExceptOneType(pddl, err);
        }else{
            heur = liftedPlannerHeurCollapseRandom(pddl, err);
        }
    }
    pddl_search_lifted_t *search;
    search = opt.lifted_planner.search_fn(pddl, heur, err);
    int ret = pddlSearchLiftedInitStep(search);
    lifted_search_started = 1;

    bor_timer_t info_timer;
    borTimerStart(&info_timer);
    for (int step = 1; ret == PDDL_SEARCH_CONT; ++step){
        if (lifted_terminate){
            ret = PDDL_SEARCH_ABORT;
            break;
        }

        ret = pddlSearchLiftedStep(search);
        if (step >= 100){
            borTimerStop(&info_timer);
            if (borTimerElapsedInSF(&info_timer) >= 1.){
                pddlSearchLiftedStatLog(search, err);
                borTimerStart(&info_timer);
            }
            step = 0;
        }
    }
    pddlSearchLiftedStatLog(search, err);

    if (ret == PDDL_SEARCH_UNSOLVABLE){
        BOR_INFO2(err, "Problem is unsolvable.");

    }else if (ret == PDDL_SEARCH_FOUND){
        BOR_INFO2(err, "Plan found.");
        const pddl_lifted_plan_t *plan = pddlSearchLiftedPlan(search);
        BOR_INFO(err, "Plan Cost: %d", plan->plan_cost);
        BOR_INFO(err, "Plan Length: %d", plan->plan_len);
        PRINT_TO_FILE(err, opt.lifted_planner.plan_out, "plan",
                      pddlSearchLiftedPlanPrint(search, fout));

    }else if (ret == PDDL_SEARCH_ABORT){
        BOR_INFO2(err, "Search aborted.");

    }else{
        BOR_FATAL("Unkown return status: %d", ret);
    }

    pddlSearchLiftedDel(search);
    if (heur != NULL)
        pddlHomomorphismHeurDel(heur);
    BOR_INFO_PREFIX_POP(err);
    signal(SIGINT, old_sigint);
    signal(SIGTERM, old_sigterm);
    return 1;
}

