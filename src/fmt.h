/***
 * cpddl
 * -------
 * Copyright (c)2021 Daniel Fiser <danfis@danfis.cz>,
 * AI Center, Department of Computer Science,
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

#ifndef __PDDL_FMT_H__
#define __PDDL_FMT_H__

#include "pddl/cost.h"
#include "pddl/cond.h"

#define F_COST(C) pddlCostFmt((C), ((char [22]){""}), 22)
#define F_COND(C, PDDL, PARAMS) \
    pddlCondFmt((C), (PDDL), (PARAMS), ((char [2048]){""}), 2048)
#define F_COND_PDDL(C, PDDL, PARAMS) \
    pddlCondPDDLFmt((C), (PDDL), (PARAMS), ((char [2048]){""}), 2048)
#define F_LIFTED_MGROUP(PDDL, MG) \
    pddlLiftedMGroupFmt((PDDL), (MG), ((char [2048]){""}), 2048)

#endif /* __PDDL_FMT_H__ */
