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

#include "internal.h"
#include "pddl/err.h"

static void printSourceFilePointer(FILE *fout,
                                   const pddl_err_source_file_ptr_t *p)
{
    if (!p->is_set)
        return;
    FILE *fin = fopen(p->fn, "r");
    if (fin == NULL){
        fprintf(fout, "%s:%d:%d: Cannot open the file\n", p->fn, p->line, p->column);
        return;
    }

    int start_line = p->line - p->num_preceding_lines;
    if (start_line < 1)
        start_line = 1;

    fprintf(fout, "%s:%d:%d:\n", p->fn, p->line, p->column);

    char *lbuf = NULL;
    size_t lsize = 0;
    ssize_t readsize = 0;
    for (int line = 1;
            line <= p->line && (readsize = getline(&lbuf, &lsize, fin)) > 0;
            ++line){
        if (line >= start_line)
            fprintf(fout, "% 6d | %s", line, lbuf);
    }
    fprintf(fout, "       | ");
    for (int i = 1; i < p->column; ++i)
        fprintf(fout, " ");
    fprintf(fout, "^--- here\n");
    fclose(fin);
}

static void pddlErrPrintMsg(const pddl_err_t *err, FILE *fout)
{
    if (!err->err)
        return;

    fprintf(fout, "Error: %s\n", err->msg);
    printSourceFilePointer(fout, &err->err_source_file);
    fflush(fout);
}

static void pddlErrPrintTraceback(const pddl_err_t *err, FILE *fout)
{
    if (!err->err)
        return;

    fprintf(fout, "Traceback:\n");
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
    ZEROIZE(err);
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

void pddlErrLogEnable(pddl_err_t *err, FILE *fout)
{
    err->log_out = fout;
}

void pddlErrLogDisablePrintResources(pddl_err_t *err, int disable)
{
    err->log_print_resources_disabled = disable;
}

void pddlErrFlush(pddl_err_t *err)
{
    if (err == NULL)
        return;
    if (err->log_out != NULL)
        fflush(err->log_out);
}

void pddlErrSetSourceFilePointer(pddl_err_t *err,
                                 const char *source_file_name,
                                 int line_number,
                                 int column_number,
                                 int num_additional_preceding_lines)
{
    PANIC_IF(strlen(source_file_name) >= PDDL_ERR_PATH_MAXLEN,
             "The path '%s' is too long.", source_file_name);

    err->err_source_file.is_set = 1;
    strcpy(err->err_source_file.fn, source_file_name);
    err->err_source_file.line = line_number;
    err->err_source_file.column = column_number;
    err->err_source_file.num_preceding_lines = num_additional_preceding_lines;
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

void _pddlPanic(const char *filename, int line, const char *func,
                const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "FATAL ERROR: %s:%d [%s]: ", filename, line, func);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(-1);
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

void _pddlCtx(pddl_err_t *err, int time, const char *fmt, ...)
{
    if (err == NULL || err->ctx_size == PDDL_ERR_CTX_MAXLEN)
        return;

    pddl_err_ctx_t *ctx = err->ctx + err->ctx_size++;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->prefix, PDDL_ERR_CTX_PREFIX_MAXLEN, fmt, ap);
    va_end(ap);
    ctx->prefix[PDDL_ERR_CTX_PREFIX_MAXLEN - 1] = '\0';

    ctx->use_time = time;
    if (time){
        pddlTimerStart(&ctx->timer);
        _pddlLog(err, "BEGIN");
    }
}

void _pddlCtxEnd(pddl_err_t *err)
{
    if (err != NULL && err->ctx_size > 0){
        pddl_err_ctx_t *ctx = err->ctx + err->ctx_size - 1;
        if (ctx->use_time){
            pddlTimerStop(&ctx->timer);
            _pddlLog(err, "END elapsed time: %.3f",
                     pddlTimerElapsedInSF(&ctx->timer));
        }
        --err->ctx_size;
    }
}

static void logResources(pddl_err_t *err)
{
    if (!err->log_timer_init){
        pddlTimerStart(&err->log_timer);
        err->log_timer_init = 1;
    }

    if (err->log_out == NULL)
        return;

    if (!err->log_print_resources_disabled){
        struct rusage usg;
        long peak_mem = 0L;
        if (getrusage(RUSAGE_SELF, &usg) == 0)
            peak_mem = usg.ru_maxrss / 1024L;
        pddlTimerStop(&err->log_timer);
        fprintf(err->log_out, "[%.3fs %ldMB] ",
                pddlTimerElapsedInSF(&err->log_timer), peak_mem);
    }
}

static void logOut(pddl_err_t *err, const char *buf, int len)
{
    if (err->log_out != NULL)
        fwrite(buf, sizeof(char), len, err->log_out);
}

static char digit_c[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                            'a', 'b', 'c', 'd', 'e', 'f' };

static const char *itos(int value, int radix, char *buf, int buflen)
{
    int neg = 0;
    if (value < 0){
        neg = 1;
        value = -value;
    }

    do {
        int digit = value % radix;
        buf[--buflen] = digit_c[digit];
        value /= radix;
    } while (value > 0);

    if (neg)
        buf[--buflen] = '-';

    return buf + buflen;
}

static const char *utos(unsigned int value, unsigned int radix, char *buf, int buflen)
{
    do {
        unsigned int digit = value % radix;
        buf[--buflen] = digit_c[digit];
        value /= radix;
    } while (value > 0);
    return buf + buflen;
}

static const char *ltos(long value, long radix, char *buf, int buflen)
{
    int neg = 0;
    if (value < 0L){
        neg = 1;
        value = -value;
    }

    do {
        long digit = value % radix;
        buf[--buflen] = digit_c[digit];
        value /= radix;
    } while (value > 0);

    if (neg)
        buf[--buflen] = '-';

    return buf + buflen;
}

static const char *ultos(unsigned long value, unsigned long radix, char *buf, int buflen)
{
    do {
        unsigned long digit = value % radix;
        buf[--buflen] = digit_c[digit];
        value /= radix;
    } while (value > 0);
    return buf + buflen;
}

#define IS_FLT_SPEC(C) \
    ((C) == 'f' || (C) == 'F' \
        || (C) == 'g' || (C) == 'G' \
        || (C) == 'a' || (C) == 'A')
#define IS_MOD(C) \
    ((C) == '#' || (C) == '0' || (C) == '-' || (C) == ' ' || (C) == '+' \
        || (C) == '\'' || ((C) >= '0' && (C) <= '9') || (C) == '.')
#define NUM_BUFSIZE 128
#define MOD_BUFSIZE 128
void _pddlLog(pddl_err_t *err, const char *fmt, ...)
{
    if (err == NULL || err->log_out == NULL)
        return;

    char bf[NUM_BUFSIZE];
    char mod[MOD_BUFSIZE];
    int ins;
    char ch;

    logResources(err);
    for (int pi = 0; pi < err->ctx_size; ++pi)
        fprintf(err->log_out, "%s: ", err->ctx[pi].prefix);

    va_list va;
    va_start(va, fmt);
    while (1){
        const char *fmt_begin = fmt;
        int len = 0;
        for (ch = *(fmt++); ch != '%' && ch != '\0'; ch = *(fmt++), ++len)
            ;
        if (len > 0)
            logOut(err, fmt_begin, len);
        if (ch == '\0')
            break;

        ch = *(fmt++);
        if (ch == '\0')
            break;

        if (ch == '%'){
            logOut(err, "%", 1);
            continue;
        }

        int is_long = 0;
        if (ch == 'l'){
            is_long = 1;
            ch = *(fmt++);
            if (ch == '\0')
                break;
        }

        int bval;
        const char *s;
        switch (ch){
            case 'b':
                PANIC_IF(is_long, "%%lb is not supported");
                bval = va_arg(va, pddl_bool_promote_type_t);
                if (bval){
                    logOut(err, "true", 4);
                }else{
                    logOut(err, "false", 5);
                }
                break;

            case 'u':
                if (is_long){
                    unsigned long v = va_arg(va, unsigned long);
                    s = ultos(v, 10, bf, NUM_BUFSIZE);
                }else{
                    unsigned int v = va_arg(va, unsigned int);
                    s = utos(v, 10, bf, NUM_BUFSIZE);
                }
                logOut(err, s, NUM_BUFSIZE - (s - bf));
                break;

            case 'd':
                if (is_long){
                    long v = va_arg(va, long);
                    s = ltos(v, 10, bf, NUM_BUFSIZE);
                }else{
                    int v = va_arg(va, int);
                    s = itos(v, 10, bf, NUM_BUFSIZE);
                }
                logOut(err, s, NUM_BUFSIZE - (s - bf));
                break;

            case 'x':
                if (is_long){
                    unsigned long v = va_arg(va, unsigned long);
                    s = ultos(v, 16, bf, NUM_BUFSIZE);
                }else{
                    unsigned int v = va_arg(va, unsigned int);
                    s = utos(v, 16, bf, NUM_BUFSIZE);
                }
                logOut(err, s, NUM_BUFSIZE - (s - bf));
                break;

            case 'c':
                ch = (char)(va_arg(va, int));
                logOut(err, &ch, 1);
                break;

            case 's':
                s = va_arg(va, char*);
                len = strlen(s);
                logOut(err, s, len);
                break;

            default:
                mod[0] = '%';
                for (ins = 1; !IS_FLT_SPEC(ch) && IS_MOD(ch); ch = *(fmt++)){
                    mod[ins++] = ch;
                }
                mod[ins++] = ch;
                mod[ins] = '\0';

                if (IS_FLT_SPEC(ch)){
                    double v = va_arg(va, double);
                    int len = snprintf(bf, NUM_BUFSIZE, mod, v);
                    len = PDDL_MIN(len, NUM_BUFSIZE);
                    logOut(err, bf, len);

                }else{
                    fprintf(stderr, "\nFATAL ERROR: unkown format flag '%c'\n", ch);
                    exit(-1);
                }
        }
    }

    fprintf(err->log_out, "\n");
    fflush(err->log_out);
    va_end(va);
}
