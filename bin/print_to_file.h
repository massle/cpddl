#ifndef _PRINT_TO_FILE_H_
#define _PRINT_TO_FILE_H_

static FILE *openFile(const char *fn)
{
    if (strcmp(fn, "-") == 0
            || strcmp(fn, "stdout") == 0)
        return stdout;
    if (strcmp(fn, "stderr") == 0)
        return stderr;
    FILE *fout = fopen(fn, "w");
    return fout;
}

static void closeFile(FILE *f)
{
    if (f != NULL && f != stdout && f != stderr)
        fclose(f);
}

#define PRINT_TO_FILE(ERR, OUT, S, CMD) \
    do { \
    if ((OUT) != NULL){ \
        FILE *fout = openFile((OUT)); \
        if (fout != NULL){ \
            BOR_INFO((ERR), "Printing %s to %s ...", (S), (OUT)); \
            CMD; \
            closeFile(fout); \
        }else{ \
            BOR_ERR_RET((ERR), -1, "Could not open '%s'", (OUT)); \
        } \
    } \
    } while (0) 

#endif /* _PRINT_TO_FILE_H_ */
