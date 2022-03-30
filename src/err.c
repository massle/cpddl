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

#include <sys/resource.h>
#include <strings.h>
#include <string.h>
#include "pddl/err.h"

static void pddlErrPrintMsg(const pddl_err_t *err, FILE *fout)
{
    if (!err->err)
        return;

    if (err->msg[0] == 0x0){
        fprintf(fout, "Error: ");
    }else{
        fprintf(fout, "%s", err->msg_prefix);
    }
    fprintf(fout, "%s\n", err->msg);
    fflush(fout);
}

static void pddlErrPrintTraceback(const pddl_err_t *err, FILE *fout)
{
    if (!err->err)
        return;

    for (int i = 0; i < err->trace_depth; ++i){
        for (int j = 0; j < i; ++j)
            fprintf(fout, "  ");
        fprintf(fout, "  ");
        fprintf(fout, "%s:%d (%s)\n",
                err->trace[i].filename,
                err->trace[i].line,
                err->trace[i].func);
    }
    fflush(fout);
}

void pddlErrInit(pddl_err_t *err)
{
    bzero(err, sizeof(*err));
}

void pddlErrSetPrefix(pddl_err_t *err, const char *prefix)
{
    strncpy(err->msg_prefix, prefix, PDDL_ERR_MSG_PREFIX_MAXLEN - 1);
    // err->msg_prefix[PDDL_ERR_MSG_PREFIX_MAXLEN - 1] is always set to 0
    // from the initialization
}

int pddlErrIsSet(const pddl_err_t *err)
{
    return err->err;
}

void pddlErrPrint(const pddl_err_t *err, int with_traceback, FILE *fout)
{
    pddlErrPrintMsg(err, fout);
    if (with_traceback)
        pddlErrPrintTraceback(err, fout);
}

void pddlErrWarnEnable(pddl_err_t *err, FILE *fout)
{
    err->warn_out = fout;
}

void pddlErrInfoEnable(pddl_err_t *err, FILE *fout)
{
    err->info_out = fout;
}

void pddlErrInfoDisablePrintResources(pddl_err_t *err, int disable)
{
    err->info_print_resources_disabled = disable;
}


void _pddlErr(pddl_err_t *err, const char *filename, int line, const char *func,
              const char *format, ...)
{
    if (err == NULL)
        return;

    va_list ap;

    err->trace[0].filename = filename;
    err->trace[0].line = line;
    err->trace[0].func = func;
    err->trace_depth = 1;
    err->trace_more = 0;

    va_start(ap, format);
    vsnprintf(err->msg, PDDL_ERR_MSG_MAXLEN, format, ap);
    va_end(ap);
    err->err = 1;
}

void _pddlErrPrepend(pddl_err_t *err, const char *format, ...)
{
    if (err == NULL)
        return;

    va_list ap;
    char msg[PDDL_ERR_MSG_MAXLEN];
    int size;

    strcpy(msg, err->msg);
    va_start(ap, format);
    size = vsnprintf(err->msg, PDDL_ERR_MSG_MAXLEN, format, ap);
    snprintf(err->msg + size, PDDL_ERR_MSG_MAXLEN - size, "%s", msg);
    va_end(ap);

}

void _pddlTrace(pddl_err_t *err, const char *filename, int line, const char *func)
{
    if (err == NULL)
        return;

    if (err->trace_depth == PDDL_ERR_TRACE_DEPTH){
        err->trace_more = 1;
    }else{
        err->trace[err->trace_depth].filename = filename;
        err->trace[err->trace_depth].line = line;
        err->trace[err->trace_depth].func = func;
        ++err->trace_depth;
    }
}

void _pddlWarn(pddl_err_t *err, const char *filename, int line, const char *func,
               const char *format, ...)
{
    if (err == NULL)
        return;

    va_list ap;

    if (err->warn_out == NULL)
        return;

    va_start(ap, format);
    fprintf(err->warn_out, "Warning: %s:%d [%s]: ", filename, line, func);
    vfprintf(err->warn_out, format, ap);
    va_end(ap);
    fprintf(err->warn_out, "\n");
    fflush(err->warn_out);
}

void _pddlInfo(pddl_err_t *err, const char *filename, int line, const char *func,
               const char *format, ...)
{
    if (err == NULL)
        return;

    struct rusage usg;
    long peak_mem = 0L;
    va_list ap;

    if (err->info_out == NULL)
        return;
    if (!err->info_timer_init){
        pddlTimerStart(&err->info_timer);
        err->info_timer_init = 1;
    }

    if (getrusage(RUSAGE_SELF, &usg) == 0)
        peak_mem = usg.ru_maxrss / 1024L;
    va_start(ap, format);
    pddlTimerStop(&err->info_timer);
    if (!err->info_print_resources_disabled)
        fprintf(err->info_out, "[%.3fs %ldMB] ",
                pddlTimerElapsedInSF(&err->info_timer), peak_mem);
    for (int pi = 0; pi < err->info_prefix_size; ++pi)
        fprintf(err->info_out, "%s", err->info_prefix[pi]);
    vfprintf(err->info_out, format, ap);
    va_end(ap);
    fprintf(err->info_out, "\n");
    fflush(err->info_out);
}
