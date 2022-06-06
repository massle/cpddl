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

#ifdef PDDL_GECODE
#include <gecode/driver.hh>
#include <gecode/int.hh>
#include <gecode/minimodel.hh>

using namespace Gecode;

struct pddl_csp_gecode {
    pddl_csp_t csp;
    pddl_csp_gecode(const pddl_csp_config_t *cfg)
    {
        csp.cls = &pddl_csp_cp_optimizer;
        csp.cfg = *cfg;
    }

    ~pddl_csp_gecode()
    {
    }
};
typedef struct pddl_csp_gecode pddl_csp_gecode_t;

#define CSP(C) \
    pddl_csp_gecode_t *csp = pddl_container_of(_csp, pddl_csp_gecode_t, csp)

static pddl_csp_t *cpNew(const pddl_csp_config_t *cfg, pddl_err_t *err)
{
    pddl_csp_gecode_t *csp = new pddl_csp_gecode_t(cfg);
    return &csp->csp;
}

static void cpDel(pddl_csp_t *_csp)
{
    CSP(_csp);
    delete csp;
}

static int cpAddVarInt(pddl_csp_t *_csp,
                       int min_val,
                       int max_val,
                       const char *name)
{
    //CSP(_csp);
    return 0;
}

static int cpAddDomainInt(pddl_csp_t *_csp,
                          int tuple_size,
                          int num_var_tuples,
                          int num_val_tuples,
                          const int *var,
                          const int *val)
{
    //CSP(_csp);
    return 0;
}

static int cpAddEqInt(pddl_csp_t *_csp, int var_id, int value)
{
    //CSP(_csp);
    return 0;
}

static int cpAddObjMinCountDifferent(pddl_csp_t *_csp,
                                     int var_size,
                                     const int *var)
{
    //CSP(_csp);
    return 0;
}

static int cpGetValInt(pddl_csp_t *_csp, int var_id)
{
    //CSP(_csp);
    return 0;
}


static int cpSolve(pddl_csp_t *_csp, pddl_err_t *err)
{
    //CSP(_csp);
    return PDDL_CSP_NO_SOLUTION;
}

static void cpDump(pddl_csp_t *_csp, const char *fn)
{
    //CSP(_csp);
}

pddl_csp_cls_t pddl_csp_cp_optimizer = {
    PDDL_CSP_CP_OPTIMIZER,
    "cp-optimizer",
    cpNew,
    cpDel,
    cpAddVarInt,
    cpAddDomainInt,
    cpAddEqInt,
    cpAddObjMinCountDifferent,
    cpGetValInt,
    cpSolve,
    cpDump,
};

#else /* PDDL_GECODE */
pddl_csp_cls_t pddl_csp_cp_gecode;
#endif /* PDDL_GECODE */

