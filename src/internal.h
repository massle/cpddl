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

#ifndef __PDDL_INTERNAL_H__
#define __PDDL_INTERNAL_H__

#include "pddl/common.h"
#include "pddl/err.h"
#include "pddl/alloc.h"

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
#define LOG_IN_CTX(E, CTX_KW, CTX_I, format, ...) \
    PDDL_LOG_IN_CTX(E, CTX_KW, CTX_I, format, __VA_ARGS__)
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

/** TODO: Get rid of this */
#define PDDL_LOG_CONFIG_INT(C, PREFIX, NAME, ERR) \
    PDDL_INFO((ERR), "%s" #NAME " = %d", (PREFIX), (C)->NAME)
#define PDDL_LOG_CONFIG_DBL(C, PREFIX, NAME, ERR) \
    PDDL_INFO((ERR), "%s" #NAME " = %.4f", (PREFIX), (C)->NAME)
#define PDDL_LOG_CONFIG_BOOL(C, PREFIX, NAME, ERR) \
    PDDL_INFO((ERR), "%s" #NAME " = %s", (PREFIX), ((C)->NAME ? "true" : "false"))


#ifdef PDDL_DEBUG
#include <assert.h>
# define ASSERT(x) assert(x)
# define DBG(E, format, ...) PDDL_INFO((E), "DEBUG: " format, __VA_ARGS__)
# define DBG2(E, msg) PDDL_INFO2((E), "DEBUG: " msg)

#else /* PDDL_DEBUG */

# define NDEBUG
# define ASSERT(x)
# define DBG(E, format, ...)
# define DBG2(E, msg)
#endif /* PDDL_DEBUG */

#define ASSERT_RUNTIME(x) \
    do { \
    if (!(x)){ \
        fprintf(stderr, "%s:%d Assertion `" #x "' failed!\n", \
                __FILE__, __LINE__); \
        exit(-1); \
    } \
    } while (0)

#define ASSERT_RUNTIME_M(X, M) \
    do { \
    if (!(X)){ \
        fprintf(stderr, "%s:%d Assertion `" #X "' failed: %s\n", \
                __FILE__, __LINE__, (M)); \
        exit(-1); \
    } \
    } while (0)




#define F_COST(C) pddlCostFmt((C), ((char [22]){""}), 22)
#define F_COND(C, PDDL, PARAMS) \
    pddlCondFmt((C), (PDDL), (PARAMS), ((char [2048]){""}), 2048)
#define F_COND_PDDL(C, PDDL, PARAMS) \
    pddlCondPDDLFmt((C), (PDDL), (PARAMS), ((char [2048]){""}), 2048)
#define F_COND_PDDL_BUFSIZE(C, PDDL, PARAMS, BUFSIZE) \
    pddlCondPDDLFmt((C), (PDDL), (PARAMS), ((char [(BUFSIZE)]){""}), (BUFSIZE))
#define F_LIFTED_MGROUP(PDDL, MG) \
    pddlLiftedMGroupFmt((PDDL), (MG), ((char [2048]){""}), 2048)


#define FREE(ptr) PDDL_FREE(ptr)
#define ALLOC(type) PDDL_ALLOC(type)
#define ALLOC_ARR(type, num_elements) PDDL_ALLOC_ARR(type, (num_elements))
#define REALLOC_ARR(ptr, type, num_elements) \
    PDDL_REALLOC_ARR((ptr), type, (num_elements))
#define CALLOC_ARR(type, num_elements) PDDL_CALLOC_ARR(type, (num_elements))
#define MALLOC(size) PDDL_MALLOC(size)
#define REALLOC(ptr, size) PDDL_REALLOC((ptr), (size))
#define STRDUP(str) PDDL_STRDUP(str)

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_INTERNAL_H__ */
