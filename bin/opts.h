#ifndef OPTS_H
#define OPTS_H

#include <pddl/pddl.h>

struct opts_param {
    char *name;
    void *dst;
    int is_int;
    int is_flt;
    int is_flag;
};
typedef struct opts_param opts_param_t;

struct opts_params {
    opts_param_t *param;
    int param_size;
    int param_alloc;
};
typedef struct opts_params opts_params_t;

void optsFree(void);

void optsStartGroup(const char *header);

void optsAddFlag(const char *long_name,
                 char short_name,
                 int *set,
                 int default_value,
                 const char *desc);

void optsAddFlagFn(const char *long_name,
                   char short_name,
                   int (*fn)(int enabled),
                   const char *desc);

void optsAddInt(const char *long_name,
                char short_name,
                int *set,
                int default_value,
                const char *desc);

void optsAddFlt(const char *long_name,
                char short_name,
                float *set,
                float default_value,
                const char *desc);

void optsAddStr(const char *long_name,
                char short_name,
                char **set,
                const char *default_value,
                const char *desc);

void optsAddTags(const char *long_name,
                 char short_name,
                 const char *default_value,
                 int (*fn)(const char *tag),
                 const char *desc);

opts_params_t *optsAddParams(const char *long_name,
                             char short_name,
                             const char *desc);


int opts(int *argc, char **argv);
void optsPrint(FILE *fout);

int optsProcessTags(const char *_s, int (*fn)(const char *t));

void optsParamsInit(opts_params_t *params);
void optsParamsFree(opts_params_t *params);
void optsParamsAddInt(opts_params_t *params, const char *name, void *dst);
void optsParamsAddFlt(opts_params_t *params, const char *name, void *dst);
void optsParamsAddFlag(opts_params_t *params, const char *name, void *dst);
int optsParamsParse(opts_params_t *params, const char *text);

#endif /* OPTS_H */
