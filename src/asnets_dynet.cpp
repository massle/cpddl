/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "sqlite3.h"
#include "toml.h"
#include "pddl/asnets.h"
#include "pddl/asnets_task.h"
#include "pddl/asnets_train_data.h"
#include "pddl/sha256.h"
#include "pddl/pddl_file.h"
#include "pddl/subprocess.h"
#include "pddl/libs_info.h"

#ifndef PDDL_DYNET
# error "asnets_dynet.cpp requires DyNet library!"
#endif /* PDDL_DYNET */

#include <dynet/dynet.h>
#include <dynet/expr.h>
#include <dynet/training.h>
#include <dynet/param-init.h>

const char * const pddl_dynet_version = "not exported";

static const float SMALL_CONST = 1e-6f;
static const float MIN_ACTIVATION_VALUE = -1.f;

static const char *teacherName(pddl_asnets_teacher_t teacher)
{
    switch (teacher){
        case PDDL_ASNETS_TEACHER_ASTAR_LMCUT:
            return "astar-lmcut";
        case PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD:
            return "external-fd";
        case PDDL_ASNETS_TEACHER_FAST_DOWNWARD:
            return "external-fast-downward";
    }
    return "(unknown)";
}

static int teacherNameToID(const char *name, pddl_asnets_teacher_t *teacher)
{
    if (strcmp(name, "astar-lmcut") == 0){
        *teacher = PDDL_ASNETS_TEACHER_ASTAR_LMCUT;
        return 0;

    }else if (strcmp(name, "external-fd") == 0){
        *teacher = PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD;
        return 0;

    }else if (strcmp(name, "external-fast-downward") == 0){
        *teacher = PDDL_ASNETS_TEACHER_FAST_DOWNWARD;
        return 0;
    }
    return -1;
}

void pddlFDConfigLog(const pddl_fd_config_t *fd_cfg, pddl_err_t *err)
{
    if (fd_cfg->saved_files_path != NULL)
        LOG(err, "asnets_curr_dir_path = %s", fd_cfg->saved_files_path);
    if (fd_cfg->fd_interpreter != NULL)
        LOG(err, "fd_interpreter = %s", fd_cfg->fd_interpreter);
    if (fd_cfg->fd_executable_path != NULL)
        LOG(err, "fd_executable_path = %s", fd_cfg->fd_executable_path);
    if (fd_cfg->sas_file_prefix != NULL)
        LOG(err, "sas_file_prefix = %s", fd_cfg->sas_file_prefix);
    if (fd_cfg->plan_file_prefix != NULL)
        LOG(err, "plan_file_prefix = %s", fd_cfg->plan_file_prefix);
    LOG_CONFIG_INT(fd_cfg, use_osp_planner, err);
    LOG_CONFIG_INT(fd_cfg, use_unique_filenames, err);
    LOG_CONFIG_INT(fd_cfg, fd_arg_size, err);
    for (int i = 0; i < fd_cfg->fd_arg_size; ++i)
    {
        if (fd_cfg->fd_args[i] != NULL) // not sure if this null check required or not here
            LOG(err, "fd_args[%d] = %s", i, fd_cfg->fd_args[i]);
    }
}

void pddlFDConfigInit(pddl_fd_config_t *cfg)
{
    ZEROIZE(cfg);
    cfg->use_osp_planner = 1; // will be overwritten from config file if present
    cfg->use_unique_filenames = 1;
    cfg->plan_file_prefix = STRDUP("plan_file_");
    cfg->sas_file_prefix = STRDUP("sas_file_");
    cfg->fd_interpreter = STRDUP("python3");
    cfg->fd_arg_size = 0;
    // remaining vars to be set from config file
}

void pddlFDConfigFree(pddl_fd_config_t *cfg)
{
    if (cfg->saved_files_path != NULL)
        FREE(cfg->saved_files_path);
    if (cfg->fd_interpreter != NULL)
        FREE(cfg->fd_interpreter);
    if (cfg->fd_executable_path != NULL)
        FREE(cfg->fd_executable_path);
    if (cfg->plan_file_prefix != NULL)
        FREE(cfg->plan_file_prefix);
    if (cfg->sas_file_prefix != NULL)
        FREE(cfg->sas_file_prefix);
    for (int i = 0; i < cfg->fd_arg_size; ++i)
    {
        if (cfg->fd_args[i] != NULL) // not sure if this null check required or not here
            FREE(cfg->fd_args[i]);
    }
    if (cfg->fd_args != NULL)
        FREE(cfg->fd_args);
}

void pddlFDConfigCopy(pddl_fd_config_t *dst,
                      const pddl_fd_config_t *src)
{
    *dst = *src;
    if (src->saved_files_path != NULL)
        dst->saved_files_path = STRDUP(src->saved_files_path);
    if (src->fd_executable_path != NULL)
        dst->fd_executable_path = STRDUP(src->fd_executable_path);
    if (src->fd_interpreter != NULL)
        dst->fd_interpreter = STRDUP(src->fd_interpreter);
    if (src->sas_file_prefix != NULL)
        dst->sas_file_prefix = STRDUP(src->sas_file_prefix);
    if (src->plan_file_prefix != NULL)
        dst->plan_file_prefix = STRDUP(src->plan_file_prefix);

    if (dst->fd_arg_size > 0){
        dst->fd_args = ALLOC_ARR(char *, dst->fd_arg_size);
        for (int i = 0; i < dst->fd_arg_size; ++i)
            dst->fd_args[i] = STRDUP(src->fd_args[i]);
    }
}

void pddlASNetsConfigLog(const pddl_asnets_config_t *cfg, pddl_err_t *err)
{
    LOG(err, "domain_pddl = %s", cfg->domain_pddl);
    LOG_CONFIG_INT(cfg, problem_pddl_size, err);
    for (int i = 0; i < cfg->problem_pddl_size; ++i)
        LOG(err, "problem_pddl[%d] = %s", i, cfg->problem_pddl[i]);
    LOG_CONFIG_INT(cfg, is_osp_problem, err);
    LOG_CONFIG_INT(cfg, hidden_dimension, err);
    LOG_CONFIG_INT(cfg, num_layers, err);
    LOG_CONFIG_INT(cfg, random_seed, err);
    LOG_CONFIG_DBL(cfg, weight_decay, err);
    LOG_CONFIG_DBL(cfg, dropout_rate, err);
    LOG_CONFIG_INT(cfg, batch_size, err);
    LOG_CONFIG_INT(cfg, double_batch_size_every_epoch, err);
    LOG_CONFIG_INT(cfg, max_train_epochs, err);
    LOG_CONFIG_INT(cfg, train_steps, err);
    LOG_CONFIG_INT(cfg, policy_rollout_limit, err);
    LOG_CONFIG_DBL(cfg, early_termination_success_rate, err);
    LOG_CONFIG_INT(cfg, early_termination_epochs, err);
    LOG_CONFIG_DBL(cfg, teacher_timeout, err);
    LOG(err, "teacher = %s", teacherName(cfg->teacher));
    if (cfg->fd_config != NULL) {
        pddlFDConfigLog(cfg->fd_config, err);
    }
    LOG_CONFIG_STR(cfg, save_model_prefix, err);
}

void pddlASNetsConfigInit(pddl_asnets_config_t *cfg)
{
    ZEROIZE(cfg);
    cfg->is_osp_problem = 0; // will be overwritten from config file if present
    cfg->hidden_dimension = 16;
    cfg->num_layers = 2;
    cfg->random_seed = 6961;
    cfg->weight_decay = 2e-4f;
    cfg->dropout_rate = 0.1f;
    cfg->batch_size = 64;
    cfg->double_batch_size_every_epoch = 0;
    cfg->max_train_epochs = 300;
    cfg->train_steps = 700;
    cfg->policy_rollout_limit = 1000;
    cfg->early_termination_success_rate = 0.999f;
    cfg->early_termination_epochs = 20;
    cfg->teacher_timeout = 10.f;
    cfg->teacher = PDDL_ASNETS_TEACHER_ASTAR_LMCUT;
    cfg->fd_config = NULL;
    cfg->save_model_prefix = NULL;
}

void pddlASNetsConfigInitCopy(pddl_asnets_config_t *dst,
                              const pddl_asnets_config_t *src)
{
    *dst = *src;
    if (src->domain_pddl != NULL)
        dst->domain_pddl = STRDUP(src->domain_pddl);

    if (dst->problem_pddl_size > 0){
        dst->problem_pddl = ALLOC_ARR(char *, dst->problem_pddl_size);
        for (int i = 0; i < dst->problem_pddl_size; ++i)
            dst->problem_pddl[i] = STRDUP(src->problem_pddl[i]);
    }

    if (src->teacher_external_cmd != NULL){
        int size = 0;
        while (src->teacher_external_cmd[size] != NULL)
            ++size;
        dst->teacher_external_cmd = ALLOC_ARR(char *, size + 1);
        for (int i = 0; i < size; ++i)
            dst->teacher_external_cmd[i] = STRDUP(src->teacher_external_cmd[i]);
        dst->teacher_external_cmd[size] = NULL;
    }

    if (src->fd_config != NULL) {
        dst->fd_config = ZALLOC(pddl_fd_config_t); // TO-DO clarify new vs ZALLOC
        pddlFDConfigCopy(dst->fd_config, src->fd_config);
    }
}

#define TOML_INT(K) \
    do { \
        if (pddl_toml_key_exists(c, #K)){ \
            pddl_toml_datum_t d = pddl_toml_int_in(c, #K); \
            if (!d.ok){ \
                pddl_toml_free(top); \
                ERR_RET(err, -1, #K " must be int"); \
            } \
            cfg->K = d.u.i; \
        } \
    } while (0)

#define TOML_FLT(K) \
    do { \
        if (pddl_toml_key_exists(c, #K)){ \
            pddl_toml_datum_t d = pddl_toml_double_in(c, #K); \
            if (!d.ok){ \
                pddl_toml_free(top); \
                ERR_RET(err, -1, #K " must be float"); \
            } \
            cfg->K = d.u.d; \
        } \
    } while (0)

int pddlASNetsConfigInitFromFile(pddl_asnets_config_t *cfg,
                                 const char *filename,
                                 pddl_err_t *err)
{
    pddlASNetsConfigInit(cfg);

    FILE *fin = fopen(filename, "r");
    if (fin == NULL)
        ERR_RET(err, -1, "Could not open file %s", filename);

    pddl_toml_table_t *top = pddl_toml_parse_file(fin, err);
    fclose(fin);
    if (top == NULL){
        TRACE_RET(err, -1);
    }

    pddl_toml_table_t *c = pddl_toml_table_in(top, "asnets");
    if (c == NULL){
        pddl_toml_free(top);
        ERR_RET(err, -1, "No [asnets] section in the configuration file.");
    }

    char *root = NULL;
    if (pddl_toml_key_exists(c, "root")){
        pddl_toml_datum_t d = pddl_toml_string_in(c, "root");
        if (!d.ok){
            pddl_toml_free(top);
            ERR_RET(err, -1, "root must be string");
        }
        root = d.u.s;
        if (strcmp(root, "__PWD__") == 0){
            FREE(root);
            root = pddlDirname(filename);
        }
    }

    if (pddl_toml_key_exists(c, "domain")){
        pddl_toml_datum_t d = pddl_toml_string_in(c, "domain");
        if (!d.ok){
            pddl_toml_free(top);
            ERR_RET(err, -1, "domain must be string");
        }
        if (root != NULL){
            char *fn = ALLOC_ARR(char, strlen(root) + strlen(d.u.s) + 2);
            sprintf(fn, "%s/%s", root, d.u.s);
            pddlASNetsConfigSetDomain(cfg, fn);
            FREE(fn);
        }else{
            pddlASNetsConfigSetDomain(cfg, d.u.s);
        }
        FREE(d.u.s);
    }

    if (pddl_toml_key_exists(c, "problems")){
        const pddl_toml_array_t *arr = pddl_toml_array_in(c, "problems");
        if (arr == NULL){
            pddl_toml_free(top);
            ERR_RET(err, -1, "problems must be array");
        }
        int size = pddl_toml_array_nelem(arr);
        for (int i = 0; i < size; ++i){
            pddl_toml_datum_t d = pddl_toml_string_at(arr, i);
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "Each element of problems must be string");
            }
            if (root != NULL){
                char *fn = ALLOC_ARR(char, strlen(root) + strlen(d.u.s) + 2);
                sprintf(fn, "%s/%s", root, d.u.s);
                if (pddlIsFile(fn)){
                    pddlASNetsConfigAddProblem(cfg, fn);
                }else{
                    int len;
                    char **files = pddlListDirPDDLFiles(fn, &len, err);
                    if (files == NULL){
                        FREE(fn);
                        TRACE_RET(err, -1);
                    }

                    for (int i = 0; i < len; ++i){
                        if (strstr(files[i], "domain") != NULL){
                            FREE(files[i]);
                            continue;
                        }
                        if (pddlIsFile(files[i]))
                            pddlASNetsConfigAddProblem(cfg, files[i]);
                        FREE(files[i]);
                    }
                    FREE(files);
                }
                FREE(fn);
            }else{
                pddlASNetsConfigAddProblem(cfg, d.u.s);
            }
            FREE(d.u.s);
        }
    }

    if (root != NULL)
        FREE(root);

    TOML_INT(is_osp_problem);
    TOML_INT(hidden_dimension);
    TOML_INT(num_layers);
    TOML_INT(random_seed);
    TOML_FLT(weight_decay);
    TOML_FLT(dropout_rate);
    TOML_INT(batch_size);
    TOML_INT(double_batch_size_every_epoch);
    TOML_INT(max_train_epochs);
    TOML_INT(train_steps);
    TOML_INT(policy_rollout_limit);
    TOML_FLT(teacher_timeout);
    TOML_FLT(early_termination_success_rate);
    TOML_INT(early_termination_epochs);

    if (pddl_toml_key_exists(c, "teacher")){
        pddl_toml_datum_t d = pddl_toml_string_in(c, "teacher");
        if (!d.ok){
            pddl_toml_free(top);
            ERR_RET(err, -1, "teacher must be string");
        }

        if (teacherNameToID(d.u.s, &cfg->teacher) != 0){
            pddl_toml_free(top);
            ERR_RET(err, -1, "Unkown teacher type \"%s\"", d.u.s);
        }
        FREE(d.u.s);
    }

    if (pddl_toml_key_exists(c, "teacher_external_cmd")){
        pddl_toml_array_t *arr = pddl_toml_array_in(c, "teacher_external_cmd");
        if (arr == NULL){
            pddl_toml_free(top);
            ERR_RET(err, -1, "teacher_external_cmd must be array of strings");
        }

        int size = pddl_toml_array_nelem(arr);
        if (size == 0){
            pddl_toml_free(top);
            ERR_RET(err, -1, "teacher_external_cmd must be non-empty");
        }

        char **cmd = ALLOC_ARR(char *, size + 1);
        for (int i = 0; i < size; ++i){
            pddl_toml_datum_t d = pddl_toml_string_at(arr, i);
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "teacher_external_cmd must be array of strings");
            }
            cmd[i] = d.u.s;
        }
        cmd[size] = NULL;
        pddlASNetsConfigSetTeacherExternalCmd(cfg, cmd);

        for (int i = 0; i < size; ++i)
            FREE(cmd[i]);
        FREE(cmd);
    }

    if (cfg->teacher == PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD
            && cfg->teacher_external_cmd == NULL){
        pddl_toml_free(top);
        ERR_RET(err, -1, "teacher_external_cmd must be defined if teacher"
                " \"%s\" is used",
                teacherName(PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD));
    }


    if (cfg->teacher == PDDL_ASNETS_TEACHER_FAST_DOWNWARD) {
        cfg->fd_config = ZALLOC(pddl_fd_config_t); // TO-DO - clarify new vs ZALLOC
        pddlFDConfigInit(cfg->fd_config);
        pddl_toml_table_t *f = pddl_toml_table_in(top, "fast_downward");
        if (f == NULL){
            pddl_toml_free(top);
            ERR_RET(err, -1, "No [fast_downward] section in the configuration file.");
        }

        if (pddl_toml_key_exists(f, "use_osp_planner")){
            pddl_toml_datum_t d = pddl_toml_int_in(f, "use_osp_planner");
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "use_osp_planner must be int");
            }
            cfg->fd_config->use_osp_planner = d.u.i;
        }

        if (pddl_toml_key_exists(f, "saved_files_path"))
        {
            pddl_toml_datum_t d = pddl_toml_string_in(f, "saved_files_path");
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "saved_files_path must be string");
            }
            cfg->fd_config->saved_files_path = STRDUP(d.u.s);
            FREE(d.u.s);
        }

        if (pddl_toml_key_exists(f, "fd_interpreter")){
            pddl_toml_datum_t d = pddl_toml_string_in(f, "fd_interpreter");
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "fd_interpreter must be string");
            }
            cfg->fd_config->fd_interpreter = STRDUP(d.u.s);
            FREE(d.u.s);
        }

        if (pddl_toml_key_exists(f, "fd_executable_path")){
            pddl_toml_datum_t d = pddl_toml_string_in(f, "fd_executable_path");
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "fd_executable_path must be string");
            }
            cfg->fd_config->fd_executable_path = STRDUP(d.u.s);
            FREE(d.u.s);
        }

        if (pddl_toml_key_exists(f, "plan_file_prefix")){
            pddl_toml_datum_t d = pddl_toml_string_in(f, "plan_file_prefix");
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "plan_file_prefix must be string");
            }
            cfg->fd_config->plan_file_prefix = STRDUP(d.u.s);
            FREE(d.u.s);
        }

        if (pddl_toml_key_exists(f, "sas_file_prefix")){
            pddl_toml_datum_t d = pddl_toml_string_in(f, "sas_file_prefix");
            if (!d.ok){
                pddl_toml_free(top);
                ERR_RET(err, -1, "sas_file_prefix must be string");
            }
            cfg->fd_config->sas_file_prefix = STRDUP(d.u.s);
            FREE(d.u.s);
        }

        if (pddl_toml_key_exists(f, "fd_args")){   
            const pddl_toml_array_t *arr = pddl_toml_array_in(f, "fd_args");
            if (arr == NULL){
                pddl_toml_free(top);
                ERR_RET(err, -1, "fd_args must be array");
            }
            int size = pddl_toml_array_nelem(arr);
            for (int i = 0; i < size; ++i){
                pddl_toml_datum_t d = pddl_toml_string_at(arr, i);
                if (!d.ok){
                    pddl_toml_free(top);
                    ERR_RET(err, -1, "Each element of fd_args must be string");
                }
                cfg->fd_config->fd_args = REALLOC_ARR(cfg->fd_config->fd_args, char *, cfg->fd_config->fd_arg_size + 1);
                cfg->fd_config->fd_args[cfg->fd_config->fd_arg_size++] = STRDUP(d.u.s);
                FREE(d.u.s);
            }
        }
    }

    pddl_toml_free(top);
    return 0;
}

void pddlASNetsConfigFree(pddl_asnets_config_t *cfg)
{
    if (cfg->domain_pddl != NULL)
        FREE(cfg->domain_pddl);
    for (int i = 0; i < cfg->problem_pddl_size; ++i)
        FREE(cfg->problem_pddl[i]);
    if (cfg->problem_pddl != NULL)
        FREE(cfg->problem_pddl);

    if (cfg->teacher_external_cmd != NULL){
        for (int i = 0; cfg->teacher_external_cmd[i] != NULL; ++i)
            FREE(cfg->teacher_external_cmd[i]);
        FREE(cfg->teacher_external_cmd);
    }

    if (cfg->fd_config != NULL) {
        pddlFDConfigFree(cfg->fd_config);
        FREE(cfg->fd_config);
    }
}

void pddlASNetsConfigSetDomain(pddl_asnets_config_t *cfg, const char *fn)
{
    if (cfg->domain_pddl != NULL)
        FREE(cfg->domain_pddl);
    cfg->domain_pddl = STRDUP(fn);
}

void pddlASNetsConfigAddProblem(pddl_asnets_config_t *cfg, const char *fn)
{
    cfg->problem_pddl = REALLOC_ARR(cfg->problem_pddl, char *,
                                    cfg->problem_pddl_size + 1);
    cfg->problem_pddl[cfg->problem_pddl_size++] = STRDUP(fn);
}

void pddlASNetsConfigSetTeacherExternalCmd(pddl_asnets_config_t *cfg,
                                           char * const * argv)
{
    int size = 0;
    while (argv[size] != NULL)
        ++size;

    if (cfg->teacher_external_cmd != NULL){
        for (int i = 0; cfg->teacher_external_cmd[i] != NULL; ++i)
            FREE(cfg->teacher_external_cmd[i]);
        FREE(cfg->teacher_external_cmd);
    }
    cfg->teacher_external_cmd = ALLOC_ARR(char *, size + 1);
    for (int i = 0; i < size; ++i)
        cfg->teacher_external_cmd[i] = STRDUP(argv[i]);
    cfg->teacher_external_cmd[size] = NULL;
}

void pddlASNetsConfigWrite(const pddl_asnets_config_t *cfg, FILE *fout)
{
    fprintf(fout, "[asnets]\n");
    if (cfg->domain_pddl == NULL){
        fprintf(fout, "#\n");
        fprintf(fout, "# The following defines the input planning tasks:\n");
        fprintf(fout, "#\n");
        fprintf(fout, "# root = \"__PWD__\"\n");
        fprintf(fout, "# domain = \"domain.pddl\"\n");
        fprintf(fout, "# problems = [\"prob1.pddl\", \"prob2.pddl\"]\n");
    }else{
        fprintf(fout, "domain = \"%s\"\n", cfg->domain_pddl);
        fprintf(fout, "problems = [\n");
        for (int i = 0; i < cfg->problem_pddl_size; ++i)
            fprintf(fout, "    \"%s\",\n", cfg->problem_pddl[i]);
        fprintf(fout, "]\n");
    }
    fprintf(fout, "hidden_dimension = %d\n", cfg->hidden_dimension);
    fprintf(fout, "num_layers = %d\n", cfg->num_layers);
    fprintf(fout, "random_seed = %d\n", cfg->random_seed);
    fprintf(fout, "weight_decay = %f\n", cfg->weight_decay);
    fprintf(fout, "dropout_rate = %f\n", cfg->dropout_rate);
    fprintf(fout, "batch_size = %d\n", cfg->batch_size);
    fprintf(fout, "double_batch_size_every_epoch = %d\n",
            cfg->double_batch_size_every_epoch);
    fprintf(fout, "max_train_epochs = %d\n", cfg->max_train_epochs);
    fprintf(fout, "train_steps = %d\n", cfg->train_steps);
    fprintf(fout, "policy_rollout_limit = %d\n", cfg->policy_rollout_limit);
    fprintf(fout, "teacher_timeout = %f\n", cfg->teacher_timeout);
    fprintf(fout, "early_termination_success_rate = %f\n",
            cfg->early_termination_success_rate);
    fprintf(fout, "early_termination_epochs = %d\n",
            cfg->early_termination_epochs);

    fprintf(fout, "teacher = \"%s\"", teacherName(cfg->teacher));
    fprintf(fout, " # must be one of \"%s\", \"%s\", \"%s\"\n",
            teacherName(PDDL_ASNETS_TEACHER_ASTAR_LMCUT),
            teacherName(PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD),
            teacherName(PDDL_ASNETS_TEACHER_FAST_DOWNWARD));

    if (cfg->teacher_external_cmd != NULL){
        fprintf(fout, "teacher_external_cmd = [");
        for (int i = 0; cfg->teacher_external_cmd[i] != NULL; ++i){
            if (i != 0)
                fprintf(fout, ", ");
            fprintf(fout, "\"%s\"", cfg->teacher_external_cmd[i]);
        }
        fprintf(fout, "]\n");
    }else{
        fprintf(fout, "# teacher_external_cmd = [\"/bin/bash\", \"/path/to/script.sh\"]");
    }
}

void pddlASNetsPolicyDistributionInit(pddl_asnets_policy_distribution_t *d)
{
    ZEROIZE(d);
}

void pddlASNetsPolicyDistributionFree(pddl_asnets_policy_distribution_t *d)
{
    if (d->op_id != NULL)
        FREE(d->op_id);
    if (d->prob != NULL)
        FREE(d->prob);
}

static dynet::Expression poolMax(const std::vector<dynet::Expression> &in)
{
    if (in.size() == 1)
        return in[0];

    dynet::Expression mat = dynet::concatenate(in, 1);
    return dynet::max_dim(mat, 1);
}

static dynet::Expression maskedSoftmax(dynet::ComputationGraph &cg,
                                       const dynet::Expression &in,
                                       const dynet::Expression &mask)
{
    // Subtract maximum for numerical stability
    dynet::Expression sm = in - dynet::max_dim(in);

    // Compute exponentials
    sm = dynet::exp(sm);

    // Multiply by the mask
    sm = dynet::cmult(sm, mask);

    // Compute sum and clip it so that we don't divide by zero
    dynet::Dim min_sum_dim({1}, sm.dim().batch_elems());
    dynet::Expression min_sum = dynet::constant(cg, min_sum_dim, SMALL_CONST);
    dynet::Expression sum = dynet::max(dynet::sum_rows(sm), min_sum);

    // Normalize each element
    sm = dynet::cdiv(sm, sum);

    return sm;
}

static dynet::Expression crossEntropyLoss(dynet::ComputationGraph &cg,
                                          dynet::Expression output,
                                          dynet::Expression labels)
{
    dynet::Expression o1 = 1 - output;
    dynet::Expression o2 = output;

    // Avoid log(0)
    dynet::Expression small_const = dynet::constant(cg, o1.dim(), SMALL_CONST);
    o1 = dynet::max(o1, small_const);
    o2 = dynet::max(o2, small_const);

    // (1 - y) * log (1 - \pi)
    dynet::Expression e = dynet::cmult(1 - labels, dynet::log(o1));
    // y * log(\pi)
    e = e + dynet::cmult(labels, dynet::log(o2));

    e = dynet::sum_elems(e);
    e = dynet::sum_batches(e);
    e = -e;
    return e;
}


struct ActionModule {
    int hidden_dim;
    int related_props;
    int layer;
    bool is_output;
    int input_vec_size;
    int output_dim;
    dynet::Parameter W;
    dynet::Parameter bias;

    ActionModule(const ActionModule&) = delete;

    // TODO: landmarks/...
    ActionModule(int hidden_dimension,
                 int num_related_propositions,
                 int layer,
                 bool is_output,
                 dynet::ParameterCollection &model)
        : hidden_dim(hidden_dimension),
          related_props(num_related_propositions),
          layer(layer),
          is_output(is_output)
    {
        if (layer == 0){
            // input state
            input_vec_size = related_props;
            // goal specification
            input_vec_size += related_props;
            // applicability of the action
            input_vec_size += 1;

        }else{
            // Related propositions
            input_vec_size = related_props * hidden_dim;
            // Skip connection
            input_vec_size += hidden_dim;
        }

        if (is_output){
            output_dim = 1;
        }else{
            output_dim = hidden_dim;
        }

        std::vector<long> dim_W(2);
        dim_W[0] = output_dim;
        dim_W[1] = input_vec_size;
        W = model.add_parameters(dynet::Dim(dim_W), dynet::ParameterInitNormal());

        std::vector<long> dim_bias(1);
        dim_bias[0] = output_dim;
        bias = model.add_parameters(dynet::Dim(dim_bias), dynet::ParameterInitNormal());
    }

    dynet::Expression expr(dynet::ComputationGraph &cg,
                           const std::vector<dynet::Expression> &input) const
    {
        dynet::Expression w = dynet::parameter(cg, W);
        dynet::Expression b = dynet::parameter(cg, bias);
        dynet::Expression u = dynet::concatenate(input);
        dynet::Expression e = (w * u) + b;
        if (is_output)
            return e;
        return dynet::elu(e);
    }

    dynet::Expression exprInput(dynet::ComputationGraph &cg,
                                const std::vector<dynet::Expression> &input_state,
                                const std::vector<dynet::Expression> &input_goal,
                                const dynet::Expression &input_applicable) const
    {
        ASSERT(layer == 0);
        std::vector<dynet::Expression> input;
        input.insert(input.end(), input_state.begin(), input_state.end());
        input.insert(input.end(), input_goal.begin(), input_goal.end());
        input.push_back(input_applicable);
        return expr(cg, input);
    }
};

struct PropositionModule {
    int hidden_dim;
    int related_acts;
    int layer;
    int input_vec_size;
    dynet::Parameter W;
    dynet::Parameter bias;

    PropositionModule(const PropositionModule&) = delete;

    PropositionModule(int hidden_dimension,
                      int num_related_actions,
                      int layer,
                      dynet::ParameterCollection &model)
        : hidden_dim(hidden_dimension),
          related_acts(num_related_actions),
          layer(layer)
    {
        // Related actions
        input_vec_size = related_acts * hidden_dim;
        if (layer > 0){
            // Skip connection
            input_vec_size += hidden_dim;
        }

        std::vector<long> dim_W(2);
        dim_W[0] = hidden_dim;
        dim_W[1] = input_vec_size;
        W = model.add_parameters(dynet::Dim(dim_W), dynet::ParameterInitNormal());

        std::vector<long> dim_bias(1);
        dim_bias[0] = hidden_dim;
        bias = model.add_parameters(dynet::Dim(dim_bias), dynet::ParameterInitNormal());
    }

    dynet::Expression expr(dynet::ComputationGraph &cg,
                           const std::vector<std::vector<dynet::Expression>> &input) const 
    {
        std::vector<dynet::Expression> pooled_input(input.size());
        for (size_t i = 0; i < input.size(); ++i)
            pooled_input[i] = poolMax(input[i]);

        dynet::Expression w = dynet::parameter(cg, W);
        dynet::Expression b = dynet::parameter(cg, bias);
        dynet::Expression u = dynet::concatenate(pooled_input);
        return dynet::elu((w * u) + b);
    }
};

struct ModelParameters {
    int num_layers;
    int hidden_dim;
    std::vector<std::vector<ActionModule *>> action;
    std::vector<std::vector<PropositionModule *>> prop;
    dynet::ParameterCollection model;

    ModelParameters(const ModelParameters &) = delete;

    ModelParameters(int hidden_dimension,
                    int num_layers,
                    const pddl_asnets_lifted_task_t *task)
        : num_layers(num_layers),
          hidden_dim(hidden_dimension)
    {
        action.resize(num_layers + 1);
        prop.resize(num_layers);

        for (int layer = 0; layer < num_layers; ++layer){
            for (int aid = 0; aid < task->action_size; ++aid){
                ActionModule *am;
                am = new ActionModule(hidden_dimension,
                                      task->action[aid].related_atom_size,
                                      layer, false, model);
                action[layer].push_back(am);
            }

            for (int pid = 0; pid < task->pred_size; ++pid){
                ASSERT(pid != task->pddl.pred.eq_pred
                        || task->pred[pid].related_action_size == 0);
                PropositionModule *pm;
                pm = new PropositionModule(hidden_dimension,
                                           task->pred[pid].related_action_size,
                                           layer, model);
                prop[layer].push_back(pm);
            }
        }

        for (int aid = 0; aid < task->action_size; ++aid){
            ActionModule *am;
            am = new ActionModule(hidden_dimension,
                                  task->action[aid].related_atom_size,
                                  num_layers, true, model);
            action[num_layers].push_back(am);
        }

        ASSERT(num_layers == (int)action.size() - 1);
        ASSERT(num_layers == (int)prop.size());
    }

    ~ModelParameters()
    {
        for (size_t i = 0; i < action.size(); ++i){
            for (size_t j = 0; j < action[i].size(); ++j)
                delete action[i][j];
        }
        for (size_t i = 0; i < prop.size(); ++i){
            for (size_t j = 0; j < prop[i].size(); ++j)
                delete prop[i][j];
        }
    }

    void dumpDebug() const
    {
        for (size_t layer = 0; layer < action.size(); ++layer){
            for (size_t ai = 0; ai < action[layer].size(); ++ai){
                ActionModule *m = action[layer][ai];
                {
                    dynet::Tensor *t = m->W.values();
                    std::vector<float> v = dynet::as_vector(*t);
                    std::cerr << "Action.W " << layer << " " << ai << std::endl;
                    for (float x : v)
                        std::cerr << " " << x;
                    std::cerr << std::endl;
                }

                {
                    dynet::Tensor *t = m->bias.values();
                    std::vector<float> v = dynet::as_vector(*t);
                    std::cerr << "Action.bias " << layer << " " << ai << std::endl;
                    for (float x : v)
                        std::cerr << " " << x;
                    std::cerr << std::endl;
                }
            }
        }

        for (size_t layer = 0; layer < prop.size(); ++layer){
            for (size_t pi = 0; pi < prop[layer].size(); ++pi){
                PropositionModule *m = prop[layer][pi];
                {
                    dynet::Tensor *t = m->W.values();
                    std::vector<float> v = dynet::as_vector(*t);
                    std::cerr << "Proposition.W " << layer << " " << pi << std::endl;
                    for (float x : v)
                        std::cerr << " " << x;
                    std::cerr << std::endl;
                }

                {
                    dynet::Tensor *t = m->bias.values();
                    std::vector<float> v = dynet::as_vector(*t);
                    std::cerr << "Action.bias " << layer << " " << pi << std::endl;
                    for (float x : v)
                        std::cerr << " " << x;
                    std::cerr << std::endl;
                }
            }
        }
    }
};

class MissingInput {
    dynet::Expression input;
    bool created;
    int dimension;

  public:
    MissingInput(int dimension)
        : created(false), dimension(dimension)
    {
    }

    dynet::Expression &get(dynet::ComputationGraph &cg)
    {
        if (!created){
            std::vector<long> d(1, dimension);
            dynet::Dim dim(d);
            // Input is set to the minimum value of the activation function.
            input = dynet::constant(cg, dim, MIN_ACTIVATION_VALUE);
            created = true;
        }

        return input;
    }

};

static void _firstActionLayer(const pddl_asnets_ground_task_t *g,
                              const ModelParameters &model,
                              dynet::ComputationGraph &cg,
                              dynet::Expression input_state,
                              dynet::Expression input_goal_condition,
                              dynet::Expression input_applicable_ops,
                              std::vector<dynet::Expression> &action_layer)
{
    MissingInput missing_input(1);

    for (int op_id = 0; op_id < g->op_size; ++op_id){
        std::vector<dynet::Expression> in_state;
        std::vector<dynet::Expression> in_goal;
        dynet::Expression in_applicable;
        for (int i = 0; i < g->op[op_id].related_fact_size; ++i){
            int fact_id = g->op[op_id].related_fact[i];
            if (fact_id < 0){
                in_state.push_back(missing_input.get(cg));
                in_goal.push_back(missing_input.get(cg));

            }else{
                PANIC_IF(fact_id < 0, "xx2");
                in_state.push_back(dynet::pick(input_state, fact_id));
                in_goal.push_back(dynet::pick(input_goal_condition, fact_id));
            }
            in_applicable = dynet::pick(input_applicable_ops, op_id);
        }
        int action_id = g->op[op_id].action->action_id;
        ActionModule *am = model.action[0][action_id];
        dynet::Expression e = am->exprInput(cg, in_state, in_goal, in_applicable);
        action_layer.push_back(e);
    }
}

static void _actionLayer(const pddl_asnets_ground_task_t *g,
                         const ModelParameters &model,
                         dynet::ComputationGraph &cg,
                         int layer,
                         const std::vector<dynet::Expression> &prop_layer,
                         const std::vector<dynet::Expression> &prev_action_layer,
                         std::vector<dynet::Expression> &action_layer,
                         float dropout_rate)
{
    MissingInput missing_input(model.hidden_dim);

    for (int op_id = 0; op_id < g->op_size; ++op_id){
        std::vector<dynet::Expression> in;
        for (int i = 0; i < g->op[op_id].related_fact_size; ++i){
            int fact_id = g->op[op_id].related_fact[i];
            if (fact_id < 0){
                in.push_back(missing_input.get(cg));

            }else{
                in.push_back(prop_layer[fact_id]);
            }
        }
        in.push_back(prev_action_layer[op_id]);
        int action_id = g->op[op_id].action->action_id;
        ActionModule *am = model.action[layer][action_id];
        dynet::Expression e = am->expr(cg, in);
        if (dropout_rate > 0.f && layer != model.num_layers){
            e = dynet::dropout(e, dropout_rate);
        }
        action_layer.push_back(e);
    }
}

static void _propLayer(const pddl_asnets_ground_task_t *g,
                       const ModelParameters &model,
                       dynet::ComputationGraph &cg,
                       int layer,
                       const std::vector<dynet::Expression> &action_layer,
                       const std::vector<dynet::Expression> *prev_prop_layer,
                       std::vector<dynet::Expression> &prop_layer,
                       float dropout_rate)
{
    MissingInput missing_input(model.hidden_dim);

    for (int fact_id = 0; fact_id < g->fact_size; ++fact_id){
        int pred_id = g->fact[fact_id].pred->pred_id;
        PropositionModule *pm = model.prop[layer][pred_id];

        std::vector<std::vector<dynet::Expression>> input;
        int input_size = g->fact[fact_id].related_op_size;
        if (prev_prop_layer != NULL)
            input_size += 1;
        input.resize(input_size);
        for (int ri = 0; ri < g->fact[fact_id].related_op_size; ++ri){
            int op_id;
            PDDL_IARR_FOR_EACH(g->fact[fact_id].related_op + ri, op_id){
                input[ri].push_back(action_layer[op_id]);
            }

            if (input[ri].size() == 0)
                input[ri].push_back(missing_input.get(cg));
        }
        if (prev_prop_layer != NULL)
            input[input_size - 1].push_back((*prev_prop_layer)[fact_id]);

        dynet::Expression e = pm->expr(cg, input);
        if (dropout_rate > 0.f){
            e = dynet::dropout(e, dropout_rate);
        }
        prop_layer.push_back(e);
    }
}

static dynet::Expression asnetsExpr(const pddl_asnets_ground_task_t *g,
                                    const ModelParameters &model,
                                    dynet::ComputationGraph &cg,
                                    dynet::Expression input_state,
                                    dynet::Expression input_goal_condition,
                                    dynet::Expression input_applicable_ops,
                                    float dropout_rate)
{
    std::vector<std::vector<dynet::Expression>> action_layer;
    action_layer.resize(model.num_layers + 1);
    std::vector<std::vector<dynet::Expression>> prop_layer;
    prop_layer.resize(model.num_layers);

    int layer = 0;
    // First action layer needs to be connected to inputs
    _firstActionLayer(g, model, cg, input_state, input_goal_condition,
                      input_applicable_ops, action_layer[0]);

    for (; layer < model.num_layers; ++layer){
        const std::vector<dynet::Expression> *prev_prop_layer = NULL;
        if (layer > 0)
            prev_prop_layer = &prop_layer[layer - 1];
        _propLayer(g, model, cg, layer, action_layer[layer],
                   prev_prop_layer, prop_layer[layer], dropout_rate);

        _actionLayer(g, model, cg, layer + 1, prop_layer[layer],
                     action_layer[layer], action_layer[layer + 1],
                     dropout_rate);
    }

    dynet::Expression out = dynet::concatenate(action_layer[layer]);

    return maskedSoftmax(cg, out, input_applicable_ops);
}

static void setApplicableOpsVector(const pddl_asnets_ground_task_t *task,
                                   const int *state,
                                   std::vector<float> &applicable_ops)
{
    applicable_ops.resize(task->strips.op.op_size);
    for (size_t i = 0; i < applicable_ops.size(); ++i)
        applicable_ops[i] = 0;

    PDDL_ISET(ops);
    pddlASNetsGroundTaskFDRApplicableOps(task, state, &ops);
    int op_id;
    PDDL_ISET_FOR_EACH(&ops, op_id)
        applicable_ops[op_id] = 1;
    pddlISetFree(&ops);
}

static void setStateVector(const pddl_asnets_ground_task_t *task,
                           const int *s,
                           std::vector<float> &state,
                           std::vector<float> &applicable_ops)
{
    state.resize(task->strips.fact.fact_size);
    for (size_t i = 0; i < state.size(); ++i)
        state[i] = 0;

    PDDL_ISET(strips_state);
    pddlASNetsGroundTaskFDRStateToStrips(task, s, &strips_state);
    int fact_id;
    PDDL_ISET_FOR_EACH(&strips_state, fact_id)
        state[fact_id] = 1;
    pddlISetFree(&strips_state);

    setApplicableOpsVector(task, s, applicable_ops);
}

static void setGoalVector(const pddl_asnets_ground_task_t *task,
                          std::vector<float> &goal)
{
    goal.resize(task->strips.fact.fact_size);
    for (size_t i = 0; i < goal.size(); ++i)
        goal[i] = 0;

    PDDL_ISET(strips_g);
    pddlASNetsGroundTaskFDRGoal(task, &strips_g);
    int fact_id;
    PDDL_ISET_FOR_EACH(&strips_g, fact_id)
        goal[fact_id] = 1;
    pddlISetFree(&strips_g);
}


static int runPolicy(const pddl_asnets_ground_task_t *task,
                     const ModelParameters &params,
                     dynet::ComputationGraph &cg,
                     const int *in_state,
                     int *out_state,
                     pddl_asnets_policy_distribution_t *distr)
{
    std::vector<float> state;
    std::vector<float> goal;
    std::vector<float> applicable_ops;

    setGoalVector(task, goal);
    setStateVector(task, in_state, state, applicable_ops);

    cg.clear();

    std::vector<long> dim(1);
    dim[0] = state.size();
    dynet::Expression e_state = dynet::input(cg, dynet::Dim(dim), state);
    dynet::Expression e_goal = dynet::input(cg, dynet::Dim(dim), goal);

    dim[0] = applicable_ops.size();
    dynet::Expression e_applicable_ops = dynet::input(cg, dynet::Dim(dim), applicable_ops);
    // Dropout is used *only* during training -- we don't need to use it here
    dynet::Expression e_output = asnetsExpr(task, params, cg, e_state, e_goal,
                                            e_applicable_ops, -1);

    std::vector<float> out = dynet::as_vector(cg.forward(e_output));
    ASSERT((int)out.size() == task->strips.op.op_size);

    int best_op_id = -1;
    float best_value = -1;
    for (size_t op_id = 0; op_id < out.size(); ++op_id){
        ASSERT(out[op_id] >= 0.f);
        // Skip operators that are not applicable
        if (applicable_ops[op_id] < .5)
            continue;
        if (out[op_id] > best_value){
            best_op_id = op_id;
            best_value = out[op_id];
        }

        if (distr != NULL){
            if (distr->op_size == distr->op_alloc){
                if (distr->op_alloc == 0)
                    distr->op_alloc = 4;
                distr->op_alloc *= 2;
                distr->op_id = REALLOC_ARR(distr->op_id, int, distr->op_alloc);
                distr->prob = REALLOC_ARR(distr->prob, float, distr->op_alloc);
            }

            distr->op_id[distr->op_size] = op_id;
            distr->prob[distr->op_size] = out[op_id];
            ++distr->op_size;
        }
    }

    if (out_state != NULL && best_op_id >= 0)
        pddlASNetsGroundTaskFDRApplyOp(task, in_state, best_op_id, out_state);

    return best_op_id;
}

struct pddl_asnets_train_stats {
    int max_epochs;
    int epoch;
    int max_train_steps;
    int train_step;
    float overall_loss;
    float success_rate;
    int num_samples;
    int consecutive_successful_epochs;
};
typedef struct pddl_asnets_train_stats pddl_asnets_train_stats_t;

struct pddl_asnets {
    pddl_asnets_config_t cfg;
    pddl_asnets_lifted_task_t lifted_task;
    dynet::ComputationGraph *cg;
    dynet::Trainer *trainer;
    ModelParameters *params;
    pddl_asnets_ground_task_t *ground_task;
    int ground_task_size;

    pddl_asnets_train_stats_t train_stats;
};

struct ASNetsTrainMiniBatchTask {
    int task_id;
    int size;
    int fact_size;
    int op_size;
    std::vector<float> state;
    std::vector<float> goal;
    std::vector<float> applicable_ops;
    std::vector<unsigned int> selected_op;
    dynet::Expression e_state;
    dynet::Expression e_goal;
    dynet::Expression e_applicable_ops;
    dynet::Expression e_output;

    ASNetsTrainMiniBatchTask()
        : task_id(-1), size(0), fact_size(0), op_size(0)
    {}

    void add(std::vector<float> &in_state,
             std::vector<float> &in_applicable_ops,
             int in_selected_op)
    {
        state.insert(state.end(), in_state.begin(), in_state.end());
        applicable_ops.insert(applicable_ops.end(),
                              in_applicable_ops.begin(),
                              in_applicable_ops.end());

        ASSERT(in_selected_op >= 0 && in_selected_op < op_size);
        selected_op.push_back(in_selected_op);
        ++size;
    }

    void createInputs(dynet::ComputationGraph &cg)
    {
        if (size == 0)
            return;
        ASSERT((int)state.size() == size * fact_size);
        ASSERT((int)applicable_ops.size() == size * op_size);
        ASSERT((int)selected_op.size() == size);
        ASSERT((int)goal.size() == fact_size);

        std::vector<long> dim(1);
        dim[0] = fact_size;
        e_state = dynet::input(cg, dynet::Dim(dim, size), state);

        std::vector<float> g;
        for (int i = 0; i < size; ++i)
            g.insert(g.end(), goal.begin(), goal.end());
        e_goal = dynet::input(cg, dynet::Dim(dim, size), g);

        dim[0] = op_size;
        e_applicable_ops = dynet::input(cg, dynet::Dim(dim, size),
                                        applicable_ops);
        e_output = dynet::one_hot(cg, op_size, selected_op);
    }
};

struct ASNetsTrainMiniBatch {
    std::vector<ASNetsTrainMiniBatchTask> batch;

    ASNetsTrainMiniBatch(const pddl_asnets_t *a,
                         const pddl_asnets_train_data_t *data,
                         int minibatch_size)
    {
        if (minibatch_size < 0)
            minibatch_size = data->sample_size;
        minibatch_size = PDDL_MIN(minibatch_size, data->sample_size);
        batch.resize(a->ground_task_size);
        for (int i = 0; i < a->ground_task_size; ++i){
            batch[i].task_id = i;
            batch[i].fact_size = a->ground_task[i].strips.fact.fact_size;
            batch[i].op_size = a->ground_task[i].strips.op.op_size;
            setGoalVector(a->ground_task + i, batch[i].goal);
        }

        for (int sample = 0; sample < minibatch_size; ++sample){
            int task_id, selected_op;
            const int *fdr_state;
            pddlASNetsTrainDataGetSample(data, sample, &task_id,
                                         &selected_op, NULL, &fdr_state);
            std::vector<float> state, applicable_ops;
            setStateVector(a->ground_task + task_id, fdr_state,
                           state, applicable_ops);
            batch[task_id].add(state, applicable_ops, selected_op);
        }
    }

    void createInputs(dynet::ComputationGraph &cg)
    {
        for (size_t task_id = 0; task_id < batch.size(); ++task_id){
            if (batch[task_id].size == 0)
                continue;
            batch[task_id].createInputs(cg);
        }
    }
};

static int policyRollout(pddl_asnets_t *a,
                         const pddl_asnets_ground_task_t *task,
                         pddl_fdr_state_pool_t *states,
                         pddl_iarr_t *trace,
                         pddl_asnets_softgoals_result_t *softgoals_result, // NULL for non-OSP problems
                         pddl_err_t *err)
{
    int ret = 0;
    int *state = ALLOC_ARR(int, task->fdr.var.var_size);
    int *state2 = ALLOC_ARR(int, task->fdr.var.var_size);

    // Save total number of softgoals to softgoals_result
    if (softgoals_result != NULL)
        softgoals_result->total_softgoals = task->fdr.goal.fact_size;
    // Start in the initial state
    pddl_state_id_t state_id = pddlFDRStatePoolInsert(states, task->fdr.init);
    int step = 0;
    for (; step < a->cfg.policy_rollout_limit; ++step)
    {
        // get the last reached state
        pddlFDRStatePoolGet(states, state_id, state);

        if (softgoals_result != NULL)
        {
            // TO-DO: pass only soft goals when extending to OSP with both hard goals and soft goals
            int softgoals_num = pddlFDRCountPartStateConsistentWithState(&task->fdr.goal, state);
            if (softgoals_num > softgoals_result->max_softgoals_achieved)
            {
                softgoals_result->max_softgoals_achieved = softgoals_num;
                softgoals_result->max_softgoals_plan_steps = step;
            }
        }
        // TO-DO: adapt when extending to OSP with both hard goals and soft goals
        if (pddlFDRPartStateIsConsistentWithState(&task->fdr.goal, state))
        {
            ret = 1;
            break;
        }

        // Apply policy. If we get -1, it means the state is dead-end,
        // because there are no applicable operators
        int op_id = runPolicy(task, *a->params, *a->cg, state, state2, NULL);
        if (op_id < 0){
            break;
        }
        if (trace != NULL)
            pddlIArrAdd(trace, op_id);

        // Insert current state
        pddl_state_id_t prev_state_id = state_id;
        state_id = pddlFDRStatePoolInsert(states, state2);
        // If the new state was already in the pool, then we got a cycle
        if (state_id <= prev_state_id){
            break;
        }
    }
    if (softgoals_result != NULL)
        softgoals_result->total_policy_steps = step;

    FREE(state);
    FREE(state2);
    return ret;
}



pddl_asnets_t *pddlASNetsNew(const pddl_asnets_config_t *cfg, pddl_err_t *err)
{
    if (cfg->problem_pddl_size <= 0)
        ERR_RET(err, NULL, "ASNets: At least one problem file is required.");

    CTX(err, "ASNets");
    pddl_asnets_t *a = ZALLOC(pddl_asnets_t);
    pddlASNetsConfigInitCopy(&a->cfg, cfg);
    CTX_NO_TIME(err, "Cfg");
    pddlASNetsConfigLog(&a->cfg, err);
    CTXEND(err);

    int st;
    st = pddlASNetsLiftedTaskInit(&a->lifted_task, cfg->domain_pddl, err);
    if (st < 0){
        CTXEND(err);
        TRACE_RET(err, NULL);
    }

    a->ground_task_size = cfg->problem_pddl_size;
    a->ground_task = ALLOC_ARR(pddl_asnets_ground_task, a->ground_task_size);
    for (int probi = 0; probi < cfg->problem_pddl_size; ++probi){
        st = pddlASNetsGroundTaskInit(&a->ground_task[probi],
                                      &a->lifted_task,
                                      cfg->domain_pddl,
                                      cfg->problem_pddl[probi],
                                      err);
        if (st < 0){
            pddlASNetsLiftedTaskFree(&a->lifted_task);
            for (int i = 0; i < probi; ++i)
                pddlASNetsGroundTaskFree(&a->ground_task[i]);
            FREE(a->ground_task);
            CTXEND(err);
            TRACE_RET(err, NULL);
        }
    }

    dynet::DynetParams dynet_params;
    dynet_params.autobatch = false;
    //dynet_params.mem_descriptor = "4096";
    //dynet_params.profiling = 10;
    dynet_params.random_seed = a->cfg.random_seed;
    //dynet_params.shared_parameters = true;
    dynet_params.weight_decay = a->cfg.weight_decay;
    dynet::initialize(dynet_params);

    a->params = new ModelParameters(cfg->hidden_dimension,
                                    cfg->num_layers,
                                    &a->lifted_task);

    // TODO: Parametrize
    a->trainer = new dynet::AdamTrainer(a->params->model);
    a->cg = new dynet::ComputationGraph();
#ifdef PDDL_DEBUG
    a->cg->set_check_validity(true);
    a->cg->set_immediate_compute(true);
#endif /* PDDL_DEBUG */

    ZEROIZE(&a->train_stats);
    a->train_stats.max_epochs = a->cfg.max_train_epochs;
    a->train_stats.max_train_steps = a->cfg.train_steps;
    a->train_stats.success_rate = -1.f;
    a->train_stats.overall_loss = -1.f;

    CTXEND(err);
    return a;
}

void pddlASNetsDel(pddl_asnets_t *a)
{
    pddlASNetsLiftedTaskFree(&a->lifted_task);
    for (int i = 0; i < a->ground_task_size; ++i)
        pddlASNetsGroundTaskFree(&a->ground_task[i]);
    FREE(a->ground_task);

    delete a->params;
    if (a->trainer != NULL)
        delete a->trainer;
    if (a->cg != NULL)
        delete a->cg;
    pddlASNetsConfigFree(&a->cfg);
    dynet::cleanup();
}

#define SIG_ACTION_W 0
#define SIG_ACTION_B 1
#define SIG_PROP_W 2
#define SIG_PROP_B 3

static const char sql_create_info[]
    = "DROP TABLE IF EXISTS asnets_info;"
      "CREATE TABLE asnets_info ("
            "parameter TEXT,"
            "int_value INT DEFAULT -1,"
            "flt_value REAL DEFAULT -1.,"
            "str_value TEXT DEFAULT NULL"
      ");";
static const char sql_query_info[]
    = "SELECT int_value, flt_value, str_value"
      " FROM asnets_info WHERE parameter = ?;";

static const char sql_create_weights[]
    = "DROP TABLE IF EXISTS asnets_weights;"
      "CREATE TABLE asnets_weights ("
            "id INT PRIMARY KEY,"
            "layer INT,"
            "sig INT,"
            "name TEXT,"
            "idx INT,"
            "weights BLOB"
      ");";
static const char sql_insert_weights[]
    = "INSERT INTO asnets_weights VALUES(?,?,?,?,?,?);";
static const char sql_query_weights[]
    = "SELECT layer, sig, name, idx, weights"
      " FROM asnets_weights WHERE id = ?;";



struct Info {
    char cpddl_version[128];
    char domain_name[128];
    char domain_pddl[4096];
    char domain_hash[PDDL_SHA256_HASH_STR_SIZE];
    pddl_asnets_config_t cfg;
    pddl_asnets_train_stats_t train_stats;
    // TODO: store the whole domain pddl file?

    Info()
    {
        cpddl_version[0] = '\x0';
        domain_name[0] = '\x0';
        domain_hash[0] = '\x0';
        ZEROIZE(&cfg);
        ZEROIZE(&train_stats);
    }

    Info(const pddl_asnets_t *a)
    {
        strncpy(cpddl_version, pddl_version, sizeof(cpddl_version) - 1);
        strncpy(domain_name, a->lifted_task.pddl.domain_name, sizeof(domain_name) - 1);
        strncpy(domain_pddl, a->lifted_task.pddl.domain_file, sizeof(domain_pddl) - 1);
        pddlASNetsLiftedTaskToSHA256(&a->lifted_task, domain_hash);
        cfg = a->cfg;
        train_stats = a->train_stats;
    }

    int checkLoadedInfo(const Info &o, pddl_err_t *err)
    {
        if (cfg.hidden_dimension != o.cfg.hidden_dimension){
            ERR_RET(err, 0, "Hidden dimensions don't match. asnets: %d, loaded: %d",
                    cfg.hidden_dimension, o.cfg.hidden_dimension);
        }

        if (cfg.num_layers != o.cfg.num_layers){
            ERR_RET(err, 0, "Number of layers don't match. asnets: %d, loaded: %d",
                    cfg.num_layers, o.cfg.num_layers);
        }

        if (strcmp(domain_name, o.domain_name) != 0){
            ERR_RET(err, 0, "Domain names differ. asnets: %s, loaded: %s",
                    domain_name, o.domain_name);
        }

        if (strcmp(domain_hash, o.domain_hash) != 0){
            ERR_RET(err, 0, "Domain hash differ. asnets: %s, loaded: %s",
                    domain_hash, o.domain_hash);
        }

        return 1;
    }

    int create(pddl_sqlite3 *db, pddl_err_t *err)
    {
        char *errmsg = NULL;
        int ret = pddl_sqlite3_exec(db, sql_create_info, NULL, NULL, &errmsg);
        if (ret != SQLITE_OK){
            ERR(err, "Sqlite Error: %s", errmsg);
            pddl_sqlite3_free(errmsg);
            return -1;
        }
        return 0;
    }

    int _sqlInsertInfo(pddl_sqlite3 *db,
                       const char *param,
                       int int_val,
                       float float_val,
                       const char *str_val,
                       pddl_err_t *err)
    {
        char *query = ALLOC_ARR(char, 1024 * 1024);
        int query_size = 0;
        query_size = sprintf(query, "INSERT INTO asnets_info (parameter");
        if (str_val != NULL){
            query_size += sprintf(query + query_size, ",str_value)");
            query_size += sprintf(query + query_size, " VALUES('%s'", param);
            query_size += sprintf(query + query_size, ",'%s');", str_val);

        }else if (float_val > -FLT_MAX){
            query_size += sprintf(query + query_size, ",flt_value)");
            query_size += sprintf(query + query_size, " VALUES('%s'", param);
            query_size += sprintf(query + query_size, ",%f);", float_val);

        }else{
            query_size += sprintf(query + query_size, ",int_value)");
            query_size += sprintf(query + query_size, " VALUES('%s'", param);
            query_size += sprintf(query + query_size, ",%d);", int_val);
        }

        char *errmsg = NULL;
        int ret = pddl_sqlite3_exec(db, query, NULL, NULL, &errmsg);
        FREE(query);
        if (ret != SQLITE_OK){
            ERR(err, "Sqlite Error: %s", errmsg);
            pddl_sqlite3_free(errmsg);
            return -1;
        }
        return 0;
    }

#define SQL_INS_INFO_STR(P, V) \
    _sqlInsertInfo(db, P, INT_MIN, -FLT_MAX, V, err)
#define SQL_INS_INFO_INT(P, V) \
    _sqlInsertInfo(db, P, V, -FLT_MAX, NULL, err)
#define SQL_INS_INFO_FLT(P, V) \
    _sqlInsertInfo(db, P, INT_MIN, V, NULL, err)

    int save(pddl_sqlite3 *db, pddl_err_t *err)
    {
        char *errmsg = NULL;
        if (SQL_INS_INFO_STR("cpddl_version", pddl_version) != 0
                || SQL_INS_INFO_STR("domain_name", domain_name) != 0
                || SQL_INS_INFO_STR("domain_pddl", domain_pddl) != 0
                || SQL_INS_INFO_STR("domain_hash", domain_hash) != 0

                || SQL_INS_INFO_INT("epoch", train_stats.epoch) != 0
                || SQL_INS_INFO_INT("num_samples", train_stats.num_samples) != 0
                || SQL_INS_INFO_FLT("overall_loss", train_stats.overall_loss) != 0
                || SQL_INS_INFO_FLT("success_rate", train_stats.success_rate) != 0

                || SQL_INS_INFO_INT("cfg_hidden_dimension", cfg.hidden_dimension) != 0
                || SQL_INS_INFO_INT("cfg_num_layers", cfg.num_layers) != 0
                || SQL_INS_INFO_INT("cfg_random_seed", cfg.random_seed) != 0
                || SQL_INS_INFO_FLT("cfg_weight_decay", cfg.weight_decay) != 0
                || SQL_INS_INFO_FLT("cfg_dropout_rate", cfg.dropout_rate) != 0
                || SQL_INS_INFO_INT("cfg_batch_size", cfg.batch_size) != 0
                || SQL_INS_INFO_INT("cfg_double_batch_size_every_epoch",
                                    cfg.double_batch_size_every_epoch) != 0
                || SQL_INS_INFO_INT("cfg_max_train_epochs", cfg.max_train_epochs) != 0
                || SQL_INS_INFO_INT("cfg_train_steps", cfg.train_steps) != 0
                || SQL_INS_INFO_INT("cfg_policy_rollout_limit", cfg.policy_rollout_limit) != 0
                || SQL_INS_INFO_FLT("cfg_teacher_timeout", cfg.teacher_timeout) != 0
                || SQL_INS_INFO_FLT("cfg_early_termination_success_rate",
                                    cfg.early_termination_success_rate) != 0
                || SQL_INS_INFO_INT("cfg_early_termination_epochs",
                                    cfg.early_termination_epochs) != 0){
            pddl_sqlite3_free(errmsg);
            TRACE_RET(err, -1);
        }
        return 0;
    }


    int _sqlSelectInfo(pddl_sqlite3 *db,
                       pddl_sqlite3_stmt *stmt,
                       const char *param,
                       int *int_val,
                       float *flt_val,
                       char *str_val,
                       pddl_err_t *err)
    {
        pddl_sqlite3_reset(stmt);
        int ret = pddl_sqlite3_bind_text(stmt, 1, param, -1, SQLITE_STATIC);
        if (ret != SQLITE_OK){
            ERR_RET(err, -1, "Sqlite Error: %s: %s",
                    pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
        }

        int found = (ret = pddl_sqlite3_step(stmt)) == SQLITE_ROW;
        if (ret != SQLITE_ROW && ret != SQLITE_DONE){
            ERR_RET(err, -1, "Sqlite Error: %s: %s",
                    pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
        }
        if (!found)
            ERR_RET(err, -1, "Parameter %s not found.", param);

        if (int_val != NULL)
            *int_val = pddl_sqlite3_column_int(stmt, 0);
        if (flt_val != NULL)
            *flt_val = pddl_sqlite3_column_double(stmt, 1);
        if (str_val != NULL){
            const unsigned char *v = pddl_sqlite3_column_text(stmt, 2);
            strcpy(str_val, (const char *)v);
        }
        return 0;
    }

    int _sqlSelectInfoInt(pddl_sqlite3 *db,
                          pddl_sqlite3_stmt *stmt,
                          const char *param,
                          pddl_err_t *err)
    {
        int val;
        int ret = _sqlSelectInfo(db, stmt, param, &val, NULL, NULL, err);
        if (ret < 0)
            TRACE_RET(err, INT_MIN);
        return val;

    }

    float _sqlSelectInfoFlt(pddl_sqlite3 *db,
                            pddl_sqlite3_stmt *stmt,
                            const char *param,
                            pddl_err_t *err)
    {
        float val;
        int ret = _sqlSelectInfo(db, stmt, param, NULL, &val, NULL, err);
        if (ret < 0)
            TRACE_RET(err, -FLT_MAX);
        return val;

    }

    int _sqlSelectInfoStr(pddl_sqlite3 *db,
                          pddl_sqlite3_stmt *stmt,
                          const char *param,
                          char *val,
                          pddl_err_t *err)
    {
        int ret = _sqlSelectInfo(db, stmt, param, NULL, NULL, val, err);
        if (ret < 0)
            TRACE_RET(err, -1);
        return 0;
    }

#define SQL_INFO_CFG_INT(N) \
    do { \
        cfg.N = _sqlSelectInfoInt(db, stmt, "cfg_" #N, err); \
        if (cfg.N == INT_MIN){ \
            TRACE_RET(err, -1); \
        }else{ \
            LOG(err, "cfg." #N " = %d", cfg.N); \
        } \
    } while (0)

#define SQL_INFO_CFG_FLT(N) \
    do { \
        cfg.N = _sqlSelectInfoFlt(db, stmt, "cfg_" #N, err); \
        if (cfg.N == -FLT_MAX){ \
            TRACE_RET(err, -1); \
        }else{ \
            LOG(err, "cfg." #N " = %f", cfg.N); \
        } \
    } while (0)

    int load(pddl_sqlite3 *db, pddl_err_t *err)
    {
        pddl_sqlite3_stmt *stmt;
        int ret = pddl_sqlite3_prepare_v2(db, sql_query_info, -1, &stmt, NULL);
        if (ret != SQLITE_OK){
            ERR_RET(err, -1, "Sqlite Error: %s: %s",
                    pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
        }

        if (_sqlSelectInfoStr(db, stmt, "cpddl_version", cpddl_version, err) != 0)
            TRACE_RET(err, -1);
        LOG(err, "cpddl version = %s", cpddl_version);
        if (_sqlSelectInfoStr(db, stmt, "domain_name", domain_name, err) != 0)
            TRACE_RET(err, -1);
        LOG(err, "domain name = %s", domain_name);
        if (_sqlSelectInfoStr(db, stmt, "domain_pddl", domain_pddl, err) != 0)
            TRACE_RET(err, -1);
        LOG(err, "domain pddl = %s", domain_pddl);
        if (_sqlSelectInfoStr(db, stmt, "domain_hash", domain_hash, err) != 0)
            TRACE_RET(err, -1);
        LOG(err, "domain hash = %s", domain_hash);

        train_stats.epoch = _sqlSelectInfoInt(db, stmt, "epoch", err);
        if (train_stats.epoch == INT_MIN)
            TRACE_RET(err, -1);
        LOG(err, "train epoch = %d", train_stats.epoch);
        train_stats.num_samples = _sqlSelectInfoInt(db, stmt, "num_samples", err);
        if (train_stats.num_samples == INT_MIN)
            TRACE_RET(err, -1);
        LOG(err, "num samples = %d", train_stats.num_samples);
        train_stats.success_rate = _sqlSelectInfoFlt(db, stmt, "success_rate", err);
        if (train_stats.success_rate == -FLT_MAX)
            TRACE_RET(err, -1);
        LOG(err, "success rate = %f", train_stats.success_rate);
        train_stats.overall_loss = _sqlSelectInfoFlt(db, stmt, "overall_loss", err);
        if (train_stats.overall_loss == -FLT_MAX)
            TRACE_RET(err, -1);
        LOG(err, "overall loss = %f", train_stats.overall_loss);

        SQL_INFO_CFG_INT(hidden_dimension);
        SQL_INFO_CFG_INT(num_layers);
        SQL_INFO_CFG_FLT(weight_decay);
        SQL_INFO_CFG_FLT(dropout_rate);
        SQL_INFO_CFG_INT(random_seed);
        SQL_INFO_CFG_INT(batch_size);
        SQL_INFO_CFG_INT(double_batch_size_every_epoch);
        SQL_INFO_CFG_INT(max_train_epochs);
        SQL_INFO_CFG_INT(train_steps);
        SQL_INFO_CFG_INT(policy_rollout_limit);
        SQL_INFO_CFG_FLT(teacher_timeout);
        SQL_INFO_CFG_FLT(early_termination_success_rate);
        SQL_INFO_CFG_INT(early_termination_epochs);
        return 0;
    }
};



static int sqlInsertWeights(pddl_sqlite3 *db,
                            pddl_sqlite3_stmt *stmt,
                            int id,
                            int layer,
                            int sig,
                            const char *name,
                            int idx,
                            const dynet::Parameter &param,
                            pddl_err_t *err)
{
    pddl_sqlite3_reset(stmt);
    int ret = pddl_sqlite3_bind_int(stmt, 1, id);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    ret = pddl_sqlite3_bind_int(stmt, 2, layer);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    ret = pddl_sqlite3_bind_int(stmt, 3, sig);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    ret = pddl_sqlite3_bind_text(stmt, 4, name, -1, SQLITE_STATIC);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    ret = pddl_sqlite3_bind_int(stmt, 5, idx);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    // Raw weights are not scaled by weight_decay so we need to do that
    // before saving the weights
    const dynet::ParameterStorage &p = param.get_storage();
    float weight_decay = p.owner->get_weight_decay().current_weight_decay();
    std::vector<float> vals = dynet::as_scale_vector(p.values, weight_decay);
    const float *vals_arr = &vals[0];
    size_t size = sizeof(float) * vals.size();
    ret = pddl_sqlite3_bind_blob(stmt, 6, vals_arr, size, SQLITE_STATIC);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    ret = pddl_sqlite3_step(stmt);
    if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    LOG(err, "Weights saved. id: %d, layer: %d, type: %s/%s, name: %s, idx: %d,"
        " array_size: %d",
        id, layer,
        (sig == SIG_ACTION_W || sig == SIG_ACTION_B ? "action" : "proposition"),
        (sig == SIG_ACTION_W || sig == SIG_PROP_W ? "W" : "bias"),
        name, idx, (int)vals.size());

    return 0;
}


static int sqlSelectWeights(pddl_sqlite3 *db,
                            pddl_sqlite3_stmt *stmt,
                            int id,
                            int param_layer,
                            int param_sig,
                            const char *param_name,
                            int param_idx,
                            dynet::Parameter &param,
                            pddl_err_t *err)
{
    pddl_sqlite3_reset(stmt);
    int ret = pddl_sqlite3_bind_int(stmt, 1, id);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    int found = (ret = pddl_sqlite3_step(stmt)) == SQLITE_ROW;
    if (ret != SQLITE_ROW && ret != SQLITE_DONE){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }
    if (!found)
        ERR_RET(err, -1, "Weight %d not found.", id);

    int layer = pddl_sqlite3_column_int(stmt, 0);
    if (layer != param_layer){
        ERR_RET(err, -1, "Layers do not match (stored layer: %d, requested: %d)",
                layer, param_layer);
    }

    int sig = pddl_sqlite3_column_int(stmt, 1);
    if (sig != param_sig)
        ERR_RET(err, -1, "Stored weights don't match");

    const unsigned char *name = pddl_sqlite3_column_text(stmt, 2);
    if (strcmp((const char *)name, param_name) != 0){
        ERR_RET(err, -1, "Stored weights don't match"
                " (stored name: %s, requested: %s)", name, param_name);
    }

    int idx = pddl_sqlite3_column_int(stmt, 3);
    if (idx != param_idx){
        ERR_RET(err, -1, "Stored weights don't match"
                " (stored index: %d, requested: %d)", idx, param_idx);
    }

    int w_size = pddl_sqlite3_column_bytes(stmt, 4) / sizeof(float);
    if (w_size != (int)param.dim().size()){
        ERR_RET(err, -1, "Size of weights don't match"
                " (stored size: %d, requested: %d)",
                w_size, (int)param.dim().size());
    }

    const float *w = (const float *)pddl_sqlite3_column_blob(stmt, 4);
    std::vector<float> warr(w, w + w_size);
    dynet::TensorTools::set_elements(param.get_storage().values, warr);

    return 0;
}

int pddlASNetsConfigInitFromModel(pddl_asnets_config_t *cfg,
                                  const char *fn,
                                  pddl_err_t *err)
{
    // TODO: Refactor with pddlASNetsLoad() and decouple from Info
    pddl_sqlite3 *db;
    int flags = SQLITE_OPEN_READONLY;
    int ret = pddl_sqlite3_open_v2(fn, &db, flags, NULL);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    Info info;
    if (info.load(db, err) != 0){
        pddl_sqlite3_close_v2(db);
        TRACE_RET(err, -1);
    }

    *cfg = info.cfg;

    ret = pddl_sqlite3_close_v2(db);
    if (ret != SQLITE_OK){
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }
    return 0;
}

int pddlASNetsSave(const pddl_asnets_t *a, const char *fn, pddl_err_t *err)
{
    CTX(err, "ASNets-Save");
    LOG(err, "Saving model to %s", fn);
    pddl_sqlite3 *db;
    int flags = SQLITE_OPEN_READWRITE
                    | SQLITE_OPEN_CREATE;
    int ret = pddl_sqlite3_open_v2(fn, &db, flags, NULL);
    if (ret != SQLITE_OK){
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    Info info(a);
    if (info.create(db, err) != 0 || info.save(db, err) != 0){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    char *errmsg = NULL;
    ret = pddl_sqlite3_exec(db, sql_create_weights, NULL, NULL, &errmsg);
    if (ret != SQLITE_OK){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        ERR(err, "Sqlite Error: %s", errmsg);
        pddl_sqlite3_free(errmsg);
        return -1;
    }

    pddl_sqlite3_stmt *stmt;
    ret = pddl_sqlite3_prepare_v2(db, sql_insert_weights, -1, &stmt, NULL);
    if (ret != SQLITE_OK){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    int id = 0;
    for (int layer = 0; layer <= a->params->num_layers; ++layer){
        const std::vector<ActionModule *> &acts = a->params->action[layer];
        for (size_t i = 0; i < acts.size(); ++i){
            ret = sqlInsertWeights(db, stmt, id, layer, SIG_ACTION_W,
                                   a->lifted_task.pddl.action.action[i].name,
                                   i, acts[i]->W, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;

            ret = sqlInsertWeights(db, stmt, id, layer, SIG_ACTION_B,
                                   a->lifted_task.pddl.action.action[i].name,
                                   i, acts[i]->bias, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;
        }
        if (layer == a->params->num_layers)
            break;

        const std::vector<PropositionModule *> &props = a->params->prop[layer];
        for (size_t i = 0; i < props.size(); ++i){
            ret = sqlInsertWeights(db, stmt, id, layer, SIG_PROP_W,
                                   a->lifted_task.pddl.pred.pred[i].name,
                                   i, props[i]->W, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;

            ret = sqlInsertWeights(db, stmt, id, layer, SIG_PROP_B,
                                   a->lifted_task.pddl.pred.pred[i].name,
                                   i, props[i]->bias, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;
        }
    }
    pddl_sqlite3_finalize(stmt);

    ret = pddl_sqlite3_close_v2(db);
    if (ret != SQLITE_OK){
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }
    LOG(err, "Model saved to '%s'", fn);
    CTXEND(err);
    return 0;
}

int pddlASNetsLoad(pddl_asnets_t *a, const char *fn, pddl_err_t *err)
{
    CTX(err, "ASNets-Load");
    LOG(err, "Loading model from %s", fn);
    pddl_sqlite3 *db;
    int flags = SQLITE_OPEN_READONLY;
    int ret = pddl_sqlite3_open_v2(fn, &db, flags, NULL);
    if (ret != SQLITE_OK){
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    Info info;
    if (info.load(db, err) != 0){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    Info info_cur(a);
    if (!info_cur.checkLoadedInfo(info, err)){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        TRACE_RET(err, -1);
    }


    pddl_sqlite3_stmt *w_stmt;
    ret = pddl_sqlite3_prepare_v2(db, sql_query_weights, -1, &w_stmt, NULL);
    if (ret != SQLITE_OK){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    int id = 0;
    for (int layer = 0; layer <= a->params->num_layers; ++layer){
        std::vector<ActionModule *> &acts = a->params->action[layer];
        for (size_t i = 0; i < acts.size(); ++i){
            ret = sqlSelectWeights(db, w_stmt, id, layer, SIG_ACTION_W,
                                   a->lifted_task.pddl.action.action[i].name,
                                   i, acts[i]->W, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;

            ret = sqlSelectWeights(db, w_stmt, id, layer, SIG_ACTION_B,
                                   a->lifted_task.pddl.action.action[i].name,
                                   i, acts[i]->bias, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;
        }
        if (layer == a->params->num_layers)
            break;

        std::vector<PropositionModule *> &props = a->params->prop[layer];
        for (size_t i = 0; i < props.size(); ++i){
            ret = sqlSelectWeights(db, w_stmt, id, layer, SIG_PROP_W,
                                   a->lifted_task.pddl.pred.pred[i].name,
                                   i, props[i]->W, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;

            ret = sqlSelectWeights(db, w_stmt, id, layer, SIG_PROP_B,
                                   a->lifted_task.pddl.pred.pred[i].name,
                                   i, props[i]->bias, err);
            if (ret != 0){
                pddl_sqlite3_close_v2(db);
                CTXEND(err);
                TRACE_RET(err, -1);
            }
            ++id;
        }
    }


    pddl_sqlite3_finalize(w_stmt);

    ret = pddl_sqlite3_close_v2(db);
    if (ret != SQLITE_OK){
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }
    CTXEND(err);
    return 0;
}

int pddlASNetsPrintModelInfo(const char *fn, pddl_err_t *err)
{
    CTX(err, "ASNets-Info");
    LOG(err, "Loading model from %s", fn);
    pddl_sqlite3 *db;
    int flags = SQLITE_OPEN_READONLY;
    int ret = pddl_sqlite3_open_v2(fn, &db, flags, NULL);
    if (ret != SQLITE_OK){
        CTXEND(err);
        ERR_RET(err, -1, "Sqlite Error: %s: %s",
                pddl_sqlite3_errstr(ret), pddl_sqlite3_errmsg(db));
    }

    Info info;
    if (info.load(db, err) != 0){
        pddl_sqlite3_close_v2(db);
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    CTXEND(err);
    return 0;
}

int pddlASNetsNumGroundTasks(const pddl_asnets_t *a)
{
    return a->ground_task_size;
}

const pddl_asnets_ground_task_t *
pddlASNetsGetGroundTask(const pddl_asnets_t *a, int id)
{
    if (id < 0 || id >= a->ground_task_size)
        return NULL;
    return a->ground_task + id;
}

int pddlASNetsRunPolicy(pddl_asnets_t *a,
                        const pddl_asnets_ground_task_t *task,
                        const int *in_state,
                        int *out_state)
{
    return runPolicy(task, *a->params, *a->cg, in_state, out_state, NULL);
}

int pddlASNetsPolicyDistribution(pddl_asnets_t *a,
                                 const pddl_asnets_ground_task_t *task,
                                 const int *in_state,
                                 pddl_asnets_policy_distribution_t *distr)
{
    runPolicy(task, *a->params, *a->cg, in_state, NULL, distr);
    return 0;
}

int pddlASNetsSolveTask(pddl_asnets_t *a,
                        const pddl_asnets_ground_task_t *task,
                        pddl_iarr_t *trace,
                        pddl_asnets_softgoals_result_t *softgoals_result,
                        pddl_err_t *err)
{
    pddl_fdr_state_pool_t states;
    pddlFDRStatePoolInit(&states, &task->fdr.var, NULL);
    int ret = policyRollout(a, task, &states, trace, softgoals_result, err);
    pddlFDRStatePoolFree(&states);
    return ret;
}

static dynet::Expression asnetsTrainExpr(pddl_asnets_t *a,
                                         pddl_asnets_train_data_t *data,
                                         int minibatch_size,
                                         dynet::ComputationGraph &cg)
{
    cg.clear();

    // Sample a minibatch
    ASNetsTrainMiniBatch batch(a, data, minibatch_size);
    batch.createInputs(cg);

    // Construct network for all relevant ground tasks at once
    std::vector<dynet::Expression> nets;
    int batch_size = 0;
    for (int task_id = 0; task_id < a->ground_task_size; ++task_id){
        if (batch.batch[task_id].size == 0)
            continue;
        const ASNetsTrainMiniBatchTask &b = batch.batch[task_id];
        //LOG(err, "Batch: task: %d, size: %d", task_id, b.size);
        dynet::Expression e = asnetsExpr(a->ground_task + task_id,
                                         *a->params,
                                         cg,
                                         b.e_state,
                                         b.e_goal,
                                         b.e_applicable_ops,
                                         a->cfg.dropout_rate);
        dynet::Expression e_loss = crossEntropyLoss(cg, e, b.e_output);
        nets.push_back(e_loss);
        batch_size += b.size;
    }

    ASSERT(nets.size() > 0);
    // Compute mean over all losses
    dynet::Expression e_loss = dynet::sum(nets) / batch_size;
    return e_loss;
}


static int trainStep(pddl_asnets_t *a,
                     int epoch,
                     int train_step,
                     pddl_asnets_train_data_t *data,
                     pddl_err_t *err)
{
    a->train_stats.train_step = train_step + 1;

    // Sample a minibatch
    pddlASNetsTrainDataShuffle(data);

    // Construct network with the right input data
    dynet::Expression e_loss = asnetsTrainExpr(a, data, a->cfg.batch_size, *a->cg);
    // TODO: L2 regularization -- is it done automatically by dynet?

    // Learn parameters
    float loss_val = dynet::as_scalar(a->cg->forward(e_loss));
    a->cg->backward(e_loss);
    a->trainer->update();

    LOG(err, "epoch %d/%d, step: %d/%d, loss: %.3f, succ: %.2f, samples: %d,"
        " succ epochs: %d"
        " | minibatch loss: %f, size: %d",
        a->train_stats.epoch, a->train_stats.max_epochs,
        a->train_stats.train_step, a->train_stats.max_train_steps,
        a->train_stats.overall_loss, a->train_stats.success_rate,
        a->train_stats.num_samples,
        a->train_stats.consecutive_successful_epochs,
        loss_val, a->cfg.batch_size);

    return 0;
}

static int trainExploration(pddl_asnets_t *a,
                            int epoch,
                            int ground_task_id,
                            pddl_asnets_train_data_t *data,
                            pddl_err_t *err)
{
    const pddl_asnets_ground_task_t *task = a->ground_task + ground_task_id;
    CTX(err, "Exploration Phase");

    pddl_fdr_state_pool_t states;
    pddlFDRStatePoolInit(&states, &task->fdr.var, err);

    // Collect states from the policy rollout
    // trace and softgoals_result set to NULL as they are not needed here
    int reached_goal = policyRollout(a, task, &states, NULL, NULL, err);
    LOG(err, "Policy rollout: %d states,"
        " reached goal: %d",
        states.num_states, reached_goal);

    // TODO: Here we can add also states from random walks.
    //       Maybe for the for the first epoch?

    // Extend training data with teacher rollouts
    int *state = ALLOC_ARR(int, task->fdr.var.var_size);
    for (pddl_state_id_t state_id = 0; state_id < states.num_states; ++state_id){
        pddlFDRStatePoolGet(&states, state_id, state);
        int ret;

        switch (a->cfg.teacher){
            case PDDL_ASNETS_TEACHER_ASTAR_LMCUT:
                ret = pddlASNetsTrainDataRolloutAStarLMCut(data, ground_task_id,
                                                           state, &task->fdr,
                                                           a->cfg.teacher_timeout,
                                                           err);
                break;
            case PDDL_ASNETS_TEACHER_EXTERNAL_FAST_DOWNWARD:
                PANIC_IF(a->cfg.teacher_external_cmd == NULL,
                         "External command is not specified.");
                ret = pddlASNetsTrainDataRolloutExternalFastDownward(data, ground_task_id,
                                                                     state, &task->fdr,
                                                                     a->cfg.teacher_external_cmd,
                                                                     a->cfg.teacher_timeout,
                                                                     err);
                break;
            case PDDL_ASNETS_TEACHER_FAST_DOWNWARD:
                // if OSP problem with initial state, then save MSGS value achieved by teacher planner
                int save_msgs = 0;
                if (a->cfg.is_osp_problem && state_id == 0) {
                    save_msgs = 1;
                }
                ret = pddlASNetsTrainDataRolloutFastDownward(data, ground_task_id,
                                                             state, &task->fdr,
                                                             a->cfg.is_osp_problem,
                                                             save_msgs,
                                                             a->cfg.fd_config,
                                                             a->cfg.teacher_timeout,
                                                             err);
                break;
        }

        if (ret < 0){
            FREE(state);
            pddlFDRStatePoolFree(&states);
            CTXEND(err);
            TRACE_RET(err, -1);
        }
    }
    FREE(state);

    pddlFDRStatePoolFree(&states);
    CTXEND(err);
    return 0;
}

static float overallLoss(pddl_asnets_t *a,
                         pddl_asnets_train_data_t *data)
{
    dynet::Expression e_loss = asnetsTrainExpr(a, data, -1, *a->cg);
    float loss = dynet::as_scalar(a->cg->forward(e_loss));
    return loss;
}

static float successRate(pddl_asnets_t *a, pddl_asnets_train_data_t *td, pddl_err_t *err)
{
    int num_solved = 0;
    int num_msgs_unknown = 0; // used for osp tasks
    for (int task_id = 0; task_id < a->ground_task_size; ++task_id)
    {
        const pddl_asnets_ground_task_t *task = a->ground_task + task_id;
        pddl_fdr_state_pool_t states;
        pddlFDRStatePoolInit(&states, &task->fdr.var, NULL);
        if (a->cfg.is_osp_problem)
        {   
            pddl_asnets_softgoals_result_t softgoals_result = PDDL_ASNETS_SOFTGOALS_RESULT_INIT;
            policyRollout(a, task, &states, NULL, &softgoals_result, err);
            int max_msgs_teacher = pddlASNetsTrainDataMSGSGet(td, task_id);
            if (max_msgs_teacher < 0) {
                // MSGS unknown
                num_msgs_unknown += 1;
            }
            else if (softgoals_result.max_softgoals_achieved >= max_msgs_teacher) { // compare with msgs size from FD
               num_solved += 1; 
            }
        }
        else
        {
            if (policyRollout(a, task, &states, NULL, NULL, err))
                num_solved += 1;
        }
        pddlFDRStatePoolFree(&states);
    }
    if (a->ground_task_size == num_msgs_unknown) { // handle case msgs unknown for all tasks to avoid divide by zero
        return 0.f;
    }
    return num_solved / (float)(a->ground_task_size - num_msgs_unknown);
}

static int trainEpoch(pddl_asnets_t *a,
                      int epoch,
                      pddl_asnets_train_data_t *data,
                      pddl_err_t *err)
{
    LOG(err, "epoch: %d/%d", epoch, a->cfg.max_train_epochs);
    a->train_stats.epoch = epoch + 1;

    // Exploration phase
    for (int ground_task = 0; ground_task < a->ground_task_size; ++ground_task){
        int ret;
        if ((ret = trainExploration(a, epoch, ground_task, data, err)) != 0){
            if (ret < 0)
                TRACE_RET(err, ret);
            return ret;
        }
    }
    a->train_stats.num_samples = data->sample_size;

    // Training phase
    int num_steps = a->cfg.train_steps;
    //num_steps = PDDL_MIN(num_steps, data->sample_size / a->cfg.batch_size);
    //num_steps = PDDL_MAX(num_steps, 1);
    LOG(err, "num training steps: %d", num_steps);
    for (int train_step = 0; train_step < num_steps; ++train_step){
        int ret;
        if ((ret = trainStep(a, epoch, train_step, data, err)) != 0){
            if (ret < 0)
                TRACE_RET(err, ret);
            return ret;
        }
    }

    CTX(err, "Success Rate");
    a->train_stats.success_rate = successRate(a, data, err);
    LOG(err, "Success rate: %f", a->train_stats.success_rate);
    CTXEND(err);
    CTX(err, "Overall Loss");
    a->train_stats.overall_loss = overallLoss(a, data);
    LOG(err, "Overall loss: %f", a->train_stats.overall_loss);
    CTXEND(err);
    LOG(err, "Train samples: %d", a->train_stats.num_samples);
    LOG(err, "epoch %d/%d, step: %d/%d, loss: %.3f, succ: %.2f, samples: %d,"
        " succ epochs: %d",
        a->train_stats.epoch, a->train_stats.max_epochs,
        a->train_stats.train_step, a->train_stats.max_train_steps,
        a->train_stats.overall_loss, a->train_stats.success_rate,
        a->train_stats.num_samples,
        a->train_stats.consecutive_successful_epochs);
    return 0;
}

int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err)
{
    CTX(err, "ASNets-Train");
    pddl_asnets_train_data_t data;
    pddlASNetsTrainDataInit(&data);
    if (a->cfg.is_osp_problem){ // for OSP problems, initialize the array of MSGS values
        pddlASNetsTrainDataMSGSInit(&data, a->ground_task_size);
    }

    a->train_stats.success_rate = successRate(a, &data, err);

    for (int epoch = 0; epoch < a->cfg.max_train_epochs; ++epoch){
        if (a->cfg.double_batch_size_every_epoch > 0
                && epoch > 0
                && epoch % a->cfg.double_batch_size_every_epoch == 0){
            a->cfg.batch_size *= 2;
        }

        int ret;
        if ((ret = trainEpoch(a, epoch, &data, err)) != 0){
            pddlASNetsTrainDataFree(&data);
            CTXEND(err);
            if (ret < 0)
                TRACE_RET(err, ret);
            return ret;
        }

        if (a->cfg.save_model_prefix != NULL){
            char fn[4096];
            sprintf(fn, "%s-%05d-%.2f-%.03f.policy",
                    a->cfg.save_model_prefix,
                    epoch,
                    a->train_stats.success_rate,
                    a->train_stats.overall_loss);
            LOG(err, "Saving model to %s (epoch: %d, success rate: %.2f, loss: %.3f)",
                fn, epoch, a->train_stats.success_rate, a->train_stats.overall_loss);
            pddlASNetsSave(a, fn, err);
        }
        if (a->train_stats.success_rate >= a->cfg.early_termination_success_rate){
            a->train_stats.consecutive_successful_epochs += 1;
        }else{
            a->train_stats.consecutive_successful_epochs = 0;
        }

        LOG(err, "Consecutive successful epochs: %d",
            a->train_stats.consecutive_successful_epochs);
        if (a->train_stats.consecutive_successful_epochs
                >= a->cfg.early_termination_epochs){
            LOG(err, "Reached %d/%d consecutive successful epochs.",
                a->train_stats.consecutive_successful_epochs,
                a->cfg.early_termination_epochs);
            LOG(err, "Terminating training.");
            break;
        }
    }
    LOG(err, "epoch %d/%d, step: %d/%d, loss: %.3f, succ: %.2f, samples: %d,"
        " succ epochs: %d",
        a->train_stats.epoch, a->train_stats.max_epochs,
        a->train_stats.train_step, a->train_stats.max_train_steps,
        a->train_stats.overall_loss, a->train_stats.success_rate,
        a->train_stats.num_samples,
        a->train_stats.consecutive_successful_epochs);
    pddlASNetsTrainDataFree(&data);
    CTXEND(err);
    return 0;
}

void pddlASNetsEvaluate(pddl_asnets_t *a, int write_plans, pddl_err_t *err)
{
    int num_solved = 0;
    int num_tasks = pddlASNetsNumGroundTasks(a);
    for (int task_id = 0; task_id < num_tasks; ++task_id)
    {
        const pddl_asnets_ground_task_t *task;
        task = pddlASNetsGetGroundTask(a, task_id);
        PDDL_IARR(plan);
        int solved = pddlASNetsSolveTask(a, task, &plan, NULL, err);
        LOG(err, "Task %s %s"
            " solved: %s, length: %d",
            task->pddl.domain_file,
            task->pddl.problem_file,
            F_BOOL(solved),
            (solved ? pddlIArrSize(&plan) : -1));
        if (solved){
            ++num_solved;
            if (write_plans){
                char fn[512];
                snprintf(fn, 511, "%s--%s.plan", task->pddl.domain_name,
                         task->pddl.problem_name);
                FILE *fout = fopen(fn, "w");
                if (fout != NULL){
                    int op_id;
                    PDDL_IARR_FOR_EACH(&plan, op_id){
                        fprintf(fout, "(%s)\n", task->fdr.op.op[op_id]->name);
                    }
                    fclose(fout);
                }else{
                    LOG(err, "Could not open file %s", fn);
                }
            }
        }
        pddlIArrFree(&plan);
    }
    LOG(err, "Solved %d out of %d tasks", num_solved, num_tasks);
}

void pddlASNetsEvaluateOSP(pddl_asnets_t *a, int write_plans, int benchmark_trainer, pddl_err_t *err)
{
    int num_allgoals_solved = 0; // TO-DO: as of now, all goals are soft goals in OSP
                                 // adapt as required when extending to both hard goals and soft goals
    int total_softgoals = 0; // total softgoals over all tasks
    int total_achieved_softgoals = 0; // total softgoals achieved by policy over all tasks
    int total_benchmark_msgs = 0; // total softgoals achieved by trainer planner over all tasks
    int num_benchmark_tle = 0; // number of tasks where trainer planner timed out
    int total_achieved_softgoals_not_tle = 0; // total softgoals achieved by policy over tasks where trainer planner did not time out
    int num_achieved_msgs = 0; // number of tasks where policy reached benchamrk msgs

    int num_tasks = pddlASNetsNumGroundTasks(a);
    for (int task_id = 0; task_id < num_tasks; ++task_id)
    {
        const pddl_asnets_ground_task_t *task;
        task = pddlASNetsGetGroundTask(a, task_id);
        PDDL_IARR(plan);
        pddl_asnets_softgoals_result_t achieved_softgoals_result = PDDL_ASNETS_SOFTGOALS_RESULT_INIT;
        int allgoals_solved = pddlASNetsSolveTask(a, task, &plan, &achieved_softgoals_result, err); // TO-DO: as of now, all goals are soft goals in OSP
                                                                                                    // adapt as required when extending to both hard goals and soft goals
        if (write_plans)
        {
            char fn[512];
            // snprintf(fn, 511, "%s--%s.plan", task->pddl.domain_name, task->pddl.problem_name);
            snprintf(fn, 511, "%s--%s--p%d.plan", task->pddl.domain_name, task->pddl.problem_name, task_id);
            FILE *fout = fopen(fn, "w");
            if (fout != NULL)
            {
                fprintf(fout, "total softgoals: %d\n", achieved_softgoals_result.total_softgoals);
                fprintf(fout, "max softgoals achieved: %d\n", achieved_softgoals_result.max_softgoals_achieved);
                fprintf(fout, "max softgoals achieved in number of policy steps: %d\n", achieved_softgoals_result.max_softgoals_plan_steps);
                int op_id;
                for (int index = 0; index < achieved_softgoals_result.max_softgoals_plan_steps; index++)
                {
                    op_id = pddlIArrGet(&plan, index);
                    fprintf(fout, "(%s)\n", task->fdr.op.op[op_id]->name);
                }
                fclose(fout);
            }
            else
            {
                PDDL_LOG(err, "Could not open file %s", fn);
            }
        }
        pddlIArrFree(&plan);

        // compute aggregate metrics and benchmarks
        if (allgoals_solved)
        {
            ++num_allgoals_solved;
        }
        total_softgoals += achieved_softgoals_result.total_softgoals; 
        total_achieved_softgoals += achieved_softgoals_result.max_softgoals_achieved;
        if(benchmark_trainer)
        {
            pddl_asnets_softgoals_result_t msgs_result = PDDL_ASNETS_SOFTGOALS_RESULT_INIT;
            if (pddlASNetsBenchmarkTrainer(&a->cfg, task->pddl.domain_file, task->pddl.problem_file, &msgs_result, err) == 0)
            {   
                if (msgs_result.max_softgoals_achieved == -2) {
                    msgs_result.max_softgoals_achieved = achieved_softgoals_result.total_softgoals;
                }
                if (msgs_result.max_softgoals_achieved >= 0) {
                    total_benchmark_msgs += msgs_result.max_softgoals_achieved;
                    total_achieved_softgoals_not_tle += achieved_softgoals_result.max_softgoals_achieved;
                    if (achieved_softgoals_result.max_softgoals_achieved >= msgs_result.max_softgoals_achieved)
                        ++num_achieved_msgs;
                    
                    PDDL_LOG(err, "Task %s %s, Benchmark results - \n"
                                  " max softgoals solved: %d,\n steps taken for max softgoals: %d\n",
                             task->pddl.domain_file,
                             task->pddl.problem_file,
                             msgs_result.max_softgoals_achieved,
                             msgs_result.max_softgoals_plan_steps);
                }
                else { // case timed out or unknown error
                    ++num_benchmark_tle;
                    PDDL_LOG(err, "Task %s %s, Benchmark results - Time Limit Exceeded\n",
                             task->pddl.domain_file,
                             task->pddl.problem_file);
                }
            }
        } 
        PDDL_LOG(err, "Task %s %s, Policy result - \n"
                      " all goals solved: %s,\n total softgoals: %d,\n max softgoals solved: %d,\n steps taken for max softgoals: %d,\n total policy steps: %d\n",
                 task->pddl.domain_file,
                 task->pddl.problem_file,
                 F_BOOL(allgoals_solved),
                 achieved_softgoals_result.total_softgoals,
                 achieved_softgoals_result.max_softgoals_achieved,
                 achieved_softgoals_result.max_softgoals_plan_steps,
                 achieved_softgoals_result.total_policy_steps);
    }
    PDDL_LOG(err, "Aggregate Results: ");
    PDDL_LOG(err, "Solved all goals for %d out of %d tasks.",
             num_allgoals_solved, num_tasks);
    PDDL_LOG(err, "Achieved a total of %d soft goals out of %d soft goals over all tasks.",
             total_achieved_softgoals, total_softgoals);
    PDDL_LOG(err, "Fraction of soft goals achieved over total soft goals: %.3f",
             total_achieved_softgoals/ (float) total_softgoals);
    if (benchmark_trainer) {
        PDDL_LOG(err, "Total Benchamrk MSGS: %d", total_benchmark_msgs);
        if (total_benchmark_msgs > 0) {
            PDDL_LOG(err, "Fraction of soft goals achieved over successful benchmark MSGS: %.3f",
                     total_achieved_softgoals_not_tle/ (float) total_benchmark_msgs);
            PDDL_LOG(err, "Number of tasks that achieved MSGS: %d", num_achieved_msgs);
            PDDL_LOG(err, "Fraction of soft goals achieved over benchmark MSGS incl. time limit exceeded: %.3f",
                     total_achieved_softgoals/ (float) total_benchmark_msgs);
            PDDL_LOG(err, "Number of benchmark time limit exceeded: %d", num_benchmark_tle);
        }
    }
}

int pddlASNetsBenchmarkTrainer(pddl_asnets_config_t* a_config, char* domain_filename, char* problem_filename, pddl_asnets_softgoals_result_t *msgs_result, pddl_err_t *err){
 
    if (a_config->teacher == PDDL_ASNETS_TEACHER_ASTAR_LMCUT) {
         /* TO-DO: call cpddl search and save results  */
        return -1;
    }
    else if (a_config->teacher == PDDL_ASNETS_TEACHER_FAST_DOWNWARD) {
        // execute fast-downward with domain and problem pddl files
        // save results to msgs_result
        if (msgs_result == NULL) {
            /* TO-DO: handle this case later*/
            return -1;
        }
        char *search_arg = PDDL_STRDUP("astar(lmcut())");
        if (a_config->is_osp_problem)
        {
            search_arg = PDDL_STRDUP("osp_dfs(u_eval=mugs_hmax(all_softgoals=true))");
        }
        char *argv[] = {
            a_config->fd_config->fd_interpreter,
            a_config->fd_config->fd_executable_path,
            PDDL_STRDUP("--build"),
            PDDL_STRDUP("release64"),
            domain_filename,
            problem_filename,
            PDDL_STRDUP("--search"),
            search_arg, 
            NULL
        };
        pddl_exec_status_t status;
        char *solbuf = NULL;
        int solbuf_size = 0;
        int execret = pddlExecvpLimits(argv, &status, NULL, 0,
                                    &solbuf, &solbuf_size, NULL, NULL, a_config->teacher_timeout, -1, err);
        PANIC_IF(execret != 0, "Fast Downward subprocess failed.");
        if (status.exited == 1)
        {
            // use exit_status_code to identify if plan found, plan not found or search timed out internally.
            switch (status.exit_status)
            { 
            case 0: // case SUCCESS:
            case 1: // case SEARCH_PLAN_FOUND_AND_OUT_OF_MEMORY
            case 2: // case SEARCH_PLAN_FOUND_AND_OUT_OF_TIME
            case 3: // case SEARCH_PLAN_FOUND_AND_OUT_OF_MEMORY_AND_TIME
                // capture msgs value form solbuf 
                {   
                    char *str = NULL;
                    str = strstr(solbuf, "#solved goals:");
                    if (str != NULL)
                        msgs_result->max_softgoals_achieved = strtol(str + 15, NULL, 10);
                    else 
                        msgs_result->max_softgoals_achieved = -2; // hack to handle case where all softgoals achieved
                    str = strstr(solbuf, "Plan length:");
                    if (str != NULL)
                        msgs_result->max_softgoals_plan_steps = strtol(str + 13, NULL, 10);
                }
                break;

            case 11: // case SEARCH_UNSOLVABLE:
            case 12: // case SEARCH_UNSOLVABLE_INCOMPLETE:
                msgs_result->max_softgoals_achieved = 0;
                msgs_result->max_softgoals_plan_steps = 0;
                break;

            case 23: // case SEARCH_OUT_OF_TIME:
            case 24: // case SEARCH_OUT_OF_MEMORY_AND_TIME:
                msgs_result->max_softgoals_achieved = -1;
                msgs_result->max_softgoals_plan_steps = -1;
                break;

            default:
                LOG(err, "unexpected search exit code from fast downward");
                msgs_result->max_softgoals_achieved = -1;
                msgs_result->max_softgoals_plan_steps = -1;
                break;
            }
        }
        // check if subprocess killed because of TLE 
        else if (status.signaled == 1) {
            msgs_result->max_softgoals_achieved = -1;
            msgs_result->max_softgoals_plan_steps = -1;
        }
        // TO-DO: should solbuf be FREEed or not? Is it heap memory or not?
        // if(solbuf != NULL)
        //     FREE(solbuf);
        // TO-DO: should search_arg be FREEed?
        // if (search_arg != NULL)
        //     FREE(search_arg);
    }
    return 0;
}
