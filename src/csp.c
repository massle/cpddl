/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>
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

#include "_csp.h"
#include "internal.h"

#if defined(PDDL_CPOPTIMIZER)
pddl_csp_cls_t *pddl_csp_default = &pddl_csp_cp_optimizer;
#else
pddl_csp_cls_t *pddl_csp_default = &pddl_csp_not_available;
#endif

pddl_csp_t *pddlCSPNew(const pddl_csp_config_t *cfg, pddl_err_t *err)
{
    return pddl_csp_default->new_fn(cfg, err);
}

void pddlCSPDel(pddl_csp_t *csp)
{
    csp->cls->del_fn(csp);
}

int pddlCSPSolverId(const pddl_csp_t *csp)
{
    return csp->cls->solver_id;
}

const char *pddlCSPSolverName(const pddl_csp_t *csp)
{
    return csp->cls->solver_name;
}

int pddlCSPAddVarInt(pddl_csp_t *csp,
                     int min_val,
                     int max_val,
                     const char *name)
{
    return csp->cls->add_var_int_fn(csp, min_val, max_val, name);
}

int pddlCSPAddDomainInt(pddl_csp_t *csp,
                        int tuple_size,
                        int num_var_tuples,
                        int num_val_tuples,
                        const int *var,
                        const int *val)
{
    return csp->cls->add_domain_int_fn(csp, tuple_size, num_var_tuples,
                                       num_val_tuples, var, val);
}

int pddlCSPAddEqInt(pddl_csp_t *csp, int var_id, int value)
{
    return csp->cls->add_eq_int_fn(csp, var_id, value);
}

int pddlCSPAddObjMinCountDifferent(pddl_csp_t *csp,
                                   int var_size,
                                   const int *var)
{
    return csp->cls->add_obj_min_count_diff_fn(csp, var_size, var);
}

int pddlCSPGetValInt(pddl_csp_t *csp, int var_id)
{
    return csp->cls->get_val_int_fn(csp, var_id);
}


int pddlCSPSolve(pddl_csp_t *csp, pddl_err_t *err)
{
    return csp->cls->solve_fn(csp, err);
}

void pddlCSPDump(pddl_csp_t *csp, const char *fn)
{
    csp->cls->dump_fn(csp, fn);
}

#define noSolverExit() \
    do { \
    fprintf(stderr, "Error: No CP solver available!\n"); \
    exit(-1); \
    } while (0)
static pddl_csp_t *noNew(const pddl_csp_config_t *cfg, pddl_err_t *err)
{ noSolverExit(); return NULL; }
static void noDel(pddl_csp_t *csp)
{ noSolverExit(); }
static int noAddVarInt(pddl_csp_t *csp,
                       int min_val,
                       int max_val,
                       const char *name)
{ noSolverExit(); return 0; }
static int noAddDomainInt(pddl_csp_t *csp,
                          int tuple_size,
                          int num_var_tuples,
                          int num_val_tuples,
                          const int *var,
                          const int *val)
{ noSolverExit(); return 0; }
static int noAddEqInt(pddl_csp_t *csp, int var_id, int value)
{ noSolverExit(); return 0; }
static int noAddObjMinCountDifferent(pddl_csp_t *csp,
                                     int var_size,
                                     const int *var)
{ noSolverExit(); return 0; }
static int noGetValInt(pddl_csp_t *csp, int var_id)
{ noSolverExit(); return 0; }
static int noSolve(pddl_csp_t *csp, pddl_err_t *err)
{ noSolverExit(); return 0; }
static void noDump(pddl_csp_t *csp, const char *fn)
{ noSolverExit(); }

pddl_csp_cls_t pddl_csp_not_available = {
    0,
    "",
    noNew,
    noDel,
    noAddVarInt,
    noAddDomainInt,
    noAddEqInt,
    noAddObjMinCountDifferent,
    noGetValInt,
    noSolve,
    noDump,
};
