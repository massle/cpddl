#ifndef OPTS_H
#define OPTS_H
void optsFree(void);

void optsStartGroup(const char *header);

void optsAddFlag(const char *long_name,
                 char short_name,
                 int *set,
                 int default_value,
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

int opts(int *argc, char **argv);
void optsPrint(FILE *fout);
#endif /* OPTS_H */
