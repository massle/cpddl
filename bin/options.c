#include <sys/time.h>
#include <sys/resource.h>
#include <libgen.h>
#include <pddl/pddl.h>
#include "print_to_file.h"
#include "options.h"
#include "opts.h"

extern const int is_pddl_fdr;
extern const int is_pddl_symba;
extern const int is_pddl_pddl;

options_t opt = { 0 };

FILE *log_out = NULL;
FILE *prop_out = NULL;

struct op_mutex_cfg {
    int ts;
    int op_fact;
    int hm_op;
    int no_prune;
    char *out;
};
typedef struct op_mutex_cfg op_mutex_cfg_t;

struct h3_cfg {
    float time;
    int mem;
};
typedef struct h3_cfg h3_cfg_t;

struct endomorph_cfg {
    pddl_endomorphism_config_t cfg;
    int fdr;
    int ts;
    int fdr_ts;
};
typedef struct endomorph_cfg endomorph_cfg_t;

static pddl_endomorphism_config_t endomorph_default_cfg = PDDL_ENDOMORPHISM_CONFIG_INIT;

static void hpotSetDisamb(int value, void *_cfg)
{
    pddl_hpot_config_t *cfg = _cfg;
    cfg->disambiguation = value;
    if (value)
        cfg->weak_disambiguation = 0;
}

static void hpotSetWeakDisamb(int value, void *_cfg)
{
    pddl_hpot_config_t *cfg = _cfg;
    cfg->weak_disambiguation = value;
    if (value)
        cfg->disambiguation = 0;
}

static void hpotSetObjSimple(int v, void *_cfg, int type)
{
    pddl_hpot_config_t *cfg = _cfg;
    if (v)
        cfg->obj = type;
}

static void hpotSetObjSample(int v, void *_cfg, int type)
{
    pddl_hpot_config_t *cfg = _cfg;
    if (v){
        cfg->obj = type;
        cfg->num_samples = v;
        cfg->samples_random_walk = 1;
    }
}

static void hpotSetObjMutex(int v, void *_cfg, int type)
{
    pddl_hpot_config_t *cfg = _cfg;
    if (v){
        cfg->obj = type;
        cfg->all_states_mutex_size = v;
    }
}

static void hpotSetObjInit(int v, void *_cfg)
{
    hpotSetObjSimple(v, _cfg, PDDL_HPOT_OBJ_INIT);
}

static void hpotSetObjAllStates(int v, void *_cfg)
{
    hpotSetObjSimple(v, _cfg, PDDL_HPOT_OBJ_ALL_STATES);
}

static void hpotSetObjMaxInitAll(int v, void *_cfg)
{
    hpotSetObjSimple(v, _cfg, PDDL_HPOT_OBJ_MAX_INIT_ALL_STATES);
}

static void hpotSetObjSamplesMax(int v, void *_cfg)
{
    hpotSetObjSample(v, _cfg, PDDL_HPOT_OBJ_SAMPLES_MAX);
}

static void hpotSetObjSamplesSum(int v, void *_cfg)
{
    hpotSetObjSample(v, _cfg, PDDL_HPOT_OBJ_SAMPLES_SUM);
}

static void hpotSetObjDiverse(int v, void *_cfg)
{
    hpotSetObjSample(v, _cfg, PDDL_HPOT_OBJ_DIVERSE);
}

static void hpotSetObjAllMutex(int v, void *_cfg)
{
    hpotSetObjMutex(v, _cfg, PDDL_HPOT_OBJ_ALL_STATES_MUTEX);
}

static void hpotSetObjAllMutexCond(int v, void *_cfg)
{
    hpotSetObjMutex(v, _cfg, PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED);
}

static void hpotSetObjAllMutexCondRand(int v, void *_cfg)
{
    hpotSetObjMutex(v, _cfg, PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND);
}

static void hpotSetObjAllMutexCondRand2(int v, void *_cfg)
{
    hpotSetObjMutex(v, _cfg, PDDL_HPOT_OBJ_ALL_STATES_MUTEX_CONDITIONED_RAND2);
}

static void hpotParams(opts_params_t *params,
                       pddl_hpot_config_t *cfg)
{
    optsParamsAddFlagFn(params, "disam", cfg, hpotSetDisamb);
    optsParamsAddFlagFn(params, "disambiguation", cfg, hpotSetDisamb);
    optsParamsAddFlagFn(params, "D", cfg, hpotSetDisamb);

    optsParamsAddFlagFn(params, "weak-disam", cfg, hpotSetWeakDisamb);
    optsParamsAddFlagFn(params, "weak-disambiguation", cfg, hpotSetWeakDisamb);
    optsParamsAddFlagFn(params, "W", cfg, hpotSetWeakDisamb);

    optsParamsAddFlagFn(params, "init", cfg, hpotSetObjInit);
    optsParamsAddFlagFn(params, "I", cfg, hpotSetObjInit);

    optsParamsAddFlagFn(params, "all", cfg, hpotSetObjAllStates);
    optsParamsAddFlagFn(params, "A", cfg, hpotSetObjAllStates);

    optsParamsAddFlagFn(params, "max-init-all", cfg, hpotSetObjMaxInitAll);

    optsParamsAddFlag(params, "add-init", &cfg->add_init_constr);
    optsParamsAddFlag(params, "+I", &cfg->add_init_constr);

    optsParamsAddIntFn(params, "sample-max", cfg, hpotSetObjSamplesMax);
    optsParamsAddIntFn(params, "sample-sum", cfg, hpotSetObjSamplesSum);
    optsParamsAddIntFn(params, "diverse", cfg, hpotSetObjDiverse);

    optsParamsAddIntFn(params, "all-mutex", cfg, hpotSetObjAllMutex);
    optsParamsAddIntFn(params, "all-mutex-cond", cfg, hpotSetObjAllMutexCond);
    optsParamsAddIntFn(params, "all-mutex-cond-rand", cfg,
                       hpotSetObjAllMutexCondRand);
    optsParamsAddIntFn(params, "all-mutex-cond-rand2", cfg,
                       hpotSetObjAllMutexCondRand2);

    optsParamsAddInt(params, "num-samples", &cfg->num_samples);
}

static int optGroundNoPruning(int enabled)
{
    if (enabled){
        opt.ground.cfg.prune_op_pre_mutex = 0;
        opt.ground.cfg.prune_op_dead_end = 0;
    }
    return 0;
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

static void irrelevance(void)
{
    pddlProcessStripsAddIrrelevance(&opt.strips.process);
}

static void irrelevanceOps(void)
{
    pddlProcessStripsAddIrrelevanceOps(&opt.strips.process);
}

static void removeUselessDelEffs(void)
{
    pddlProcessStripsAddRemoveUselessDelEffs(&opt.strips.process);
}

static void unreachableOps(void)
{
    pddlProcessStripsAddUnreachableOps(&opt.strips.process);
}

static void removeOpsEmptyAddEff(void)
{
    pddlProcessStripsAddRemoveOpsEmptyAddEff(&opt.strips.process);
}

static void famDeadEnd(void)
{
    pddlProcessStripsAddFAMGroupsDeadEndOps(&opt.strips.process);
}

static void deduplicateOps(void)
{
    pddlProcessStripsAddDeduplicateOps(&opt.strips.process);
}

static void sortOps(void)
{
    pddlProcessStripsAddSortOps(&opt.strips.process);
}

static void pruneH2Fw(void)
{
    pddlProcessStripsAddH2Fw(&opt.strips.process, 0.f);
}

static int pruneH2FwLimit(float v)
{
    pddlProcessStripsAddH2Fw(&opt.strips.process, v);
    return 0;
}

static void pruneH2FwBw(void)
{
    pddlProcessStripsAddH2FwBw(&opt.strips.process, 0.f);
}

static int pruneH2FwBwLimit(float v)
{
    pddlProcessStripsAddH2FwBw(&opt.strips.process, v);
    return 0;
}

static void pruneH3Fw(void)
{
    pddlProcessStripsAddH3Fw(&opt.strips.process, 0.f, 0);
}

static void pruneH3FwLimit(void *ud)
{
    h3_cfg_t *cfg = ud;
    size_t mem = cfg->mem;
    mem *= 1024UL * 1024UL;
    pddlProcessStripsAddH3Fw(&opt.strips.process, cfg->time, mem);
    bzero(cfg, sizeof(*cfg));
}

static void h2Alias(void)
{
    unreachableOps();
    irrelevanceOps();
    famDeadEnd();
    pruneH2FwBw();
    irrelevance();
    removeUselessDelEffs();
    deduplicateOps();
}

static void endomorphism(void *ud)
{
    endomorph_cfg_t *cfg = ud;
    if (cfg->fdr){
        pddlProcessStripsAddEndomorphFDR(&opt.strips.process, &cfg->cfg);
    }else if (cfg->ts){
        pddlProcessStripsAddEndomorphTS(&opt.strips.process, &cfg->cfg);
    }else if (cfg->fdr_ts){
        pddlProcessStripsAddEndomorphFDRTS(&opt.strips.process, &cfg->cfg);
    }
    bzero(cfg, sizeof(*cfg));
    cfg->cfg = endomorph_default_cfg;
}

static void opMutex(void *ud)
{
    op_mutex_cfg_t *cfg = ud;
    pddlProcessStripsAddOpMutex(&opt.strips.process,
                                cfg->ts, cfg->op_fact, cfg->hm_op,
                                cfg->no_prune, cfg->out);
    if (cfg->out != NULL)
        PDDL_FREE(cfg->out);
    bzero(cfg, sizeof(*cfg));
}


static void setBaseOptions(void)
{
    optsAddFlag("help", 'h', &opt.help, 0, "Print this help.");
    optsAddInt("max-mem", 0x0, &opt.max_mem, 0,
               "Maximum memory in MB if >0.");
    optsAddStr("log-out", 0x0, &opt.log_out, "stderr",
               "Set output file for logs.");
    optsAddStr("prop-out", 0x0, &opt.prop_out, 0x0,
               "Set output file for properties log.");

}

static void setPddlOptions(void)
{
    optsStartGroup("PDDL:");
    optsAddFlag("force-adl", 0x0, &opt.pddl.force_adl, 1,
                "Force :adl requirement if it is not specified in the"
                " domain file.");
    optsAddFlag("remove-empty-types", 0x0, &opt.pddl.remove_empty_types, 1,
                "Remove empty types");
    optsAddFlag("pddl-ce", 0x0, &opt.pddl.compile_away_cond_eff, 0,
                "Compile away conditional effects on the PDDL level.");
}

static void setLMGOptions(void)
{
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
}

static void setLEndoOptions(void)
{
    optsStartGroup("Lifted Endomorphisms:");
    optsAddFlag("lendo", 0x0, &opt.lifted_endomorph.enable, 0,
                "Enable pruning od PDDL using lifted endomorphisms.");
    optsAddFlag("lendo-ignore-costs", 0x0, &opt.lifted_endomorph.ignore_costs, 0,
                "Ignore costs of actions when inferring lifted endomorphisms.");
}

static void setPddlPostprocessOptions(void)
{
    optsStartGroup("PDDL Post-process:");
    optsAddStr("pddl-domain-out", 0x0, &opt.pddl.domain_out, NULL,
               "Write PDDL domain file.");
    optsAddStr("pddl-problem-out", 0x0, &opt.pddl.problem_out, NULL,
               "Write PDDL problem file.");
    optsAddFlag("pddl-compile-in-lmg", 0x0, &opt.pddl.compile_in_lmg, 0,
                "Compile lifted mutex groups into actions' preconditions.");
    optsAddFlag("pddl-stop", 0x0, &opt.pddl.stop, 0,
                "Stop after processing PDDL.");
}

static void setLiftedPlannerOptions(void)
{
    if (is_pddl_fdr || is_pddl_symba || is_pddl_pddl)
        return;

    pddl_homomorphism_config_t _homomorph_cfg = PDDL_HOMOMORPHISM_CONFIG_INIT;
    opt.lifted_planner.homomorph_cfg = _homomorph_cfg;
    opt.lifted_planner.homomorph_samples = 1;

    opts_params_t *params;
    optsStartGroup("Lifted Planner:");
    optsAddIntSwitch("lplan", 0x0, &opt.lifted_planner.search,
                     "Search algorithm for the lifted planner, one of:\n"
                     "  none - no search (default)\n"
                     "  astar - A*\n"
                     "  gbfs - Greedy Best First Search\n"
                     "  lazy - Greedy Best First Search with lazy evaluation",
                     4,
                     "none", LIFTED_PLAN_NONE,
                     "astar", LIFTED_PLAN_ASTAR,
                     "gbfs", LIFTED_PLAN_GBFS,
                     "lazy", LIFTED_PLAN_LAZY);
    optsAddIntSwitch("lplan-h", 0x0, &opt.lifted_planner.heur,
                     "Heuristic function for the lifted planner, one of:\n"
                     "  blind - Blind heuristic (default)\n"
                     "  hmax - lifted h^max\n"
                     "  hadd - lifted h^add\n"
                     "  homo-lmc - Homomorphism-based LM-Cut heuristic (see --lplan-h-homo)\n"
                     "  homo-ff - Homomorphism-based FF heuristic (see --lplan-h-homo)",
                     5,
                     "blind", LIFTED_PLAN_HEUR_BLIND,
                     "hmax", LIFTED_PLAN_HEUR_HMAX,
                     "hadd", LIFTED_PLAN_HEUR_HADD,
                     "homo-lmc", LIFTED_PLAN_HEUR_HOMO_LMC,
                     "homo-ff", LIFTED_PLAN_HEUR_HOMO_FF);
    params = optsAddParams("lplan-h-homo", 0x0,
        "Configuration of the homomorphism for the"
        " homomorphism-based heuristics.\n"
        "Possible options:\n"
        "  type = types|rnd-objs|gaif|rpg\n"
        "  endomorph = <bool> -- enables lifted endomorphism (default: false)\n"
        "  endomorph-ignore-cost = <bool> -- endomorphism ignores costs (default: false)\n"
        "  rm-ratio = <float> -- ratio of removed objects\n"
        "  seed = <int> -- random seed\n"
        "  keep-goal-objs = <bool> -- do not collapse goal objects (default: true)\n"
        "  samples = <int> -- number of samples from which 1 is selected (default: 1)\n"
        "  rpg-max-depth = <int> -- maximum depth used for the rpg method (default: 2)"
        );
    optsParamsAddIntSwitch(params, "type",
                           &opt.lifted_planner.homomorph_cfg.type,
                           5,
                           "types", PDDL_HOMOMORPHISM_TYPES,
                           "rnd-objs", PDDL_HOMOMORPHISM_RAND_OBJS,
                           "gaifmain", PDDL_HOMOMORPHISM_GAIFMAN,
                           "gaif", PDDL_HOMOMORPHISM_GAIFMAN,
                           "rpg", PDDL_HOMOMORPHISM_RPG);
    optsParamsAddFlag(params, "endomorph",
                      &opt.lifted_planner.homomorph_cfg.use_endomorphism);
    optsParamsAddFlag(params, "endomorph-ignore-costs",
                      &opt.lifted_planner.homomorph_cfg.endomorphism_cfg.ignore_costs);
    optsParamsAddFlt(params, "rm-ratio",
                     &opt.lifted_planner.homomorph_cfg.rm_ratio);
    optsParamsAddInt(params, "seed", &opt.lifted_planner.random_seed);
    optsParamsAddFlag(params, "keep-goal-objs",
                      &opt.lifted_planner.homomorph_cfg.keep_goal_objs);
    optsParamsAddInt(params, "samples",
                     &opt.lifted_planner.homomorph_samples);
    optsParamsAddInt(params, "rpg-max-depth",
                     &opt.lifted_planner.homomorph_cfg.rpg_max_depth);

    optsAddStr("lplan-out", 0x0, &opt.lifted_planner.plan_out, NULL,
               "Output filename for the found plan.");
    optsAddStr("lplan-o", 0x0, &opt.lifted_planner.plan_out, NULL,
               "Alias for --lplan-out");
}

static void setGroundOptions(void)
{
    if (is_pddl_pddl)
        return;
    opt.ground.cfg.lifted_mgroups = NULL;
    opt.ground.cfg.remove_static_facts = 1;
    opt.ground.method = GROUND_DL;

    optsStartGroup("Grounding:");
    optsAddIntSwitch("ground", 'G', &opt.ground.method,
                     "Grounding method, one of:\n"
                     "  dl - datalog-based grounding method (default)\n"
                     "  sql - sqlite-based grounding method\n"
                     "  trie - default grounding method",
                     4,
                     "trie", GROUND_TRIE,
                     "sql", GROUND_SQL,
                     "dl", GROUND_DL,
                     "datalog", GROUND_DL);
    optsAddFlag("ground-prune-mutex", 0x0,
                &opt.ground.cfg.prune_op_pre_mutex, 1,
                "Prune during grounding by checking preconditions of operators");
    optsAddFlag("ground-prune-de", 0x0,
                &opt.ground.cfg.prune_op_dead_end, 1,
                "Prune during grounding by checking dead-ends");
    optsAddFlag("ground-prune-dead-end", 0x0,
                &opt.ground.cfg.prune_op_dead_end, 1,
                "Alias for --ground-prune-dead-end");
    optsAddFlagFn("ground-prune-none", 0x0, optGroundNoPruning,
                  "Alias for --no-ground-prune-mutex --no-ground-prune-de");
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
    optsAddStr("strips-fam-dump", 0x0, &opt.strips.fam_dump, NULL,
               "Compute fam-groups and dump the corresponding mutex pairs"
               " to the specified file.");
    optsAddStr("strips-h2-dump", 0x0, &opt.strips.h2_dump, NULL,
               "Compute h^2 and dump the mutexes to the specified file.");
    optsAddStr("strips-h3-dump", 0x0, &opt.strips.h3_dump, NULL,
               "Compute h^3 and dump the mutexes to the specified file.");
    optsAddFlag("strips-stop", 0x0, &opt.strips.stop, 0,
                "Stop after grounding to STRIPS.");
}

static void setMutexGroupOptions(void)
{
    if (is_pddl_pddl)
        return;
    optsStartGroup("Mutex Groups:");
    optsAddIntSwitch("mg", 0x0, &opt.mg.method,
                     "Method for inference of mutex groups, one of:\n"
                     "  0/n/none - no mutex groups will be inferred on STRIPS level (default)\n"
                     "  fam - fact-alternating mutex groups\n"
                     "  h2 - mutex groups from h^2 mutexes\n",
                     5,
                     "none", MG_NONE,
                     "n", MG_NONE,
                     "0", MG_NONE,
                     "fam", MG_FAM,
                     "h2", MG_H2);
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
    optsAddFlag("mg-cover-num", 0x0, &opt.mg.cover_number, 0,
                "Compute cover number of the inferred mutex groups.");
}

static void setProcessStripsOptions(void)
{
    if (is_pddl_pddl)
        return;
    opts_params_t *params;

    pddlProcessStripsInit(&opt.strips.process);

    optsStartGroup("Process STRIPS:");
    optsAddFlagFn2("P-irr", 0x0, irrelevance, "Irrelevance analysis.");
    optsAddFlagFn2("P-irr-op", 0x0, irrelevanceOps,
                   "As --P-irr but removes only operators.");
    optsAddFlagFn2("P-rm-useless-del-effs", 0x0, removeUselessDelEffs,
                   "Remove delete effects that can never be used.");
    optsAddFlagFn2("P-unreachable-op", 0x0, unreachableOps,
                   "Remove unreachable operators based on mutexes.");
    optsAddFlagFn2("P-rm-ops-empty-add-eff", 0x0, removeOpsEmptyAddEff,
                   "Remove operators with empty add effects.");
    optsAddFlagFn2("P-fam-dead-end", 0x0, famDeadEnd,
                   "Remove dead-end operators using fam-groups (see --mg fam).");
    optsAddFlagFn2("P-dedup", 0x0, deduplicateOps,
                   "Remove duplicate operators.");
    optsAddFlagFn2("P-sort", 0x0, sortOps, "Sort operators by their names.");
    optsAddFlagFn2("P-h2fw", 0x0, pruneH2Fw,
                   "Prune with h^2 in forward direction without time limit.");
    optsAddFltFn("P-h2fw-time-limit", 0x0, pruneH2FwLimit,
                 "Prune with h^2 in forward direction with the specified time limit.");
    optsAddFlagFn2("P-h2fwbw", 0x0, pruneH2FwBw,
                   "Prune with h^2 in forward/backward direction without time limit.");
    optsAddFltFn("P-h2fwbw-time-limit", 0x0, pruneH2FwBwLimit,
                 "Prune with h^2 in forward/backward direction with the specified time limit.");
    optsAddFlagFn2("P-h3fw", 0x0, pruneH3Fw,
                   "Prune with h^3 in forward direction without time limit.");

    h3_cfg_t h3_cfg = { 0 };
    params = optsAddParamsAndFn("P-h3fw-limit", 0x0,
                                "Prune with h^3 with the specified limits.\n"
                                "Options:\n"
                                "  time = <float> -- time limit in s\n"
                                "  mem = <int> -- excess memory in MB",
                                &h3_cfg, pruneH3FwLimit);
    optsParamsAddFlt(params, "time", &h3_cfg.time);
    optsParamsAddInt(params, "mem", &h3_cfg.mem);

    endomorph_cfg_t endomorph_cfg = { 0 };
    endomorph_cfg.cfg = endomorph_default_cfg;
    params = optsAddParamsAndFn("P-endo", 0x0,
                                "Endomorphism.\n"
                                "Options:\n"
                                "  fdr = <bool> -- use FDR\n"
                                "  mg-strips = <bool> -- use MG-STRIPS\n"
                                "  ts = <bool> -- use factored transition system\n"
                                "  fdr-ts = <bool> -- use FDR and then factored TS\n"
                                "  max-time = <float> -- time limit (default: 1 hour)\n"
                                "  max-search-time = <float> -- time limit for the search part (default: 1 hour)\n"
                                "  num-threads = <int> -- number of threads for the solved (default: 1)\n"
                                "  fork = <bool> -- run in subprocess (default: true)\n"
                                "  ignore-costs = <bool> -- ignore operator costs (default: false)",
                                &endomorph_cfg, endomorphism);
    optsParamsAddFlag(params, "fdr", &endomorph_cfg.fdr);
    optsParamsAddFlag(params, "ts", &endomorph_cfg.ts);
    optsParamsAddFlag(params, "fdr-ts", &endomorph_cfg.fdr_ts);
    optsParamsAddFlt(params, "max-time", &endomorph_cfg.cfg.max_time);
    optsParamsAddFlt(params, "max-search-time", &endomorph_cfg.cfg.max_search_time);
    optsParamsAddInt(params, "num-threads", &endomorph_cfg.cfg.num_threads);
    optsParamsAddFlag(params, "fork", &endomorph_cfg.cfg.run_in_subprocess);
    optsParamsAddFlag(params, "ignore-costs", &endomorph_cfg.cfg.ignore_costs);


    op_mutex_cfg_t opm_cfg = { 0 };
    params = optsAddParamsAndFn("P-opm", 0x0,
                                "Operator mutexes.\n"
                                "Options:\n"
                                "  ts = <bool> -- use transition systems\n"
                                "  op-fact = <int> -- op-fact compilation\n"
                                "  hm-op = <int> -- h^m from each operator\n"
                                "  no-prune = <bool> -- disabled pruning\n"
                                "  out = <str> -- path to file where operator mutex are stored",
                                &opm_cfg, opMutex);
    optsParamsAddFlag(params, "ts", &opm_cfg.ts);
    optsParamsAddInt(params, "op-fact", &opm_cfg.op_fact);
    optsParamsAddInt(params, "hm-op", &opm_cfg.hm_op);
    optsParamsAddFlag(params, "no-prune", &opm_cfg.no_prune);
    optsParamsAddStr(params, "out", &opm_cfg.out);

    optsAddFlagFn2("h2", 0x0, h2Alias,
                   "Alias for --P-{unreachable-op,irr-op,fam-dead-end,"
                   "h2fwbw,irr,rm-useless-del-effs,dedup}"
                   " (set by default for pddl-symba)");

    if (is_pddl_symba){
        h2Alias();
        sortOps();
    }
}

static void setRedBlackOptions(void)
{
    if (is_pddl_fdr || is_pddl_symba || is_pddl_pddl)
        return;

    pddl_red_black_fdr_config_t _rb_cfg = PDDL_RED_BLACK_FDR_CONFIG_INIT;
    opt.rb_fdr.cfg = _rb_cfg;

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
}

static void setFDROptions(void)
{
    if (is_pddl_pddl)
        return;

    opts_params_t *params;

    opt.fdr.var_flag = PDDL_FDR_VARS_LARGEST_FIRST;
    pddl_hpot_config_t _pot_cfg = PDDL_HPOT_CONFIG_INIT;
    opt.fdr.pot_cfg = _pot_cfg;

    if (is_pddl_fdr)
        opt.fdr.out = "-";

    optsStartGroup("Finite Domain Representation:");
    char desc[512];
    sprintf(desc, "Sort FDR variables with largest first%s.",
            (is_pddl_symba ? "" : " (default)"));
    optsAddFlagFn("fdr-largest", 0x0, optFDRLargestFirst, desc);
    sprintf(desc, "Sort FDR variables with essential first%s.",
            (is_pddl_symba ? " (default)" : ""));
    optsAddFlagFn("fdr-essential", 0x0, optFDREssentialFirst, desc);
    optsAddFlagFn("fdr-ess", 0x0, optFDREssentialFirst,
                  "Alias for --fdr-essential.");
    optsAddFlag("fdr-order-vars-cg", 0x0, &opt.fdr.order_vars_cg, 1,
                "Order FDR variables using causal graph.");
    optsAddStr("fdr-out", 'o', &opt.fdr.out, opt.fdr.out,
               "Output filename for FDR encoding of the task.");
    optsAddFlag("fdr-pretty-print-vars", 0x0, &opt.fdr.pretty_print_vars, 0,
                "Log FDR variables.");
    optsAddFlag("fdr-pretty-print-cg", 0x0, &opt.fdr.pretty_print_cg, 0,
                "Log FDR causal graph.");
    optsAddFlag("fdr-pot", 0x0, &opt.fdr.pot, 0,
                "Generate potential heuristics as part of the FDR output.");
    params = optsAddParams("fdr-pot-cfg", 0x0,
               "Configuration for the potential heuristic (if --fdr-pot is used).\n"
               "See --gplan-pot for the description of options.");
    hpotParams(params, &opt.fdr.pot_cfg);
    optsAddFlag("fdr-tnf", 0x0, &opt.fdr.to_tnf, 0,
                "Transform FDR to Transition Normal Form.");
    optsAddFlag("fdr-tnfm", 0x0, &opt.fdr.to_tnf_multiply, 0,
                "Transform FDR operators to TNF by multiplying its"
                " preconditions.");

    if (is_pddl_symba)
        optFDREssentialFirst(1);
}

static void setGroundPlannerOptions(void)
{
    if (is_pddl_fdr || is_pddl_symba || is_pddl_pddl)
        return;

    pddl_hpot_config_t _pot_cfg = PDDL_HPOT_CONFIG_INIT;
    opt.ground_planner.pot_cfg = _pot_cfg;

    opts_params_t *params;
    optsStartGroup("Grounded Planner:");
    optsAddIntSwitch("gplan", 0x0, &opt.ground_planner.search,
                     "Search algorithm for the grounded planner, one of:\n"
                     "  none - no search (default)\n"
                     "  astar - A*\n"
                     "  gbfs - Greedy Best First Search\n"
                     "  lazy - Greedy Best First Search with lazy evaluation",
                     4,
                     "none", GROUND_PLAN_NONE,
                     "astar", GROUND_PLAN_ASTAR,
                     "gbfs", GROUND_PLAN_GBFS,
                     "lazy", GROUND_PLAN_LAZY);
    optsAddIntSwitch("gplan-h", 0x0, &opt.ground_planner.heur,
                     "Heuristic function for the grounded planner, one of:\n"
                     "  blind - Blind heuristic (default)\n"
                     "  lmc - LM-Cut\n"
                     "  max/hmax - h^max\n"
                     "  add/hadd - h^add\n"
                     "  ff/hff - FF heuristic\n"
                     "  flow - Flow heuristic\n"
                     "  pot - Potential heuristic",
                     11,
                     "none", GROUND_PLAN_HEUR_BLIND,
                     "blind", GROUND_PLAN_HEUR_BLIND,
                     "lmc", GROUND_PLAN_HEUR_LMC,
                     "max", GROUND_PLAN_HEUR_MAX,
                     "hmax", GROUND_PLAN_HEUR_MAX,
                     "add", GROUND_PLAN_HEUR_ADD,
                     "hadd", GROUND_PLAN_HEUR_ADD,
                     "ff", GROUND_PLAN_HEUR_FF,
                     "hff", GROUND_PLAN_HEUR_FF,
                     "flow", GROUND_PLAN_HEUR_FLOW,
                     "pot", GROUND_PLAN_HEUR_POT);

    params = optsAddParams("gplan-pot", 0x0,
        "Configuration for the potential heuristic"
        " (if --gplan-h pot is used)."
        "Options:\n"
        "  D/disamb = <bool> -- turns on disambiguation (default: true)\n"
        "  W/weak-disamb = <bool> -- turns on weak disambiguation (default: false)\n"
        "  I/init = <bool> -- sets objective to initial state\n"
        "  A/all = <bool> -- sets objective to all syntactic states (default: true)\n"
        "  max-init-all = <bool> -- sets objective to the maximum of I and A\n"
        "  +I/add-init = <bool> -- adds constraint on the inital state (default: true)\n"
        "  sample-max = <int> -- maximum over the specified number of samples states\n"
        "  sample-sum = <int> -- optimize for the sum over the specified number of sampled states\n"
        "  diverse = <int> -- diversification over the specified number states\n"
        "  all-mutex = <int> -- all syntactic states respecting mutexes of the given size\n"
        "  all-mutex-cond = <int> -- conditioned ensemble\n"
        "  all-mutex-cond-rand = <int> -- conditioned on <num-samples> fact sets\n"
        "  all-mutex-cond-rand2 = <int>\n"
        "  num-samples = <int> -- sets number of samples"
        );
    hpotParams(params, &opt.ground_planner.pot_cfg);

    optsAddStr("gplan-out", 0x0, &opt.ground_planner.plan_out, NULL,
               "Output filename for the found plan.");
    optsAddStr("gplan-o", 0x0, &opt.ground_planner.plan_out, NULL,
               "Alias for --gplan-out");
}

static void setSymbaOptions(void)
{
    if (is_pddl_fdr || is_pddl_pddl)
        return;

    opts_params_t *params;

    pddl_symbolic_task_config_t _symba_cfg = PDDL_SYMBOLIC_TASK_CONFIG_INIT;
    opt.symba.cfg = _symba_cfg;

    if (is_pddl_symba)
        opt.symba.search = SYMBA_FWBW;

    optsStartGroup("Symbolic Search:");
    optsAddIntSwitch("symba", 0x0, &opt.symba.search,
                     "Symbolic search, one of:\n"
                     "  none -- symbolic search disabled (default)\n"
                     "  fw -- forward-only search\n"
                     "  bw -- backward-only search\n"
                     "  fwbw/bi -- bi-directional search (default fot pddl-symba)",
                     5,
                     "none", SYMBA_NONE,
                     "fw", SYMBA_FW,
                     "bw", SYMBA_BW,
                     "fwbw", SYMBA_FWBW,
                     "bi", SYMBA_FWBW);
    optsAddInt("symba-fam", 0x0, &opt.symba.cfg.fam_groups, 0,
               "Infer at most the specified number of goal-aware fam-groups.");
    optsAddFlag("symba-fw-pot", 0x0, &opt.symba.cfg.fw.use_pot_heur, 0,
                "Use potential heuristics in the forward search.");
    params = optsAddParams("symba-fw-pot-cfg", 0x0,
                           "Configuration of the potential heuristic for"
                           " the forward search. (See --gplan-pot)");
    hpotParams(params, &opt.symba.cfg.fw.pot_heur_config);
    optsAddFlag("symba-bw-pot", 0x0,
                &opt.symba.cfg.bw.use_pot_heur, 0,
                "Use potential heuristics in the backward search."
                " Note that this will always be treated as inconsistent.");
    params = optsAddParams("symba-bw-pot-cfg", 0x0,
                           "Configuration of the potential heuristic for"
                           " the backward search. (See --gplan-pot)");
    hpotParams(params, &opt.symba.cfg.bw.pot_heur_config);
    optsAddFlt("symba-goal-constr-max-time", 0x0,
               &opt.symba.cfg.goal_constr_max_time, 30.f,
               "Set the time limit for applying mutex constraints on the"
               " set of goal states.");
    optsAddFlag("symba-bw-goal-split", 0x0,
                &opt.symba.cfg.bw.use_goal_splitting, 1,
                "Use goal-splitting for backward search if potential"
                " heuristic is used.");
    optsAddFlt("symba-fw-tr-merge-max-time", 0x0,
               &opt.symba.cfg.fw.trans_merge_max_time, 10.,
               "Time limit for merging transition relations in the forward"
               " direction.");
    optsAddFlt("symba-bw-tr-merge-max-time", 0x0,
               &opt.symba.cfg.bw.trans_merge_max_time, 10.,
               "Time limit for merging transition relations in the backward"
               " direction.");
    optsAddFlag("symba-bw-off", 0x0,
                &opt.symba.bw_off_if_constr_failed, 1,
                "Turn off backward search in case of bi-directional search"
                " when mutex constraints could not be applied within\n"
                "the time limit (see also --symba-goal-constr-max-time).");
    optsAddFlt("symba-bw-step-time-limit", 0x0,
               &opt.symba.cfg.bw.step_time_limit, 180.,
               "Time limit (in seconds) for a single step in the backward"
               " direction in case of bi-directional search.\n"
               "If the time limit is reached the backward search is disabled.");
    optsAddFlag("symba-log-every-step", 0x0,
                &opt.symba.cfg.log_every_step, 0,
                "Log every step of during the search.");
    optsAddStr("symba-out", 0x0, &opt.symba.out, NULL,
               "Output file for the plan.");
}

static void setReversibilityOptions(void)
{
    if (is_pddl_fdr || is_pddl_symba || is_pddl_pddl)
        return;

    optsStartGroup("Reversibility:");
    optsAddInt("reversibility-max-depth", 0x0, &opt.reversibility.max_depth, 1,
               "Maximum depth when searching for reversible plans"
               " (also see --report-reversibility*).");
    optsAddFlag("reversibility-use-mutex", 0x0, &opt.reversibility.use_mutex, 0,
                "Use mutexes when search for reversible plans"
                " (also see --report-reversibility*).");
}

static void setReportsOptions(void)
{
    if (is_pddl_fdr || is_pddl_symba || is_pddl_pddl)
        return;

    optsStartGroup("Reports:");
    optsAddFlag("report-lmg", 0x0, &opt.report.lmg, 0,
                "Create report of lifted mutex groups.");
    optsAddFlag("report-reversibility-simple", 0x0,
                &opt.report.reversibility_simple, 0,
                "Compute reversibility with the \"simple\" method.");
    optsAddFlag("report-reversibility-iterative", 0x0,
                &opt.report.reversibility_iterative, 0,
                "Compute reversibility with the \"iterative\" method.");
    optsAddFlag("report-mgroups", 0x0, &opt.report.mgroups, 0,
                "Report on mutex groups.");
}

static void help(const char *argv0, FILE *fout)
{
    fprintf(fout, "Usage: %s [OPTIONS] [domain.pddl] problem.pddl\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "OPTIONS:\n");
    optsPrint(fout);
}

int setOptions(int argc, char *argv[], pddl_err_t *err)
{
    setBaseOptions();
    setPddlOptions();
    setLMGOptions();
    setLEndoOptions();
    setPddlPostprocessOptions();
    setLiftedPlannerOptions();
    setGroundOptions();
    setMutexGroupOptions();
    setProcessStripsOptions();
    setRedBlackOptions();
    setFDROptions();
    setGroundPlannerOptions();
    setSymbaOptions();
    setReversibilityOptions();
    setReportsOptions();

    if (is_pddl_pddl)
        opt.pddl.stop = 1;

    if (opts(&argc, argv) != 0)
        return -1;

    if (opt.help){
        help(argv[0], stderr);
        return -1;
    }

    if (opt.lmg.fd_monotonicity)
        opt.lmg.fd = 1;

    if (argc != 3 && argc != 2){
        for (int i = 1; i < argc; ++i){
            fprintf(stderr, "Error: Unrecognized argument: %s\n", argv[i]);
        }
        help(argv[0], stderr);
        return -1;
    }

    if (opt.log_out != NULL){
        log_out = openFile(opt.log_out);
        pddlErrWarnEnable(err, log_out);
        pddlErrInfoEnable(err, log_out);
    }

    if (opt.prop_out != NULL){
        prop_out = openFile(opt.prop_out);
        pddlErrPropEnable(err, prop_out);
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

    if (opt.lifted_planner.random_seed > 0){
        opt.lifted_planner.homomorph_cfg.random_seed
                = opt.lifted_planner.random_seed;
    }

    PDDL_LOG(err, "Version: %{version}s", pddl_version);
    return 0;
}
