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

void optsAddTags(const char *long_name,
                 char short_name,
                 const char *default_value,
                 int (*fn)(const char *tag),
                 const char *desc);


int opts(int *argc, char **argv);
void optsPrint(FILE *fout);

int optsProcessTags(const char *_s, int (*fn)(const char *t));

#endif /* OPTS_H */
