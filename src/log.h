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

#ifndef __PDDL_LOG_H__
#define __PDDL_LOG_H__

#define PDDL_LOG_CONFIG_INT(C, PREFIX, NAME, ERR) \
    BOR_INFO((ERR), "%s" #NAME " = %d", (PREFIX), (C)->NAME)
#define PDDL_LOG_CONFIG_DBL(C, PREFIX, NAME, ERR) \
    BOR_INFO((ERR), "%s" #NAME " = %.4f", (PREFIX), (C)->NAME)
#define PDDL_LOG_CONFIG_BOOL(C, PREFIX, NAME, ERR) \
    BOR_INFO((ERR), "%s" #NAME " = %s", (PREFIX), ((C)->NAME ? "true" : "false"))

#endif /* __PDDL_LOG_H__ */
