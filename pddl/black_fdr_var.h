/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_BLACK_FDR_VAR_H__
#define __PDDL_BLACK_FDR_VAR_H__

#include <pddl/strips.h>
#include <pddl/mgroup.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_black_mgroups_config {
    int lp_add_2cycles; /*!< Add all 2-cycles into LP (default: true) */
    int lp_add_3cycles; /*!< Add all 3-cycles into LP (default: false) */
};
typedef struct pddl_black_mgroups_config pddl_black_mgroups_config_t;

#define PDDL_BLACK_MGROUPS_CONFIG_INIT \
    { \
        1, /* .lp_add_2cycles */ \
        0, /* .lp_add_2cycles */ \
    }

struct pddl_black_mgroup {
    bor_iset_t mgroup;
    pddl_mgroups_t fam_groups;
};
typedef struct pddl_black_mgroup pddl_black_mgroup_t;

struct pddl_black_mgroups {
    pddl_black_mgroup_t *mgroup;
    int mgroup_size;
    int mgroup_alloc;
};
typedef struct pddl_black_mgroups pddl_black_mgroups_t;

void pddlBlackMGroups(pddl_black_mgroups_t *bmgroups,
                      const pddl_strips_t *strips,
                      const pddl_mgroups_t *mgroups,
                      const pddl_black_mgroups_config_t *cfg,
                      bor_err_t *err);

void pddlBlackMGroupsFree(pddl_black_mgroups_t *bmgroups);
void pddlBlackMGroupsPrint(const pddl_strips_t *strips,
                           const pddl_black_mgroups_t *bmgroups,
                           FILE *fout);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_BLACK_FDR_VAR_H__ */
