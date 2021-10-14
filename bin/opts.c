#include <boruvka/alloc.h>
#include <stdio.h>
#include <limits.h>
#include "opts.h"

#define FLAG 1
#define INT  2
#define FLT  3
#define STR  4


struct opt_group {
    char *header;
};
typedef struct opt_group opt_group_t;

struct opt_opt {
    int type;
    int group;
    char *long_name;
    char short_name;
    int idefault;
    float fdefault;
    char *sdefault;
    void *set;
    char *desc;
    char **allowed_values;
};
typedef struct opt_opt opt_opt_t;

struct opt {
    opt_group_t *group;
    int group_size;
    int group_alloc;

    opt_opt_t *opt;
    int opt_size;
    int opt_alloc;
};
typedef struct opt opt_t;

static opt_t o = { 0 };

static int cur_group = -1;

void optsFree(void)
{
    for (int i = 0; i < o.group_size; ++i){
        BOR_FREE(o.group[i].header);
    }
    if (o.group != NULL)
        BOR_FREE(o.group);

    for (int i = 0; i < o.opt_size; ++i){
        if (o.opt[i].long_name != NULL)
            BOR_FREE(o.opt[i].long_name);
        if (o.opt[i].sdefault != NULL)
            BOR_FREE(o.opt[i].sdefault);
        if (o.opt[i].desc != NULL)
            BOR_FREE(o.opt[i].desc);
    }
    if (o.opt != NULL)
        BOR_FREE(o.opt);
}

void optsStartGroup(const char *header)
{
    if (o.group_size == o.group_alloc){
        if (o.group_alloc == 0)
            o.group_alloc = 1;
        o.group_alloc *= 2;
        o.group = BOR_REALLOC_ARR(o.group, opt_group_t, o.group_alloc);
    }

    cur_group = o.group_size;
    opt_group_t *g = o.group + o.group_size++;
    g->header = BOR_STRDUP(header);
}

static opt_opt_t *optsAdd(int type,
                          const char *long_name,
                          char short_name,
                          void *set,
                          const char *desc)
{
    if (o.opt_size == o.opt_alloc){
        if (o.opt_alloc == 0)
            o.opt_alloc = 1;
        o.opt_alloc *= 2;
        o.opt = BOR_REALLOC_ARR(o.opt, opt_opt_t, o.opt_alloc);
    }

    opt_opt_t *opt = o.opt + o.opt_size++;
    bzero(opt, sizeof(*opt));
    opt->group = cur_group;
    opt->type = type;
    if (long_name != NULL)
        opt->long_name = BOR_STRDUP(long_name);
    opt->short_name = short_name;
    opt->set = set;
    if (desc != NULL)
        opt->desc = BOR_STRDUP(desc);
    return opt;
}

void optsAddFlag(const char *long_name,
                 char short_name,
                 int *set,
                 int default_value,
                 const char *desc)
{
    opt_opt_t *opt = optsAdd(FLAG, long_name, short_name, set, desc);
    opt->idefault = default_value;
    *(int *)set = default_value;
}

void optsAddInt(const char *long_name,
                char short_name,
                int *set,
                int default_value,
                const char *desc)
{
    opt_opt_t *opt = optsAdd(INT, long_name, short_name, set, desc);
    opt->idefault = default_value;
    *(int *)set = default_value;
}

void optsAddFlt(const char *long_name,
                char short_name,
                float *set,
                float default_value,
                const char *desc)
{
    opt_opt_t *opt = optsAdd(FLT, long_name, short_name, set, desc);
    opt->fdefault = default_value;
    *(float *)set = default_value;
}

void optsAddStr(const char *long_name,
                char short_name,
                char **set,
                const char *default_value,
                const char *desc)
{
    opt_opt_t *opt = optsAdd(STR, long_name, short_name, set, desc);
    if (default_value != NULL){
        opt->sdefault = BOR_STRDUP(default_value);
        *(char **)set = opt->sdefault;
    }
}

static opt_opt_t *findOptLong(const char *name)
{
    for (int i = 0; i < o.opt_size; i++){
        if (o.opt[i].long_name && strcmp(o.opt[i].long_name, name) == 0)
            return o.opt + i;
    }
    return NULL;
}

static void optSetFlag(opt_opt_t *opt)
{
    if (opt->set){
        *(int *)opt->set = 1;
    }
}

static void optSetNoFlag(opt_opt_t *opt)
{
    if (opt->set){
        *(int *)opt->set = 0;
    }
}

static int optSet(opt_opt_t *opt, const char *oname, const char *val)
{
    if (opt->type == INT){
        char *end;
        long v = strtol(val, &end, 10);
        if (*end != 0x0){
            fprintf(stderr, "Error: Invalid value for the option %s\n", oname);
            return -1;
        }
        if (v >= INT_MAX || v <= INT_MIN){
            fprintf(stderr, "Error: The value for the option %s is not an integer\n", oname);
            return -1;
        }
        *(int *)opt->set = v;

    }else if (opt->type == FLT){
        char *end;
        float v = strtol(val, &end, 10);
        if (*end != 0x0){
            fprintf(stderr, "Error: Invalid value for the option %s\n", oname);
            return -1;
        }
        *(float *)opt->set = v;

    }else if (opt->type == STR){
        if (opt->set)
            *(char **)opt->set = (char *)val;
    }

    return 0;
}

static opt_opt_t *findOptShort(char name)
{
    for (int i = 0; i < o.opt_size; i++){
        if (name == o.opt[i].short_name)
            return o.opt + i;
    }
    return NULL;
}

static opt_opt_t *findOpt(char *_arg)
{
    char *arg = _arg;
    opt_opt_t *opt;

    if (arg[0] == '-'){
        if (arg[1] == '-'){
            return findOptLong(arg + 2);
        }else{
            if (arg[1] == 0x0)
                return NULL;

            for (++arg; *arg != 0x0; ++arg){
                opt = findOptShort(*arg);
                if (arg[1] == 0x0){
                    return opt;
                }else if (opt->type == FLAG){
                    optSetFlag(opt);
                }else{
                    fprintf(stderr, "Error: Unknown option %s.\n", _arg);
                    return NULL;
                }
            }
            
        }
    }

    return NULL;
}


int opts(int *argc, char **argv)
{
    if (*argc <= 1)
        return 0;

    int args_remaining = 1;
    for (int i = 1; i < *argc; i++){
        opt_opt_t *opt = findOpt(argv[i]);

        if (opt){
            if (opt->type == FLAG){
                optSetFlag(opt);
            }else{
                if (i + 1 < *argc){
                    ++i;
                    if (optSet(opt, argv[i - 1], argv[i]) != 0)
                        return -1;
                }else{
                    fprintf(stderr, "Error: Missing value for the option %s\n", argv[i]);
                    return -1;
                }
            }
        }else{
            int found = 0;
            if (strncmp(argv[i], "--no-", 5) == 0){
                char arg[strlen(argv[i]) + 1];
                sprintf(arg, "--%s", argv[i] + 5);
                opt = findOpt(arg);
                if (opt != NULL && opt->type == FLAG){
                    optSetNoFlag(opt);
                    found = 1;
                }
            }

            if (!found){
                if (strncmp(argv[i], "-", 1) == 0){
                    fprintf(stderr, "Error: Invalid option %s\n", argv[i]);
                    return -1;
                }
                argv[args_remaining++] = argv[i];
            }
        }
    }

    *argc = args_remaining;
    return 0;
}

static int maxLen(int group)
{
    int maxlen = 0;
    for (int i = 0; i < o.opt_size; ++i){
        const opt_opt_t *opt = o.opt + i;
        if (opt->group != group)
            continue;

        int len = 0;
        if (opt->long_name != NULL){
            len = 2 + strlen(opt->long_name);
            if (opt->type == FLAG){
                len += 5;
            }
            if (opt->short_name != 0x0){
                len += 3;
            }
        }else{
            len = 2;
        }
        maxlen = BOR_MAX(maxlen, len);
    }
    return maxlen;
}

static void optsPrintDefault(const opt_opt_t *opt, FILE *fout)
{
    fprintf(fout, " (default: ");
    if (opt->type == FLAG){
        if (opt->idefault){
            fprintf(fout, "enabled");
        }else{
            fprintf(fout, "disabled");
        }

    }else if (opt->type == INT){
        fprintf(fout, "%d", opt->idefault);

    }else if (opt->type == FLT){
        fprintf(fout, "%.4f", opt->fdefault);

    }else if (opt->type == STR){
        if (opt->sdefault == NULL){
            fprintf(fout, "nil");
        }else{
            fprintf(fout, "%s", opt->sdefault);
        }
    }
    fprintf(fout, ")");
}

static void optsPrintOpts(int group, FILE *fout)
{
    int width = maxLen(group);
    for (int i = 0; i < o.opt_size; ++i){
        const opt_opt_t *opt = o.opt + i;
        if (opt->group != group)
            continue;

        fprintf(fout, "    ");
        int prefixlen = 4;

        int len = 0;
        if (opt->long_name != NULL){
            if (opt->type == FLAG){
                fprintf(fout, "--(no-)%s", opt->long_name);
                len += 7 + strlen(opt->long_name);
            }else{
                fprintf(fout, "--%s", opt->long_name);
                len += 2 + strlen(opt->long_name);
            }
        }

        if (opt->short_name != 0x0){
            if (opt->long_name != NULL){
                fprintf(fout, "/");
                len += 1;
            }
            fprintf(fout, "-%c", opt->short_name);
            len += 2;
        }
        for (int i = len; i < width; ++i)
            fprintf(fout, " ");
        prefixlen += width;

        fprintf(fout, "  ");
        prefixlen += 2;

        if (opt->type == FLAG){
            fprintf(fout, "   ");
        }else if (opt->type == INT){
            fprintf(fout, "int");
        }else if (opt->type == FLT){
            fprintf(fout, "flt");
        }else if (opt->type == STR){
            fprintf(fout, "str");
        }
        prefixlen += 3;

        if (opt->desc != NULL){
            fprintf(fout, "  ");
            prefixlen += 2;

            const char *c = opt->desc;
            while (*c != 0x0){
                fprintf(fout, "%c", *c);
                if (*c == '\n'){
                    for (int j = 0; j < prefixlen; ++j)
                        fprintf(fout, " ");
                }
                ++c;
            }
        }
        optsPrintDefault(opt, fout);
        fprintf(fout, "\n");
    }
}

void optsPrint(FILE *fout)
{
    optsPrintOpts(-1, fout);
    for (int gi = 0; gi < o.group_size; ++gi){
        fprintf(fout, "\n%s\n", o.group[gi].header);
        optsPrintOpts(gi, fout);
    }
    fprintf(fout, "\n");
}

int optsProcessTags(const char *_s, int (*fn)(const char *t))
{
    if (_s == NULL)
        return 0;
    if (*_s == 0x0)
        return 0;

    char *s = BOR_STRDUP(_s);
    char *cur = s;
    char *next = s + 1;
    do {
        for (; *next != 0x0 && *next != ':'; ++next);
        int shift = *next != 0x0;
        *next = 0x0;
        if (fn(cur) != 0){
            BOR_FREE(s);
            return -1;
        }

        if (shift)
            ++next;
        cur = next;
    } while (*next != 0x0);
    BOR_FREE(s);

    return 0;
}

