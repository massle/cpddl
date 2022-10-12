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

#define CONTAINER_OF_CONST(name, ptr, type, member) \
    const type *name = pddl_container_of((ptr), type, member)
#define CONTAINER_OF(name, ptr, type, member) \
    type *name = pddl_container_of((ptr), type, member)

#define ERR PDDL_ERR
#define ERR2 PDDL_ERR2
#define ERR_RET PDDL_ERR_RET
#define ERR_RET2 PDDL_ERR_RET2
#define FATAL PDDL_FATAL
#define FATAL2 PDDL_FATAL2
#define WARN PDDL_WARN
#define WARN2 PDDL_WARN2
#define CTX PDDL_CTX
#define CTX_F PDDL_CTX_F
#define CTX_NO_TIME PDDL_CTX_NO_TIME
#define CTX_NO_TIME_F PDDL_CTX_NO_TIME_F
#define CTXEND PDDL_CTXEND
#define LOG PDDL_LOG
#define LOG2 PDDL_LOG2
#define LOG_IN_CTX PDDL_LOG_IN_CTX
#define TRACE PDDL_TRACE
#define TRACE_RET PDDL_TRACE_RET
#define TRACE_PREPEND PDDL_TRACE_PREPEND
#define TRACE_PREPEND_RET PDDL_TRACE_PREPEND_RET

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
    pddlFmFmt((C), (PDDL), (PARAMS), ((char [2048]){""}), 2048)
#define F_COND_PDDL(C, PDDL, PARAMS) \
    pddlFmPDDLFmt((C), (PDDL), (PARAMS), ((char [2048]){""}), 2048)
#define F_COND_PDDL_BUFSIZE(C, PDDL, PARAMS, BUFSIZE) \
    pddlFmPDDLFmt((C), (PDDL), (PARAMS), ((char [(BUFSIZE)]){""}), (BUFSIZE))
#define F_LIFTED_MGROUP(PDDL, MG) \
    pddlLiftedMGroupFmt((PDDL), (MG), ((char [2048]){""}), 2048)


#define ZEROIZE PDDL_ZEROIZE
#define ZEROIZE_ARR PDDL_ZEROIZE_ARR
#define ZEROIZE_RAW PDDL_ZEROIZE_RAW

#define FREE PDDL_FREE
#define ALLOC PDDL_ALLOC
#define ALLOC_ARR PDDL_ALLOC_ARR
#define REALLOC_ARR PDDL_REALLOC_ARR
#define CALLOC_ARR PDDL_CALLOC_ARR
#define ZALLOC PDDL_ZALLOC
#define MALLOC PDDL_MALLOC
#define ZMALLOC PDDL_ZMALLOC
#define REALLOC PDDL_REALLOC
#define STRDUP PDDL_STRDUP

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_INTERNAL_H__ */
