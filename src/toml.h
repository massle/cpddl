/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef _PDDL_TOML_H_
#define _PDDL_TOML_H_

#include <pddl/common.h>
#include <pddl/err.h>
#include "_toml.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define PDDL_TOML_FN_MAXSIZE 256
#define PDDL_TOML_PATH_MAXSIZE 1024
#define PDDL_TOML_STACK_MAXSIZE 8

struct pddl_toml_ctx{
    pddl_toml_table_t *table;
    int path_idx;
};
typedef struct pddl_toml_ctx pddl_toml_ctx_t;

struct pddl_toml {
    char fn[PDDL_TOML_FN_MAXSIZE];
    pddl_toml_table_t *root;

    char cur_path[PDDL_TOML_PATH_MAXSIZE];
    int cur_path_size;
    pddl_toml_ctx_t stack[PDDL_TOML_STACK_MAXSIZE];
    int stack_size;
};
typedef struct pddl_toml pddl_toml_t;

int pddlTomlInitFile(pddl_toml_t *t, const char *fn, pddl_err_t *err);
void pddlTomlFree(pddl_toml_t *t);

int pddlTomlPushTable(pddl_toml_t *t, const char *key, pddl_err_t *err);
void pddlTomlPop(pddl_toml_t *t);
int pddlTomlInt(pddl_toml_t *t, const char *key, int *dst,
                pddl_bool_t required, pddl_err_t *err);
int pddlTomlFlt(pddl_toml_t *t, const char *key, float *dst,
                pddl_bool_t required, pddl_err_t *err);
int pddlTomlDbl(pddl_toml_t *t, const char *key, double *dst,
                pddl_bool_t required, pddl_err_t *err);
int pddlTomlBool(pddl_toml_t *t, const char *key, pddl_bool_t *dst,
                 pddl_bool_t required, pddl_err_t *err);
int pddlTomlStr(pddl_toml_t *t, const char *key, char **dst,
                pddl_bool_t required, pddl_err_t *err);
int pddlTomlArrStr(pddl_toml_t *t, const char *key, char ***dst, int *dst_size,
                   pddl_bool_t required, pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PDDL_TOML_H_ */
