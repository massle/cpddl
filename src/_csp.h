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

#ifndef __PDDL__CSP_H__
#define __PDDL__CSP_H__

#include "pddl/csp.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_csp_cls {
    int solver_id;
    const char *solver_name;
    pddl_csp_t *(*new_fn)(const pddl_csp_config_t *, pddl_err_t *);
    void (*del_fn)(pddl_csp_t *);
    int (*add_var_int_fn)(pddl_csp_t *, int min_val, int max_val,
                          const char *name);
    int (*add_domain_int_fn)(pddl_csp_t *csp,
                             int tuple_size,
                             int num_var_tuples,
                             int num_val_tuples,
                             const int *var,
                             const int *val);
    int (*add_eq_int_fn)(pddl_csp_t *csp, int var_id, int value);
    int (*add_obj_min_count_diff_fn)(pddl_csp_t *csp,
                                     int var_size,
                                     const int *var);
    int (*get_val_int_fn)(pddl_csp_t *csp, int var_id);
    int (*solve_fn)(pddl_csp_t *csp, pddl_err_t *err);
    void (*dump_fn)(pddl_csp_t *csp, const char *fn);
};
typedef struct pddl_csp_cls pddl_csp_cls_t;

struct pddl_csp {
    pddl_csp_cls_t *cls;
    pddl_csp_config_t cfg;
};

extern pddl_csp_cls_t *pddl_csp_default;
extern pddl_csp_cls_t pddl_csp_cp_optimizer;
extern pddl_csp_cls_t pddl_csp_gecode;
extern pddl_csp_cls_t pddl_csp_not_available;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_CSP_H__ */

