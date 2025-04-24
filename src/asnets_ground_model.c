#include "internal.h"

#include <pddl/asnets_ground_model.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
pddlNNLayerFFInit(pddl_nn_layer_feed_forward_t* l, int input, int output)
{
    l->inputs = input;
    l->outputs = output;
    l->weights = ZALLOC_ARR(float, (input * output));
    l->biases = ZALLOC_ARR(float, output);
}

void
pddlNNLayerFFFree(pddl_nn_layer_feed_forward_t* l)
{
    FREE(l->weights);
    FREE(l->biases);
}

void
pddlNNLayerPoolInit(
    pddl_nn_layer_max_pool_t* l,
    int num_indices,
    int num_outputs)
{
    l->indices = ZALLOC_ARR(int, num_indices);
    l->inputs = ZALLOC_ARR(int, num_outputs);
    l->outputs = num_outputs;
}

void
pddlNNLayerPoolFree(pddl_nn_layer_max_pool_t* l)
{
    FREE(l->indices);
    FREE(l->inputs);
}

void
pddlGroundASNetsPLayerFree(pddl_ground_asnets_proposition_layer_t* l)
{
    pddlNNLayerFFFree(&l->perceptron);
    pddlNNLayerPoolFree(&l->pool);
}

void
pddlGroundASNetsInit(pddl_ground_asnets_t* a, int layers)
{
    a->layers = layers;
    a->proposition_layers =
        ZALLOC_ARR(pddl_ground_asnets_proposition_layer_t, layers);
    a->action_layers = ZALLOC_ARR(pddl_nn_layer_feed_forward_t, layers + 1);
}

void
pddlGroundASNetsFree(pddl_ground_asnets_t* m)
{
    for (int i = m->layers - 1; i >= 0; --i) {
        pddlGroundASNetsPLayerFree(m->proposition_layers + i);
        pddlNNLayerFFFree(m->action_layers + i);
    }
    pddlNNLayerFFFree(m->action_layers + m->layers);
    FREE(m->proposition_layers);
    FREE(m->action_layers);
}

void
pddlDumpNNLayerFF(
    const pddl_nn_layer_feed_forward_t* m,
    FILE* out,
    pddl_err_t* err)
{
    fprintf(out, "linear\n");
    fprintf(out, "%d\n", m->outputs);
    for (int o = 0, k = 0; o < m->outputs; ++o, ++k) {
        for (int i = 0; i + 1 < m->inputs; ++i, ++k) {
            fprintf(out, "%f ", m->weights[k]);
        }
        fprintf(out, "%f\n", m->weights[k]);
    }
    for (int o = 0; o + 1 < m->outputs; ++o) {
        fprintf(out, "%f ", m->biases[o]);
    }
    fprintf(out, "%f\n", m->biases[m->outputs - 1]);
}

void
pddlDumpNNLayerPool(
    const pddl_nn_layer_max_pool_t* m,
    FILE* out,
    pddl_err_t* err)
{
    fprintf(out, "max-pool\n");
    fprintf(out, "%d\n", m->outputs);
    for (int o = 0, k = 0; o < m->outputs; ++o) {
        fprintf(out, "%d\n", m->inputs[o]);
        if (m->inputs[o] == 0)
            continue;
        for (int i = 0; i + 1 < m->inputs[o]; ++i, ++k) {
            fprintf(out, "%d ", m->indices[k]);
        }
        fprintf(out, "%d\n", m->indices[k]);
        ++k;
    }
}

void
pddlDumpGroundASNetsModel(
    const pddl_ground_asnets_t* m,
    const char* fn,
    pddl_err_t* err)
{
    FILE* out = fopen(fn, "w");
    if (out == NULL) {
        PANIC("Could not open file");
    }

    const char* ACTIVATION = "relu";

    fprintf(out, "%d\n", m->input_interface.l0.inputs);
    fprintf(out, "%d\n", 5 * m->layers + 7);
    pddlDumpNNLayerFF(&m->input_interface.l0, out, err);
    fprintf(out, "%s\n", ACTIVATION);
    pddlDumpNNLayerFF(&m->input_interface.l1, out, err);
    fprintf(out, "%s\n", ACTIVATION);
    pddlDumpNNLayerFF(&m->input_interface.l2, out, err);
    // no activation necessary
    pddlDumpNNLayerFF(m->action_layers, out, err);
    for (int l = 0; l < m->layers; ++l) {
        fprintf(out, "%s\n", ACTIVATION);
        pddlDumpNNLayerPool(&m->proposition_layers[l].pool, out, err);
        pddlDumpNNLayerFF(&m->proposition_layers[l].perceptron, out, err);
        fprintf(out, "%s\n", ACTIVATION);
        pddlDumpNNLayerFF(m->action_layers + l + 1, out, err);
    }
    pddlDumpNNLayerPool(&m->output_interface, out, err);

    fclose(out);
}

void
pddlGroundASNetsConfInit(
    pddl_ground_asnets_conf_t* conf,
    int num_facts,
    int num_operators)
{
    conf->variable = ZALLOC_ARR(int, num_facts);
    conf->value = ZALLOC_ARR(int, num_facts);
    conf->label = ZALLOC_ARR(int, num_operators);
    for (int fact_id = 0; fact_id < num_facts; ++fact_id) {
        conf->variable[fact_id] = -1;
    }
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        conf->label[op_id] = -1;
    }
}

void
pddlGroundASNetsConfLoad(
    pddl_ground_asnets_conf_t* conf,
    const char* fn,
    const pddl_asnets_ground_task_t* task)
{
    FILE* f = fopen(fn, "r");
    if (f == NULL) {
        PANIC("could not open interface specification file");
    }

    int n;
    size_t buffer_size;
    char* buffer = NULL;
    char num[256];
    memset(num, 0, 256);

    fscanf(f, "%d\n", &n);
    conf->num_labels = n;

    ssize_t read = getline(&buffer, &buffer_size, f);
    if (read == -1 || strcmp(buffer, "begin-operators\n")) {
        PANIC("expected begin-operators");
    }
    conf->num_operators = 0;
    while ((read = getline(&buffer, &buffer_size, f)) != -1) {
        if (strcmp(buffer, "end-operators\n") == 0) {
            break;
        }
        buffer[read - 1] = '\0';

        memset(num, 0, 256);
        size_t i = 0;
        for (; i < read && buffer[i] != ' '; num[i] = buffer[i], ++i) { }
        if (i == read) {
            PANIC("invalid operator spec");
        }
        ++i;

        int label = atoi(num);
        int op_id_ = -1;
        for (int op_id = 0; op_id < task->op_size; ++op_id) {
            const pddl_strips_op_t* op = task->strips.op.op[op_id];
            if (strcmp(buffer + i, op->name) == 0) {
                op_id_ = op_id;
                break;
            }
        }
        if (op_id_ < 0) {
            printf("could not find operator with name %s", (buffer + i));
            PANIC("could not find operator");
        }

        conf->label[op_id_] = label;
        ++conf->num_operators;
    }

    fscanf(f, "%d\n", &n);
    conf->num_variables = n;

    read = getline(&buffer, &buffer_size, f);
    if (read == -1 || strcmp(buffer, "begin-variables\n")) {
        PANIC("expected begin-variables");
    }
    conf->num_facts = 0;
    while ((read = getline(&buffer, &buffer_size, f)) != -1) {
        if (strcmp(buffer, "end-variables\n") == 0) {
            break;
        }
        buffer[read - 1] = '\0';

        memset(num, 0, 256);
        size_t i = 0;
        for (; i < read && buffer[i] != ' '; num[i] = buffer[i], ++i) { }
        if (i == read) {
            PANIC("invalid fact spec");
        }
        ++i;
        int var_id = atoi(num);

        memset(num, 0, 256);
        for (int j = 0; i < read && buffer[i] != ' ';
             num[j] = buffer[i], ++i, ++j) { }
        if (i == read) {
            PANIC("invalid fact spec");
        }
        ++i;
        int value = atoi(num);

        int fact_id_ = -1;
        for (int fact_id = 0; fact_id < task->fact_size; ++fact_id) {
            const pddl_fact_t* fact = task->strips.fact.fact[fact_id];
            if (strcmp(buffer + i, fact->name) == 0) {
                fact_id_ = fact_id;
                break;
            }
        }
        if (fact_id_ < 0) {
            printf("could not find fact with name %s", (buffer + i));
            PANIC("could not find fact");
        }

        conf->variable[fact_id_] = var_id;
        conf->value[fact_id_] = value;
        ++conf->num_facts;
    }

    free(buffer);
    fclose(f);
}

void
pddlGroundASNetsConfFree(pddl_ground_asnets_conf_t* conf)
{
    FREE(conf->variable);
    FREE(conf->value);
    FREE(conf->label);
    // FREE(conf);
}
