#include <stdio.h>
#include <pddl/pddl.h>
#include <opts.h>

struct options {
    int help;
    int print;
    int force_adl;
    int compile_away_cond_eff;
    int compile_away_cond_eff_pddl;
    int max_candidates;
    int max_mgroups;
    int ground;
    int lmg;
    int lmg_fd;
    int lmg_fd_monotonicity;
    int lift_prune;
    int lift_prune_pre;
    int lift_prune_dead_end;
    int ground_lmg;
    int fam_maximal;
    int fam_lmg;
    int h2_mutex;
    int h2_mgroup;
    int mgroup_cover_num;
    int cmp_mgroup_dominance;
    int fdr_vars;
} o;

static void mgroupCoverNumber(const pddl_mgroups_t *mgroups,
                              const pddl_strips_t *strips,
                              bor_err_t *err)
{
    if (o.mgroup_cover_num){
        BOR_INFO2(err, "Computing mutex group cover number");
        int num = pddlMGroupsCoverNumber(mgroups, strips->fact.fact_size);
        BOR_INFO(err, "Mutex group cover number: %d", num);
    }
}

static void fdrVars(const pddl_strips_t *strips,
                    const pddl_mgroups_t *mgroups,
                    bor_err_t *err)
{
    if (!o.fdr_vars)
        return;
    pddl_mutex_pairs_t mutex;
    pddlMutexPairsInitStrips(&mutex, strips);
    pddlMutexPairsAddMGroups(&mutex, mgroups);

    unsigned flags = PDDL_FDR_VARS_LARGEST_FIRST;
    flags |= PDDL_FDR_VARS_NO_NEGATED_FACTS;
    BOR_INFO2(err, "Creating FDR variables...");
    pddl_fdr_vars_t vars;
    pddlFDRVarsInitFromStrips(&vars, strips, mgroups, &mutex, flags);
    BOR_INFO(err, "Created FDR variables: %d", vars.var_size);
    //pddlFDRVarsPrintDebug(&vars, stderr);
    pddlFDRVarsFree(&vars);

    pddlMutexPairsFree(&mutex);
}

static void mgroupDominance(const pddl_mgroups_t *m1,
                            const pddl_mgroups_t *m2,
                            const char *m1_name,
                            const char *m2_name,
                            bor_err_t *err)
{
    int dom = 0;
    for (int i = 0; i < m1->mgroup_size; ++i){
        const pddl_mgroup_t *mi = m1->mgroup + i;
        int found = 0;
        for (int j = 0; j < m2->mgroup_size; ++j){
            const pddl_mgroup_t *mj = m2->mgroup + j;
            if (pddlISetEq(&mi->mgroup, &mj->mgroup)
                    || pddlISetIsSubset(&mi->mgroup, &mj->mgroup)){
                found = 1;
                break;
            }
        }
        if (!found)
            dom += 1;
    }
    BOR_INFO(err, "mutex group dominance %s > %s: %d", m1_name, m2_name, dom);
}

int main(int argc, char *argv[])
{
    pddl_config_t cfg = PDDL_CONFIG_INIT;
    pddl_t pddl;
    bor_err_t err = BOR_ERR_INIT;
    pddl_strips_t strips;
    pddl_lifted_mgroups_t lifted_mgroups;
    pddl_lifted_mgroups_t monotonicity_invariants;
    pddl_mgroups_t lmg_mgroups;
    pddl_mgroups_t h2_mgroups;
    pddl_mgroups_t fam_mgroups;
    pddl_files_t files;

    bzero(&o, sizeof(o));
    o.max_candidates = 10000;
    o.max_mgroups = 10000;

    optsAddDesc("help", 'h', OPTS_NONE, &o.help, NULL,
                "Print this help.");
    optsAddDesc("print", 'p', OPTS_NONE, &o.print, NULL,
                "Print the inferred mutex groups.");
    optsAddDesc("adl", 'a', OPTS_NONE, &o.force_adl, NULL,
                "Force :adl requirement even if it is not specified in the"
                " domain file. (default: off)");
    optsAddDesc("ce", 0x0, OPTS_NONE, &o.compile_away_cond_eff, NULL,
                "Compile away conditional effects on the STRIPS level"
                " (recommended). (default: off)");
    optsAddDesc("ce-pddl", 0x0, OPTS_NONE, &o.compile_away_cond_eff_pddl, NULL,
                "Compile away conditional effects on the PDDL level."
                " (default: off)");
    optsAddDesc("max-candidates", 0x0, OPTS_INT, &o.max_candidates, NULL,
                "Maximum number of mutex group candidates. (default: 10000)");
    optsAddDesc("max-mgroups", 0x0, OPTS_INT, &o.max_mgroups, NULL,
                "Maximum number of mutex group. (default: 10000)");
    optsAddDesc("ground", 'g', OPTS_NONE, &o.ground, NULL,
                "Ground PDDL to STRIPS. (default: off)");
    optsAddDesc("lmg", 0x0, OPTS_NONE, &o.lmg, NULL,
                "Find lifted mutex groups (improved algorithm)."
                " (default: off)");
    optsAddDesc("lmg-fd", 0x0, OPTS_NONE, &o.lmg_fd, NULL,
                "Find Fast-Downward type of invariants. (default: off)");
    optsAddDesc("lmg-fd-mono", 0x0, OPTS_NONE, &o.lmg_fd_monotonicity, NULL,
                "Find Fast-Downward monotonicit invariants, requires --fd."
                " (default: off)");
    optsAddDesc("lift-prune", 0x0, OPTS_NONE, &o.lift_prune, NULL,
                "Use lifted mutex groups for pruning during grounding,"
                " requires --lmg* and -g. (default: off)");
    optsAddDesc("lift-prune-pre", 0x0, OPTS_NONE, &o.lift_prune_pre, NULL,
                "Use lifted mutex groups for pruning during grounding by"
                " checking preconditions."
                " requires --lmg* and -g. (default: off)");
    optsAddDesc("ground-lmg", 0x0, OPTS_NONE, &o.ground_lmg, NULL,
                "Ground lifted mutex groups, requires -g and --lmg*."
                " (default: off)");
    optsAddDesc("fam", 0x0, OPTS_NONE, &o.fam_maximal, NULL,
                "Infer all maximal fam-groups using ILP-based algorithm,"
                "requires -g. (default: off)");
    optsAddDesc("fam-lmg", 0x0, OPTS_NONE, &o.fam_lmg, NULL,
                "Infer all maximal fam-groups using ILP-based algorithm"
                " with lifted mgroups as input,"
                "requires --fam and -g and --lmg* and --ground-lmg."
                " (default: off)");
    optsAddDesc("h2-mutex", 0x0, OPTS_NONE, &o.h2_mutex, NULL,
                "Infer h2-mutexes, requires -g. (default: off)");
    optsAddDesc("h2-mgroup", 0x0, OPTS_NONE, &o.h2_mgroup, NULL,
                "Infer maximal mutex groups from h2-mutexes,"
                " requires --h2-mutex. (default: off)");
    optsAddDesc("mgroup-cover-num", 0x0, OPTS_NONE, &o.mgroup_cover_num, NULL,
                "Compute mutex group cover number. (default: off)");
    optsAddDesc("cmp-mgroup-dominance", 0x0, OPTS_NONE,
                &o.cmp_mgroup_dominance, NULL,
                "Compute dominance between mutex groups, which method found"
                " mutex groups not found by the other method. (default: off)");
    optsAddDesc("fdr-vars", 0x0, OPTS_NONE, &o.fdr_vars, NULL,
                "Find how many variables are created from the inferred"
                " mutex groups. (default: off)");

    if (opts(&argc, argv) || o.help || (argc != 3 && argc != 2)){
        fprintf(stderr, "Usage: %s [OPTIONS] domain.pddl problem.pddl\n",
                argv[0]);
        fprintf(stderr, "  OPTIONS:\n");
        optsPrint(stderr, "    ");
        fprintf(stderr, "\n");
        return -1;
    }

    borErrWarnEnable(&err, stderr);
    borErrInfoEnable(&err, stderr);

    cfg.force_adl = 0;
    if (o.force_adl)
        cfg.force_adl = 1;

    if (o.lmg_fd_monotonicity && !o.lmg_fd){
        fprintf(stderr, "Error: --lmg-fd-mono requires --lmg-fd\n");
        return -1;
    }
    if ((o.lift_prune || o.lift_prune_pre)
            && ((!o.lmg && !o.lmg_fd) || !o.ground)){
        fprintf(stderr, "Error: --lift-prune* requires --lmg* and -g\n");
        return -1;
    }
    if (o.ground_lmg && ((!o.lmg && !o.lmg_fd) || !o.ground)){
        fprintf(stderr, "Error: --ground-lmg requires --lmg* and -g\n");
        return -1;
    }
    if (o.fam_maximal && !o.ground){
        fprintf(stderr, "Error: --fam requires -g\n");
        return -1;
    }
    if (o.fam_lmg && (!o.ground || (!o.lmg && !o.lmg_fd)
                        || !o.ground_lmg || !o.fam_maximal)){
        fprintf(stderr, "Error: --fam-lmg requires --fam and -g and --lmg*"
                        " --ground-lmg\n");
        return -1;
    }
    if (o.h2_mutex && !o.ground){
        fprintf(stderr, "Error: --h2-mutex requires -g\n");
        return -1;
    }
    if (o.h2_mgroup && (!o.ground || !o.h2_mutex)){
        fprintf(stderr, "Error: --h2-mgroup requires -g and --h2-mutex\n");
        return -1;
    }

    if (o.lift_prune){
        o.lift_prune_pre = 1;
        o.lift_prune_dead_end = 1;
    }

    if (argc == 2){
        if (pddlFiles1(&files, argv[1], &err) != 0){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }
    }else{ // argc == 3
        if (pddlFiles(&files, argv[1], argv[2], &err) != 0){
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }
    }

    if (pddlInit(&pddl, files.domain_pddl, files.problem_pddl,
                 &cfg, &err) != 0){
        fprintf(stderr, "Error: ");
        borErrPrint(&err, 1, stderr);
        return -1;
    }

    pddlNormalize(&pddl);
    if (o.compile_away_cond_eff_pddl)
        pddlCompileAwayCondEff(&pddl);

    BOR_INFO(&err, "Number of PDDL Types: %d", pddl.type.type_size);
    BOR_INFO(&err, "Number of PDDL Objects: %d", pddl.obj.obj_size);
    BOR_INFO(&err, "Number of PDDL Predicates: %d", pddl.pred.pred_size);
    BOR_INFO(&err, "Number of PDDL Functions: %d", pddl.func.pred_size);
    BOR_INFO(&err, "Number of PDDL Actions: %d", pddl.action.action_size);
    BOR_INFO(&err, "Number of PDDL Metric: %d", pddl.metric);
    fflush(stdout);
    fflush(stderr);

    if (o.lmg || o.lmg_fd){
        pddl_lifted_mgroups_infer_limits_t limits
                = PDDL_LIFTED_MGROUPS_INFER_LIMITS_INIT;
        limits.max_candidates = o.max_candidates;
        limits.max_mgroups = o.max_mgroups;

        pddlLiftedMGroupsInit(&lifted_mgroups);
        pddlLiftedMGroupsInit(&monotonicity_invariants);
        if (o.lmg_fd){
            pddl_lifted_mgroups_t *mono = NULL;
            if (o.lmg_fd_monotonicity)
                mono = &monotonicity_invariants;
            pddlLiftedMGroupsInferMonotonicity(&pddl, &limits, mono,
                    &lifted_mgroups, &err);
        }else{
            pddlLiftedMGroupsInferFAMGroups(&pddl, &limits,
                                            &lifted_mgroups, &err);
        }

        if (o.print){
            for (int li = 0; li < lifted_mgroups.mgroup_size; ++li){
                fprintf(stdout, "LMG:%d: ", li);
                pddlLiftedMGroupPrint(&pddl, lifted_mgroups.mgroup + li,
                                      stdout);
            }

            for (int li = 0; li < monotonicity_invariants.mgroup_size; ++li){
                const pddl_lifted_mgroup_t *m
                            = monotonicity_invariants.mgroup + li;
                fprintf(stdout, "Mono:%d: ", li);
                pddlLiftedMGroupPrint(&pddl, m, stdout);
            }
        }
    }

    if (o.ground){
        pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;
        if (o.lift_prune_pre || o.lift_prune_dead_end){
            ground_cfg.lifted_mgroups = &lifted_mgroups;
            ground_cfg.prune_op_pre_mutex = o.lift_prune_pre;
            ground_cfg.prune_op_dead_end = o.lift_prune_dead_end;
        }
        if (pddlStripsGround(&strips, &pddl, &ground_cfg, &err) != 0){
            BOR_INFO2(&err, "Grounding failed.");
            fprintf(stderr, "Error: ");
            borErrPrint(&err, 1, stderr);
            return -1;
        }

        if (o.compile_away_cond_eff)
            pddlStripsCompileAwayCondEff(&strips);

        BOR_INFO(&err, "Number of Strips Operators: %d", strips.op.op_size);
        BOR_INFO(&err, "Number of Strips Facts: %d", strips.fact.fact_size);

        int count = 0;
        for (int i = 0; i < strips.op.op_size; ++i){
            if (strips.op.op[i]->cond_eff_size > 0)
                ++count;
        }
        BOR_INFO(&err, "Number of Strips Operators"
                " with Conditional Effects: %d", count);
        BOR_INFO(&err, "Goal is unreachable: %d",
                strips.goal_is_unreachable);
        BOR_INFO(&err, "Has Conditional Effects: %d", strips.has_cond_eff);
        fflush(stdout);
        fflush(stderr);

        if (o.ground_lmg){
            pddlMGroupsGround(&lmg_mgroups, &pddl, &lifted_mgroups, &strips);
            BOR_INFO(&err, "Ground mutex groups from lifted mutex groups: %d",
                     lmg_mgroups.mgroup_size);

            if (o.print){
                for (int gi = 0; gi < lmg_mgroups.mgroup_size; ++gi){
                    const pddl_mgroup_t *m = lmg_mgroups.mgroup + gi;
                    fprintf(stdout, "LMG-Ground:%d:%d ",
                            gi, m->lifted_mgroup_id);
                    pddlMGroupPrint(&pddl, &strips, m, stdout);
                }
            }

            pddlMGroupsRemoveSubsets(&lmg_mgroups);
            BOR_INFO(&err, "Ground maximal mutex groups from lifted mutex"
                            " groups: %d", lmg_mgroups.mgroup_size);

            pddl_mutex_pairs_t mutex;
            pddlMutexPairsInitStrips(&mutex, &strips);
            pddlMutexPairsAddMGroups(&mutex, &lmg_mgroups);
            BOR_INFO(&err, "Lifted mutex groups mutex-pairs: %d",
                     mutex.num_mutex_pairs);
            pddlMutexPairsFree(&mutex);

            if (o.print){
                for (int gi = 0; gi < lmg_mgroups.mgroup_size; ++gi){
                    const pddl_mgroup_t *m = lmg_mgroups.mgroup + gi;
                    fprintf(stdout, "LMG-Ground-Maximal:%d:%d ",
                            gi, m->lifted_mgroup_id);
                    pddlMGroupPrint(&pddl, &strips, m, stdout);
                }
            }
            mgroupCoverNumber(&lmg_mgroups, &strips, &err);
            fdrVars(&strips, &lmg_mgroups, &err);
        }


        if (o.h2_mutex){
            pddl_mutex_pairs_t mutex;
            pddlMutexPairsInitStrips(&mutex, &strips);
            pddlH2(&strips, &mutex, NULL, NULL, 0., &err);
            if (o.h2_mgroup){
                pddlMGroupsInitEmpty(&h2_mgroups);
                pddlMutexPairsInferMutexGroups(&mutex, &h2_mgroups, &err);
                BOR_INFO(&err, "Found %d h2 mutex groups.",
                         h2_mgroups.mgroup_size);

                if (o.print){
                    for (int gi = 0; gi < h2_mgroups.mgroup_size; ++gi){
                        const pddl_mgroup_t *m = h2_mgroups.mgroup + gi;
                        fprintf(stdout, "H2-MGroup:%d ", gi);
                        pddlMGroupPrint(&pddl, &strips, m, stdout);
                    }
                }
                mgroupCoverNumber(&h2_mgroups, &strips, &err);
                fdrVars(&strips, &h2_mgroups, &err);
            }
            pddlMutexPairsFree(&mutex);
        }

        if (o.fam_maximal){
            pddl_famgroup_config_t cfg = PDDL_FAMGROUP_CONFIG_INIT;
            cfg.maximal = 1;
            if (o.fam_lmg){
                pddlMGroupsInitCopy(&fam_mgroups, &lmg_mgroups);
            }else{
                pddlMGroupsInitEmpty(&fam_mgroups);
            }
            if (pddlFAMGroupsInfer(&fam_mgroups, &strips, &cfg, &err) != 0){
                fprintf(stderr, "Error: ");
                borErrPrint(&err, 1, stderr);
                return -1;
            }
            if (o.fam_lmg)
                pddlMGroupsRemoveSubsets(&fam_mgroups);

            pddl_mutex_pairs_t mutex;
            pddlMutexPairsInitStrips(&mutex, &strips);
            pddlMutexPairsAddMGroups(&mutex, &fam_mgroups);
            BOR_INFO(&err, "fam-groups: %d, mutex-pairs: %d",
                     fam_mgroups.mgroup_size, mutex.num_mutex_pairs);
            pddlMutexPairsFree(&mutex);

            if (o.print){
                for (int gi = 0; gi < fam_mgroups.mgroup_size; ++gi){
                    const pddl_mgroup_t *m = fam_mgroups.mgroup + gi;
                    fprintf(stdout, "FAM-Maximal:%d ", gi);
                    pddlMGroupPrint(&pddl, &strips, m, stdout);
                }
            }
            mgroupCoverNumber(&fam_mgroups, &strips, &err);
            fdrVars(&strips, &fam_mgroups, &err);
        }

        pddlStripsFree(&strips);
    }

    if (o.cmp_mgroup_dominance){
        if (o.ground_lmg && o.h2_mgroup){
            mgroupDominance(&lmg_mgroups, &h2_mgroups, "lmg", "h2", &err);
            mgroupDominance(&h2_mgroups, &lmg_mgroups, "h2", "lmg", &err);
        }
        if (o.h2_mgroup && o.fam_maximal){
            mgroupDominance(&fam_mgroups, &h2_mgroups, "fam", "h2", &err);
            mgroupDominance(&h2_mgroups, &fam_mgroups, "h2", "fam", &err);
        }
        if (o.ground_lmg && o.fam_maximal){
            mgroupDominance(&fam_mgroups, &lmg_mgroups, "fam", "lmg", &err);
            mgroupDominance(&lmg_mgroups, &fam_mgroups, "lmg", "fam", &err);
        }
    }

    if (o.ground_lmg)
        pddlMGroupsFree(&lmg_mgroups);
    if (o.h2_mgroup)
        pddlMGroupsFree(&h2_mgroups);
    if (o.fam_maximal)
        pddlMGroupsFree(&fam_mgroups);
    if (o.lmg || o.lmg_fd){
        pddlLiftedMGroupsFree(&lifted_mgroups);
        pddlLiftedMGroupsFree(&monotonicity_invariants);
    }
    pddlFree(&pddl);

    BOR_INFO2(&err, "DONE");
    return 0;
}

