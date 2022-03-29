/***
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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

#ifndef __PDDL_ERR_H__
#define __PDDL_ERR_H__

#include <pddl/timer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Maximal length of an error message */
#define PDDL_ERR_MSG_MAXLEN 256
/** Maximal length of an error prefix */
#define PDDL_ERR_MSG_PREFIX_MAXLEN 32
/** Maximal depth of a trace */
#define PDDL_ERR_TRACE_DEPTH 32
/** Maximal length of a prefix */
#define PDDL_ERR_PREFIX_MAXLEN 32
/** Number of prefixes */
#define PDDL_ERR_PREFIX_NUM 8

struct pddl_err_trace {
    const char *filename;
    int line;
    const char *func;
};
typedef struct pddl_err_trace pddl_err_trace_t;

struct pddl_err {
    pddl_err_trace_t trace[PDDL_ERR_TRACE_DEPTH];
    int trace_depth;
    int trace_more;
    char msg_prefix[PDDL_ERR_MSG_PREFIX_MAXLEN];
    char msg[PDDL_ERR_MSG_MAXLEN];
    int err;
    char info_prefix[PDDL_ERR_PREFIX_NUM][PDDL_ERR_PREFIX_MAXLEN];
    int info_prefix_size;

    FILE *warn_out;
    FILE *info_out;
    int info_print_resources_disabled;
    pddl_timer_t info_timer;
    int info_timer_init;
};
typedef struct pddl_err pddl_err_t;

#define PDDL_ERR_INIT { 0 }

/**
 * Initialize error structure.
 */
void pddlErrInit(pddl_err_t *err);

/**
 * Set error prefix that is printed on the error line.
 */
void pddlErrSetPrefix(pddl_err_t *err, const char *prefix);

/**
 * Returns true if an error message is set.
 */
int pddlErrIsSet(const pddl_err_t *err);

/**
 * Print the stored error message.
 */
void pddlErrPrint(const pddl_err_t *err, int with_traceback, FILE *fout);

/**
 * Enable/disable warnings.
 * Sets the output stream, if fout is NULL the warnings are disabled.
 */
void pddlErrWarnEnable(pddl_err_t *err, FILE *fout);

/**
 * Enable/disable info messages.
 */
void pddlErrInfoEnable(pddl_err_t *err, FILE *fout);

/**
 * Disable printing resources with PDDL_INFO
 */
void pddlErrInfoDisablePrintResources(pddl_err_t *err, int disable);



/**
 * Sets error message and starts tracing the calls.
 */
#define PDDL_ERR(E, format, ...) \
    _pddlErr((E), __FILE__, __LINE__, __func__, format, __VA_ARGS__)
#define PDDL_ERR2(E, msg) \
    _pddlErr((E), __FILE__, __LINE__, __func__, msg)

/**
 * Same as PDDL_ERR() but also returns the value V immediatelly.
 */
#define PDDL_ERR_RET(E, V, format, ...) do { \
        PDDL_ERR((E), format, __VA_ARGS__); \
        return (V); \
    } while (0)
#define PDDL_ERR_RET2(E, V, msg) do { \
        PDDL_ERR2((E), msg); \
        return (V); \
    } while (0)


/**
 * Fatal error that causes exit.
 */
#define PDDL_FATAL(format, ...) do { \
        fprintf(stderr, "FATAL ERROR: %s:%d [%s]: " format "\n", \
                __FILE__, __LINE__, __func__, __VA_ARGS__); \
        exit(-1); \
    } while (0)
#define PDDL_FATAL2(msg) do { \
        fprintf(stderr, "FATAL ERROR: %s:%d [%s]: " msg "\n", \
                __FILE__, __LINE__, __func__); \
        exit(-1); \
    } while (0)

/**
 * Prints warning.
 */
#define PDDL_WARN(E, format, ...) \
    _pddlWarn((E), __FILE__, __LINE__, __func__, format, __VA_ARGS__)
#define PDDL_WARN2(E, msg) \
    _pddlWarn((E), __FILE__, __LINE__, __func__, msg)

/**
 * Prints info line with timestamp.
 */
#define PDDL_INFO(E, format, ...) \
    _pddlInfo((E), __FILE__, __LINE__, __func__, format, __VA_ARGS__)
#define PDDL_INFO2(E, msg) \
    _pddlInfo((E), __FILE__, __LINE__, __func__, msg)

/**
 * Push another prefix to the info stream
 */
#define PDDL_INFO_PREFIX_PUSH(E, prefix) \
    if ((E) != NULL && (E)->info_prefix_size < PDDL_ERR_PREFIX_NUM) \
        strncpy((E)->info_prefix[(E)->info_prefix_size++], \
                prefix, PDDL_ERR_PREFIX_MAXLEN)

/**
 * Same as PDDL_INFO_PREFIX_PUSH() but allows formatting.
 */
#define PDDL_INFO_PREFIX_PUSHF(E, format, ...) \
    if ((E) != NULL && (E)->info_prefix_size < PDDL_ERR_PREFIX_NUM) \
        snprintf((E)->info_prefix[(E)->info_prefix_size++], \
                 PDDL_ERR_PREFIX_MAXLEN, format, __VA_ARGS__)

/**
 * Remove the last added prefix from the info stream.
 */
#define PDDL_INFO_PREFIX_POP(E) \
    if ((E) != NULL && (E)->info_prefix_size > 0) \
        --(E)->info_prefix_size

/**
 * Trace the error -- record the current file, line and function.
 */
#define PDDL_TRACE(E) \
   _pddlTrace((E), __FILE__, __LINE__, __func__)

/**
 * Same as PDDL_TRACE() but also returns the value V.
 */
#define PDDL_TRACE_RET(E, V) do { \
        PDDL_TRACE(E); \
        return (V); \
    } while (0)

/**
 * Prepends the message before the current error message and trace the
 * call.
 */
#define PDDL_TRACE_PREPEND(E, format, ...) do { \
        _pddlErrPrepend((E), format, __VA_ARGS__); \
        PDDL_TRACE(E); \
    } while (0)
#define PDDL_TRACE_PREPEND_RET(E, V, format, ...) do { \
        _pddlErrPrepend((E), format, __VA_ARGS__); \
        PDDL_TRACE_RET((E), V); \
    } while (0)


void _pddlErr(pddl_err_t *err, const char *filename, int line, const char *func,
             const char *format, ...);
void _pddlErrPrepend(pddl_err_t *err, const char *format, ...);
void _pddlTrace(pddl_err_t *err, const char *fn, int line, const char *func);
void _pddlWarn(pddl_err_t *err, const char *filename, int line, const char *func,
              const char *format, ...);
void _pddlInfo(pddl_err_t *err, const char *filename, int line, const char *func,
              const char *format, ...);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PDDL_ERR_H__ */
