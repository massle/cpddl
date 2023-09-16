/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "pddl/pot_conj.h"
#include "pddl/critical_path.h"
#include "internal.h"

enum conj_iterator_type {
    IT_PAIRS,
    IT_TRIPLES,
};

struct conj_iterator {
    enum conj_iterator_type type;
    const pddl_strips_t *strips;
    const pddl_mutex_pairs_t *mutex;
    pddl_iset_t conj;
    int cur[4];
    int cur_size;
};
typedef struct conj_iterator conj_iterator_t;

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
            it->cur[0] = 0;
            it->cur[1] = 1;
            it->cur_size = 2;
            break;
        case IT_TRIPLES:
            it->cur[0] = 0;
            it->cur[1] = 1;
            it->cur[2] = 2;
            it->cur_size = 3;
            break;
    }
}

static void conjIteratorInitCopy(conj_iterator_t *it,
                                 const conj_iterator_t *src)
{
    *it = *src;
    pddlISetInit(&it->conj);
}

static void conjIteratorIncCur(conj_iterator_t *it)
{
    for (int inc_idx = it->cur_size - 1; inc_idx >= 0; --inc_idx){
        it->cur[inc_idx]++;
        if (it->cur[inc_idx] < it->strips->fact.fact_size - (it->cur_size - inc_idx - 1)){
            for (int i = inc_idx + 1; i < it->cur_size; ++i){
                it->cur[i] = it->cur[i - 1] + 1;
                if (it->cur[i] == it->strips->fact.fact_size){
                    it->cur[0] = it->cur[1] = it->cur[2] = it->cur[3] = -1;
                    return;
                }
            }
            //printf("%d %d %d\n", it->cur[0], it->cur[1], it->cur[2]);
            return;
        }
    }

    it->cur[0] = it->cur[1] = it->cur[2] = it->cur[3] = -1;
    return;
}

static const pddl_iset_t *conjIteratorNext(conj_iterator_t *it)
{
    switch (it->type){
        case IT_PAIRS:
            if (it->cur[0] < 0)
                return NULL;
            while (pddlMutexPairsIsMutex(it->mutex, it->cur[0], it->cur[1])){
                conjIteratorIncCur(it);
                if (it->cur[0] < 0)
                    return NULL;
            }
            PDDL_ISET_SET(&it->conj, it->cur[0], it->cur[1]);
            conjIteratorIncCur(it);
            break;

        case IT_TRIPLES:
            if (it->cur[0] < 0)
                return NULL;
            while (pddlMutexPairsIsMutex(it->mutex, it->cur[0], it->cur[1])
                    || pddlMutexPairsIsMutex(it->mutex, it->cur[1], it->cur[2])
                    || pddlMutexPairsIsMutex(it->mutex, it->cur[0], it->cur[2])){
                conjIteratorIncCur(it);
                if (it->cur[0] < 0)
                    return NULL;
            }
            PDDL_ISET_SET(&it->conj, it->cur[0], it->cur[1], it->cur[2]);
            conjIteratorIncCur(it);
            break;
    }

    return &it->conj;
}

static void conjIteratorFree(conj_iterator_t *it)
{
    pddlISetFree(&it->conj);
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
    //pddlH2(&mg_strips.strips, &fdr_mutex, NULL, NULL, 0., err);
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
        return -1;

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

        pddl_strips_conj_t pc;
        pddlStripsConjInit(&pc, strips, &cfg, err);

        pddl_mutex_pairs_t pc_mutex;
        pddlStripsConjMutexPairsInitCopy(&pc_mutex, mutex, &pc);

        int hvalue = pot(&pc.strips, &pc_mutex, mgroup, err);
        max_hvalue = PDDL_MAX(max_hvalue, hvalue);

        pddlMutexPairsFree(&pc_mutex);
        pddlStripsConjFree(&pc);
        pddlStripsConjConfigFree(&cfg);
        CTXEND(err);

        if (hvalue < 0)
            return -1;

        conj = conjIteratorNext(&it);
    }
    conjIteratorFree(&it);

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

            int hvalue = pot(&pc.strips, &pc_mutex, mgroup, err);
            max_hvalue = PDDL_MAX(max_hvalue, hvalue);

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

    return max_hvalue;
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
