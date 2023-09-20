/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "pddl/pot_conj.h"
#include "pddl/critical_path.h"
#include "pddl/time_limit.h"
#include "pddl/rand.h"
#include "internal.h"

void pddlPotConjFindConfigLog(const pddl_pot_conj_find_config_t *cfg,
                              pddl_err_t *err)
{
    LOG_CONFIG_DBL(cfg, time_limit, err);
    LOG_CONFIG_INT(cfg, max_epochs, err);
    LOG_CONFIG_INT(cfg, max_conj_dim, err);
    LOG_CONFIG_DBL(cfg, log_freq, err);
    LOG_CONFIG_BOOL(cfg, random_conjs, err);
    LOG_CONFIG_INT(cfg, random_seed, err);
    LOG_CONFIG_STR(cfg, write_progress_prefix, err);
}

#define SET_MAX_SIZE 10

enum conj_iterator_type {
    IT_PAIRS,
    IT_TRIPLES,
    IT_ALL,
    IT_RND,
};

struct conj_iterator {
    enum conj_iterator_type type;
    const pddl_strips_t *strips;
    const pddl_mutex_pairs_t *mutex;
    pddl_iset_t conj;
    int set[SET_MAX_SIZE];
    int set_size;
    pddl_bool_t set_end;
    pddl_rand_t rnd;
    int rnd_max_size;
    pddl_iset_t *not_mutex_with;
};
typedef struct conj_iterator conj_iterator_t;

static void _conjIteratorSetInit(conj_iterator_t *it, int size)
{
    PANIC_IF(size >= SET_MAX_SIZE, "The maximum set size is %d", SET_MAX_SIZE);
    it->set_size = size;
    for (int i = 0; i < SET_MAX_SIZE; ++i)
        it->set[i] = i;
    it->set_end = pddl_false;
}

static void _conjIteratorSetNext(conj_iterator_t *it)
{
    for (int idx = it->set_size - 1; idx >= 0; --idx){
        int max_val = it->strips->fact.fact_size - (it->set_size - idx - 1);
        it->set[idx]++;
        if (it->set[idx] < max_val){
            for (int i = idx + 1; i < it->set_size; ++i)
                it->set[i] = it->set[i - 1] + 1;
            return;
        }
    }

    it->set_end = pddl_true;
}

static void conjIteratorInit(conj_iterator_t *it,
                             enum conj_iterator_type type,
                             const pddl_strips_t *strips,
                             const pddl_mutex_pairs_t *mutex)
{
    ZEROIZE(it);
    it->type = type;
    it->strips = strips;
    it->mutex = mutex;
    pddlISetInit(&it->conj);

    switch (type){
        case IT_PAIRS:
        case IT_ALL:
            _conjIteratorSetInit(it, 2);
            break;
        case IT_TRIPLES:
            _conjIteratorSetInit(it, 3);
            break;
        case IT_RND:
            pddlRandInit(&it->rnd, 0);
            it->not_mutex_with = CALLOC_ARR(pddl_iset_t, strips->fact.fact_size);
            for (int fi = 0; fi < strips->fact.fact_size; ++fi){
                pddlMutexPairsGetNotMutexWith(mutex, fi, it->not_mutex_with + fi);
            }
            break;
    }
}

static void conjIteratorInitRand(conj_iterator_t *it,
                                 int max_size,
                                 int random_seed,
                                 const pddl_strips_t *strips,
                                 const pddl_mutex_pairs_t *mutex)
{
    conjIteratorInit(it, IT_RND, strips, mutex);
    pddlRandInit(&it->rnd, random_seed);
    it->rnd_max_size = max_size;
}

static void conjIteratorInitCopy(conj_iterator_t *it,
                                 const conj_iterator_t *src)
{
    *it = *src;
    pddlISetInit(&it->conj);

    if (src->not_mutex_with != NULL){
        it->not_mutex_with = CALLOC_ARR(pddl_iset_t, it->strips->fact.fact_size);
        for (int fi = 0; fi < it->strips->fact.fact_size; ++fi){
            pddlISetUnion(it->not_mutex_with + fi, src->not_mutex_with + fi);
        }
    }
}

static void conjIteratorFree(conj_iterator_t *it)
{
    pddlISetFree(&it->conj);
    if (it->not_mutex_with != NULL){
        for (int fi = 0; fi < it->strips->fact.fact_size; ++fi)
            pddlISetFree(it->not_mutex_with + fi);
        FREE(it->not_mutex_with);
    }
}

static const pddl_iset_t *conjIteratorNext(conj_iterator_t *it)
{
    if (it->type == IT_RND){
        PDDL_ISET(available);
        do {
            int size = pddlRand(&it->rnd, 2, it->rnd_max_size + 1);
            pddlISetEmpty(&it->conj);
            pddlISetEmpty(&available);
            for (int i = 0; i < size; ++i){
                if (i != 0 && pddlISetSize(&available) == 0)
                    break;

                int fact;
                if (i == 0){
                    fact = pddlRand(&it->rnd, 0, it->strips->fact.fact_size);
                    pddlISetUnion(&available, it->not_mutex_with + fact);
                }else{
                    int idx = pddlRand(&it->rnd, 0, pddlISetSize(&available));
                    fact = pddlISetGet(&available, idx);
                    pddlISetIntersect(&available, it->not_mutex_with + fact);
                }
                pddlISetAdd(&it->conj, fact);
            }
            ASSERT(pddlISetSize(&it->conj) <= 1
                    || !pddlMutexPairsIsMutexSet(it->mutex, &it->conj));
        } while (pddlISetSize(&it->conj) <= 1);
        pddlISetFree(&available);
        return &it->conj;
    }

    while (!it->set_end){
        pddlISetEmpty(&it->conj);
        for (int i = 0; i < it->set_size; ++i)
            pddlISetAdd(&it->conj, it->set[i]);

        if (!pddlMutexPairsIsMutexSet(it->mutex, &it->conj))
            break;

        _conjIteratorSetNext(it);
    }

    if (it->set_end){
        if (it->type == IT_ALL && it->set_size + 1 < SET_MAX_SIZE){
            _conjIteratorSetInit(it, it->set_size + 1);
            return conjIteratorNext(it);
        }
        return NULL;
    }

    _conjIteratorSetNext(it);
    return &it->conj;
}


static int pot(const pddl_strips_t *strips,
               const pddl_mutex_pairs_t *mutex,
               const pddl_mgroups_t *mgroup,
               pddl_err_t *err)
{
    pddl_fdr_config_t cfg = PDDL_FDR_CONFIG_INIT;
    cfg.var.alg = PDDL_FDR_VARS_ALG_LARGEST_FIRST;

    pddl_fdr_t fdr;
    pddlFDRInitFromStrips(&fdr, strips, mgroup, mutex, &cfg, err);

    pddl_mutex_pairs_t fdr_mutex;
    pddlFDRMutexPairsInitCopy(&fdr_mutex, mutex, &fdr);

    pddl_mg_strips_t mg_strips;
    pddlMGStripsInitFDR(&mg_strips, &fdr);

    pddlMutexPairsAddMGroups(&fdr_mutex, &mg_strips.mg);
    pddlH2(&mg_strips.strips, &fdr_mutex, NULL, NULL, 0., err);
    //pddlH2FwBw(&mg_strips.strips, &mg_strips.mg, &fdr_mutex, NULL, NULL, 0., err);

    pddl_hpot_config_t pot_cfg = PDDL_HPOT_CONFIG_INIT;
    pot_cfg.fdr = &fdr;
    pot_cfg.mg_strips = &mg_strips;
    pot_cfg.mutex = &fdr_mutex;

    pddl_hpot_config_opt_state_t cfginit = PDDL_HPOT_CONFIG_OPT_STATE_INIT;
    PDDL_HPOT_CONFIG_ADD(&pot_cfg, &cfginit);

    pddl_pot_solutions_t sol;
    pddlPotSolutionsInit(&sol);
    if (pddlHPot(&sol, &pot_cfg, err) != 0)
        TRACE_RET(err, -1);

    int hvalue = -1;
    if (sol.sol_size > 0){
        hvalue = pddlPotSolutionEvalFDRState(sol.sol + 0, &fdr.var, fdr.init);
        PDDL_LOG(err, "Heuristic value: %d", hvalue);
    }
    pddlPotSolutionsFree(&sol);

    pddlMGStripsFree(&mg_strips);
    pddlMutexPairsFree(&fdr_mutex);
    pddlFDRFree(&fdr);
    return hvalue;
}

static int potConj(const pddl_strips_t *strips,
                   const pddl_mutex_pairs_t *mutex,
                   const pddl_mgroups_t *mgroup,
                   const pddl_set_iset_t *conjs,
                   const pddl_iset_t *conj,
                   pddl_err_t *err)
{
    if (conj == NULL && (conjs == NULL || pddlSetISetSize(conjs) == 0)){
        pddlErrLogPause(err);
        int ret = pot(strips, mutex, mgroup, err);
        pddlErrLogContinue(err);
        return ret;
    }

    pddl_strips_conj_config_t pc_cfg;
    pddlStripsConjConfigInit(&pc_cfg);
    // Set up the set of conjunctions C
    if (conjs != NULL){
        for (int i = 0; i < pddlSetISetSize(conjs); ++i)
            pddlStripsConjConfigAddConjAndSubsets(&pc_cfg, pddlSetISetGet(conjs, i));
    }
    if (conj != NULL)
        pddlStripsConjConfigAddConjAndSubsets(&pc_cfg, conj);

    /*
    int fact;
    PDDL_ISET_FOR_EACH(conj, fact)
        printf(" (%s)%s", strips->fact.fact[fact]->name,
               (pddlISetIn(fact, &strips->goal) ? ":G" : ""));
    printf(" :: %d\n", pddlSetISetSize(&pc_cfg.conj));
    fflush(stdout);
    */

    pddlErrLogPause(err);

    // Construct P^C
    pddl_strips_conj_t pc;
    pddlStripsConjInit(&pc, strips, &pc_cfg, err);

    // Initialize a set of mutexes
    pddl_mutex_pairs_t pc_mutex;
    pddlStripsConjMutexPairsInitCopy(&pc_mutex, mutex, &pc);

    // Compute h-value for P^C
    int hvalue = pot(&pc.strips, &pc_mutex, mgroup, err);

    pddlErrLogContinue(err);

    // Free allocated memory
    pddlMutexPairsFree(&pc_mutex);
    pddlStripsConjFree(&pc);
    pddlStripsConjConfigFree(&pc_cfg);

    return hvalue;
}

static int writeProgress(int hvalue,
                         const pddl_set_iset_t *conjs,
                         const pddl_strips_t *strips,
                         const char *prefix,
                         int epoch_idx,
                         pddl_err_t *err)
{
    char fn[1024];
    snprintf(fn, 1024, "%s.%04d", prefix, epoch_idx);
    FILE *fout = fopen(fn, "w");
    if (fout == NULL)
        ERR_RET(err, -1, "Could not open %s", fn);

    fprintf(fout, "hvalue = %d\n", hvalue);
    fprintf(fout, "conj = [\n");
    for (int cid = 0; cid < pddlSetISetSize(conjs); ++cid){
        fprintf(fout, "  [");
        const pddl_iset_t *c = pddlSetISetGet(conjs, cid);
        for (int i = 0; i < pddlISetSize(c); ++i){
            if (i > 0)
                fprintf(fout, ", ");
            fprintf(fout, "\"%s\"", strips->fact.fact[pddlISetGet(c, i)]->name);
        }
        fprintf(fout, "],\n");
    }
    fprintf(fout, "]\n");

    fclose(fout);
    return 0;
}

int pddlPotConjFind(pddl_set_iset_t *conjs,
                    const pddl_strips_t *strips,
                    const pddl_mutex_pairs_t *mutex,
                    const pddl_mgroups_t *mgroup,
                    const pddl_pot_conj_find_config_t *cfg,
                    pddl_err_t *err)
{
    CTX(err, "Pot-Conj-Find");
    CTX_NO_TIME(err, "Cfg");
    pddlPotConjFindConfigLog(cfg, err);
    CTXEND(err);

    // Set up time limit
    pddl_time_limit_t time_limit;
    pddlTimeLimitSet(&time_limit, cfg->time_limit);

    // Determine the base heuristic value
    int best_hvalue = potConj(strips, mutex, mgroup, conjs, NULL, err);
    LOG(err, "Base h-value: %d", best_hvalue);

    for (int epoch = 0; epoch < cfg->max_epochs; ++epoch){
        if (pddlTimeLimitCheck(&time_limit) != 0){
            LOG(err, "Reached time limit. (%.2f seconds)", cfg->time_limit);
            break;
        }

        CTX(err, "Epoch %d", epoch);

        // Set to true if an improvement was found
        pddl_bool_t improved = pddl_false;

        // Frequency of logging
        pddl_time_limit_t log_timer;
        pddlTimeLimitSet(&log_timer, cfg->log_freq);

        conj_iterator_t it;
        if (cfg->random_conjs){
            conjIteratorInitRand(&it, cfg->max_conj_dim, cfg->random_seed,
                                 strips, mutex);
        }else{
            conjIteratorInit(&it, IT_ALL, strips, mutex);
        }

        const pddl_iset_t *conj = conjIteratorNext(&it);
        for (int conji = 0; conj != NULL; ++conji){
            ASSERT(!cfg->random_conjs || pddlISetSize(conj) <= cfg->max_conj_dim);
            if (pddlISetSize(conj) > cfg->max_conj_dim)
                break;

            if (pddlTimeLimitCheck(&time_limit) != 0){
                LOG(err, "Reached time limit. (%.2f seconds)", cfg->time_limit);
                break;
            }

            // Compute h-value for P^C
            int hvalue = potConj(strips, mutex, mgroup, conjs, conj, err);

            if (hvalue > best_hvalue){
                // Found improving conjunction
                LOG(err, "Tested %d conjunctions, cur size: %d",
                    conji + 1, pddlISetSize(conj));

                char log[1024];
                int written = 0;
                int fact;
                PDDL_ISET_FOR_EACH(conj, fact){
                    if (written >= 1024 - 1)
                        break;
                    written += snprintf(log + written, 1024 - written, " (%s)%s",
                                        strips->fact.fact[fact]->name,
                                        (pddlISetIn(fact, &strips->goal) ? ":G" : ""));
                }
                log[written] = '\x0';
                LOG(err, "Improving conjunction:%s", log);
                LOG(err, "Best h-value so far: %d", hvalue);
                best_hvalue = hvalue;
                pddlSetISetAdd(conjs, conj);
                improved = pddl_true;

                if (cfg->write_progress_prefix != NULL){
                    if (writeProgress(hvalue, conjs, strips,
                                cfg->write_progress_prefix, epoch, err) != 0){
                        conjIteratorFree(&it);
                        CTXEND(err);
                        CTXEND(err);
                        TRACE_RET(err, -1);
                    }
                }

            }else if (pddlTimeLimitCheck(&log_timer) != 0){
                // Log progress so that we know that something is happenning
                LOG(err, "Tested %d conjunctions, cur size: %d",
                    conji + 1, pddlISetSize(conj));
                pddlTimeLimitSet(&log_timer, cfg->log_freq);
            }

            // Handle error state
            if (hvalue < 0){
                conjIteratorFree(&it);
                CTXEND(err);
                CTXEND(err);
                TRACE_RET(err, -1);
            }

            if (improved)
                break;

            conj = conjIteratorNext(&it);
        }
        conjIteratorFree(&it);
        CTXEND(err);

        // No improvement -- there is no point in continuing
        if (!improved)
            break;
    }

    CTXEND(err);
    return 0;
}


static int pddlPotConjMaxInitHValue1(const pddl_strips_t *strips,
                                     const pddl_mutex_pairs_t *mutex,
                                     const pddl_mgroups_t *mgroup,
                                     enum conj_iterator_type it_type,
                                     pddl_err_t *err)
{
    int max_hvalue = -1;

    conj_iterator_t it;
    conjIteratorInit(&it, it_type, strips, mutex);
    const pddl_iset_t *conj = conjIteratorNext(&it);
    while (conj != NULL){
        if (it_type == IT_PAIRS){
            CTX(err, "CONJ-1-pair");
            int f1 = pddlISetGet(conj, 0);
            int f2 = pddlISetGet(conj, 1);
            LOG(err, "Set: %d:(%s) %d:(%s)",
                f1, strips->fact.fact[f1]->name,
                f2, strips->fact.fact[f2]->name);

        }else if (it_type == IT_TRIPLES){
            CTX(err, "CONJ-1-triple");
            int f1 = pddlISetGet(conj, 0);
            int f2 = pddlISetGet(conj, 1);
            int f3 = pddlISetGet(conj, 2);
            LOG(err, "Set: %d:(%s) %d:(%s) %d:(%s)",
                f1, strips->fact.fact[f1]->name,
                f2, strips->fact.fact[f2]->name,
                f3, strips->fact.fact[f3]->name);
        }

        pddl_strips_conj_config_t cfg;
        pddlStripsConjConfigInit(&cfg);
        pddlStripsConjConfigAddConj(&cfg, conj);

        if (pddlISetSize(conj) > 2){
            PDDL_ISET(pair);
            for (int i = 0; i < pddlISetSize(conj) - 1; ++i){
                for (int j = i + 1; j < pddlISetSize(conj); ++j){
                    PDDL_ISET_SET(&pair, pddlISetGet(conj, i), pddlISetGet(conj, j));
                    pddlStripsConjConfigAddConj(&cfg, &pair);
                }
            }
            pddlISetFree(&pair);
        }

        pddl_strips_conj_t pc;
        pddlStripsConjInit(&pc, strips, &cfg, err);

        pddl_mutex_pairs_t pc_mutex;
        pddlStripsConjMutexPairsInitCopy(&pc_mutex, mutex, &pc);

        pddlErrLogPause(err);
        int hvalue = pot(&pc.strips, &pc_mutex, mgroup, err);
        pddlErrLogContinue(err);
        if (hvalue < 0)
            TRACE_RET(err, -1);
        if (hvalue > max_hvalue)
            max_hvalue = hvalue;
        LOG(err, "Heuristic value: %d / best: %d", hvalue, max_hvalue);

        pddlMutexPairsFree(&pc_mutex);
        pddlStripsConjFree(&pc);
        pddlStripsConjConfigFree(&cfg);
        CTXEND(err);

        if (hvalue < 0)
            return -1;

        conj = conjIteratorNext(&it);
    }
    conjIteratorFree(&it);

    PDDL_LOG(err, "Maximum heuristic value: %d", max_hvalue);
    return max_hvalue;
}

static int pddlPotConjMaxInitHValue2(const pddl_strips_t *strips,
                                     const pddl_mutex_pairs_t *mutex,
                                     const pddl_mgroups_t *mgroup,
                                     enum conj_iterator_type it_type,
                                     pddl_err_t *err)
{
    int max_hvalue = -1;

    conj_iterator_t it;
    conjIteratorInit(&it, it_type, strips, mutex);
    const pddl_iset_t *conj = conjIteratorNext(&it);
    while (conj != NULL){
        conj_iterator_t it2;
        conjIteratorInitCopy(&it2, &it);
        const pddl_iset_t *conj2 = conjIteratorNext(&it2);
        while (conj2 != NULL){
            if (it_type == IT_PAIRS){
                CTX(err, "CONJ-2-pair");
                int f1 = pddlISetGet(conj, 0);
                int f2 = pddlISetGet(conj, 1);
                LOG(err, "Set: %d:(%s) %d:(%s)",
                    f1, strips->fact.fact[f1]->name,
                    f2, strips->fact.fact[f2]->name);
                f1 = pddlISetGet(conj2, 0);
                f2 = pddlISetGet(conj2, 1);
                LOG(err, "Set: %d:(%s) %d:(%s)",
                    f1, strips->fact.fact[f1]->name,
                    f2, strips->fact.fact[f2]->name);

            }else if (it_type == IT_TRIPLES){
                CTX(err, "CONJ-2-triple");
                int f1 = pddlISetGet(conj, 0);
                int f2 = pddlISetGet(conj, 1);
                int f3 = pddlISetGet(conj, 2);
                LOG(err, "Set: %d:(%s) %d:(%s) %d:(%s)",
                    f1, strips->fact.fact[f1]->name,
                    f2, strips->fact.fact[f2]->name,
                    f3, strips->fact.fact[f3]->name);

                f1 = pddlISetGet(conj2, 0);
                f2 = pddlISetGet(conj2, 1);
                f3 = pddlISetGet(conj2, 2);
                LOG(err, "Set: %d:(%s) %d:(%s) %d:(%s)",
                    f1, strips->fact.fact[f1]->name,
                    f2, strips->fact.fact[f2]->name,
                    f3, strips->fact.fact[f3]->name);
            }

            pddl_strips_conj_config_t cfg;
            pddlStripsConjConfigInit(&cfg);
            pddlStripsConjConfigAddConj(&cfg, conj);
            pddlStripsConjConfigAddConj(&cfg, conj2);

            pddl_strips_conj_t pc;
            pddlStripsConjInit(&pc, strips, &cfg, err);

            pddl_mutex_pairs_t pc_mutex;
            pddlStripsConjMutexPairsInitCopy(&pc_mutex, mutex, &pc);

            pddlErrLogPause(err);
            int hvalue = pot(&pc.strips, &pc_mutex, mgroup, err);
            pddlErrLogContinue(err);
            if (hvalue > max_hvalue)
                max_hvalue = hvalue;
            LOG(err, "Heuristic value: %d / best: %d", hvalue, max_hvalue);

            pddlMutexPairsFree(&pc_mutex);
            pddlStripsConjFree(&pc);
            pddlStripsConjConfigFree(&cfg);
            CTXEND(err);

            if (hvalue < 0)
                return -1;

            conj2 = conjIteratorNext(&it2);
        }
        conjIteratorFree(&it2);

        conj = conjIteratorNext(&it);
    }
    conjIteratorFree(&it);

    PDDL_LOG(err, "Maximum heuristic value: %d", max_hvalue);
    return max_hvalue;
}

int pddlPotConjMaxInitHValueBase(const pddl_strips_t *strips,
                                 const pddl_mutex_pairs_t *mutex,
                                 const pddl_mgroups_t *mgroup,
                                 pddl_err_t *err)
{
    CTX(err, "BASE");

    pddlErrLogPause(err);
    int hvalue = pot(strips, mutex, mgroup, err);
    pddlErrLogContinue(err);

    LOG(err, "Heuristic value: %d", hvalue);
    CTXEND(err);
    return hvalue;
}

int pddlPotConjMaxInitHValueOnePair(const pddl_strips_t *strips,
                                    const pddl_mutex_pairs_t *mutex,
                                    const pddl_mgroups_t *mgroup,
                                    pddl_err_t *err)
{
    return pddlPotConjMaxInitHValue1(strips, mutex, mgroup, IT_PAIRS, err);
}

int pddlPotConjMaxInitHValueOneTriple(const pddl_strips_t *strips,
                                      const pddl_mutex_pairs_t *mutex,
                                      const pddl_mgroups_t *mgroup,
                                      pddl_err_t *err)
{
    return pddlPotConjMaxInitHValue1(strips, mutex, mgroup, IT_TRIPLES, err);
}

int pddlPotConjMaxInitHValueTwoPairs(const pddl_strips_t *strips,
                                     const pddl_mutex_pairs_t *mutex,
                                     const pddl_mgroups_t *mgroup,
                                     pddl_err_t *err)
{
    return pddlPotConjMaxInitHValue2(strips, mutex, mgroup, IT_PAIRS, err);
}


int pddlPotConjDim(const pddl_strips_t *strips,
                   const pddl_mutex_pairs_t *mutex,
                   const pddl_mgroups_t *mgroup,
                   int dimension,
                   pddl_err_t *err)
{
    pddl_strips_conj_config_t cfg;
    pddlStripsConjConfigInit(&cfg);

    conj_iterator_t it;
    conjIteratorInit(&it, IT_ALL, strips, mutex);
    const pddl_iset_t *conj = conjIteratorNext(&it);
    while (conj != NULL){
        if (pddlISetSize(conj) > dimension)
            break;
        pddlStripsConjConfigAddConj(&cfg, conj);
        conj = conjIteratorNext(&it);
    }
    conjIteratorFree(&it);

    pddl_strips_conj_t pc;
    pddlStripsConjInit(&pc, strips, &cfg, err);

    pddl_mutex_pairs_t pc_mutex;
    pddlStripsConjMutexPairsInitCopy(&pc_mutex, mutex, &pc);

    int hvalue = pot(&pc.strips, &pc_mutex, mgroup, err);
    LOG(err, "Heuristic value: %d", hvalue);

    pddlMutexPairsFree(&pc_mutex);
    pddlStripsConjFree(&pc);
    pddlStripsConjConfigFree(&cfg);

    if (hvalue < 0)
        TRACE_RET(err, -1);
    return 0;
}
