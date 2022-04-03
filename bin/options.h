#ifndef OPTIONS_H
#define OPTIONS_H

#include <pddl/pddl.h>
#include "process_strips.h"

#define GROUND_TRIE 0
#define GROUND_SQL 1
#define GROUND_DL 2

#define MG_NONE 0
#define MG_FAM 1
#define MG_H2 2

struct options {
    int help;
    float t;
    int max_mem;
    pddl_files_t files;

    struct {
        int force_adl;
        int remove_empty_types;
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
        int enable;
        int ignore_costs;
    } lifted_endomorph;

    struct {
        int enable;
        pddl_search_lifted_t *(*search_fn)(const pddl_t *pddl,
                                           pddl_homomorphism_heur_t *heur,
                                           pddl_err_t *err);
        pddl_homomorphism_heur_t *(*heur_fn)(const pddl_t *pddl,
                                             const pddl_homomorphism_config_t *cfg,
                                             pddl_err_t *err);
        pddl_homomorphism_config_t homomorph_cfg;
        int homomorph_samples;
        char *plan_out;
    } lifted_planner;

    struct {
        pddl_ground_config_t cfg;
        int method;

        int mgroup;
        int mgroup_remove_subsets;
        char *mgroup_out;
    } ground;

    struct {
        int compile_away_cond_eff;
        pddl_process_strips_t process;
        char *py_out;
        int stop;
    } strips;

    struct {
        int method;
        int fam_lmg;
        int fam_maximal;
        float fam_time_limit;
        int fam_limit;
        int remove_subsets;
        char *out;
    } mg;

    struct {
        int enable;
        pddl_red_black_fdr_config_t cfg;
        char *out;
    } rb_fdr;

    struct {
        unsigned flag;
        unsigned var_flag;
        int order_vars_cg;
        char *out;
        int pretty_print_vars;
        int pretty_print_cg;
    } fdr;

    struct {
        pddl_heur_t *(*heur_fn0)(void);
        pddl_heur_t *(*heur_fn2)(const pddl_fdr_t *fdr, pddl_err_t *err);
        pddl_heur_t *(*heur_fn_pot)(const pddl_fdr_t *fdr,
                                    const pddl_hpot_config_t *cfg,
                                    pddl_err_t *err);
        char *plan_out;

        int use_astar;
        int use_gbfs;
        int use_lazy;

        int use_lmc;
        int use_hmax;
        int use_hadd;
        int use_hff;
        int use_flow;
        int use_pot;
        pddl_hpot_config_t pot_cfg;
    } ground_planner;

    struct {
        int lmg;
        int reversibility_simple;
        int reversibility_iterative;
    } report;

    struct {
        int max_depth;
        int use_mutex;
    } reversibility;
};
typedef struct options options_t;
extern options_t opt;

int setOptions(int argc, char *argv[], pddl_err_t *err);

#endif /* OPTIONS_H */
