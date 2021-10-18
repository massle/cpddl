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
        pddl_ground_config_t cfg;
        int (*method_fn)(pddl_strips_t *,
                         const pddl_t *,
                         const pddl_ground_config_t *,
                         bor_err_t *);

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
};
typedef struct options options_t;
extern options_t opt;

int setOptions(int argc, char *argv[], bor_err_t *err);

#endif /* OPTIONS_H */
