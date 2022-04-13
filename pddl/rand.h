/***
 * Copyright (c)2010 Daniel Fiser <danfis@danfis.cz>
 *
 *  This file is part of cpddl.
 *
 *  Distributed under the OSI-approved BSD License (the "License");
 *  see accompanying file BDS-LICENSE for details or see
 *  <http://www.opensource.org/licenses/bsd-license.php>.
 *
 *  This software is distributed WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the License for more information.
 */

#ifndef __PDDL_RAND_H__
#define __PDDL_RAND_H__

#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <pddl/core.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Rand - Random Number Generator
 * ===============================
 *
 * This is only simplified API upon std C erand48(3) function.
 */
struct _pddl_rand_t {
    struct drand48_data data;
};
typedef struct _pddl_rand_t pddl_rand_t;

/**
 * Functions
 * ----------
 */

/**
 * Initializes rand struct.
 */
_pddl_inline void pddlRandInit(pddl_rand_t *r);

/**
 * Initialize the random generator using the given seed.
 */
_pddl_inline void pddlRandInitSeed(pddl_rand_t *r, int seed);

/**
 * Initialize r to current state of s.
 */
_pddl_inline void pddlRandInitCopy(pddl_rand_t *r, const pddl_rand_t *s);

/**
 * Returns randomly generated number in range <0, 1.).
 */
_pddl_inline double pddlRand01(pddl_rand_t *r);

/**
 * Returns randomly generated number in range <from, to).
 */
_pddl_inline double pddlRand(pddl_rand_t *r, pddl_real_t from, pddl_real_t to);


/**** INLINES ****/
_pddl_inline void pddlRandInit(pddl_rand_t *r)
{
    unsigned short int seed[3];
    seed[0] = time(NULL);
    seed[1] = getpid();
    seed[2] = getpgrp();
    seed48_r(seed, &r->data);
}

_pddl_inline void pddlRandInitSeed(pddl_rand_t *r, int seed)
{
    srand48_r(seed, &r->data);
}

_pddl_inline void pddlRandInitCopy(pddl_rand_t *r, const pddl_rand_t *s)
{
    r->data = s->data;
}

_pddl_inline double pddlRand01(pddl_rand_t *r)
{
    double num;
    drand48_r(&r->data, &num);
    return num;
}

_pddl_inline double pddlRand(pddl_rand_t *r, pddl_real_t from, pddl_real_t to)
{
    double num;
    drand48_r(&r->data, &num);
    num *= to - from;
    num += from;
    return num;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_RAND_H__ */
