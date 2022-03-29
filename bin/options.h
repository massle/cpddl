#ifndef OPTIONS_H
#define OPTIONS_H

#include <pddl/pddl.h>
#include "process_strips.h"

struct options {
    int help;
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
        int (*method_fn)(pddl_strips_t *,
                         const pddl_t *,
                         const pddl_ground_config_t *,
                         pddl_err_t *);

        int mgroup;
        int mgroup_remove_subsets;
        char *mgroup_out;
    } ground;

    struct {
        int compile_away_cond_eff;
        pddl_process_strips_t process;
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
        int lmg;
    } report;

    struct {
        int enable;
        char *plan_out;
    } astar;
};
typedef struct options options_t;
extern options_t opt;

int setOptions(int argc, char *argv[], pddl_err_t *err);

#endif /* OPTIONS_H */
