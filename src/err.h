/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __PDDL_ERR_INTERNAL_H__
#define __PDDL_ERR_INTERNAL_H__

#include "pddl/err.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define ERR(E, format, ...) PDDL_ERR((E), (format), __VA_ARGS__)
#define ERR2(E, msg) PDDL_ERR2((E), (msg))
#define ERR_RET(E, V, format, ...) \
    PDDL_ERR_RET((E), (V), (format), __VA_ARGS__)
#define ERR_RET2(E, V, msg) PDDL_ERR_RET2((E), (V), (msg))
#define FATAL(format, ...) PDDL_FATAL(format, __VA_ARGS__)
#define FATAL2(msg) PDDL_FATAL2(msg)
#define WARN(E, format, ...) PDDL_WARN((E), (format), __VA_ARGS__)
#define WARN2(E, msg) PDDL_WARN2((E), (msg))
#define CTX(E, KW, I) PDDL_CTX((E), (KW), (I))
#define CTX_NO_TIME(E, KW, I) PDDL_CTX_NO_TIME((E), (KW), (I))
#define CTXEND(E) PDDL_CTXEND(E)
#define LOG(E, format, ...) PDDL_LOG((E), (format), __VA_ARGS__)
#define LOG2(E, format) PDDL_LOG2((E), (format))
#define TRACE(E) PDDL_TRACE(E)
#define TRACE_RET(E, V) PDDL_TRACE_RET((E), (V))
#define TRACE_PREPEND(E, format, ...) \
    PDDL_TRACE_PREPEND((E), (format), __VA_ARGS__)
#define TRACE_PREPEND_RET(E, V, format, ...) \
    PDDL_TRACE_PREPEND_RET((E), (V), (format), __VA_ARGS__)

#define LOG_CONFIG_INT(C, NAME, ERR) \
    LOG((ERR), #NAME " = %{" #NAME "}d", (C)->NAME)
#define LOG_CONFIG_ULONG(C, NAME, ERR) \
    LOG((ERR), #NAME " = %{" #NAME "}lu", (C)->NAME)
#define LOG_CONFIG_DBL(C, NAME, ERR) \
    LOG((ERR), #NAME " = %{" #NAME "}.4f", (C)->NAME)
#define LOG_CONFIG_BOOL(C, NAME, ERR) \
    LOG((ERR), #NAME " = %{" #NAME "}b", (C)->NAME)

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_ERR_INTERNAL_H__ */
