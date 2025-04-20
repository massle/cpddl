#include "internal.h"

#include <pddl/asnets_ground_model.h>
#include <stdio.h>

pddl_nn_layer_feed_forward_t*
pddlNNLayerFFNew(int input, int output)
{
    pddl_nn_layer_feed_forward_t* l = ZALLOC(pddl_nn_layer_feed_forward_t);
    l->inputs = input;
    l->outputs = output;
    l->weights = ZALLOC_ARR(float, (input * output));
    l->biases = ZALLOC_ARR(float, output);
    return l;
}

void
pddlNNLayerFFDel(pddl_nn_layer_feed_forward_t* l)
{
    FREE(l->weights);
    FREE(l->biases);
    FREE(l);
}

pddl_nn_layer_max_pool_t*
pddlNNLayerPoolNew(int num_indices, int num_outputs)
{
    pddl_nn_layer_max_pool_t* l = ZALLOC(pddl_nn_layer_max_pool_t);
    l->indices = ZALLOC_ARR(int, num_indices);
    l->inputs = ZALLOC_ARR(int, num_outputs);
    l->outputs = num_outputs;
    return l;
}

void
pddlNNLayerPoolDel(pddl_nn_layer_max_pool_t* l)
{
    FREE(l->indices);
    FREE(l->inputs);
    FREE(l);
}

pddl_ground_asnets_proposition_layer_t*
pddlGroundASNetsPLayerNew()
{
    pddl_ground_asnets_proposition_layer_t* l =
        ZALLOC(pddl_ground_asnets_proposition_layer_t);
    l->perceptron = NULL;
    l->pool = NULL;
    return l;
}

void
pddlGroundASNetsPLayerDel(pddl_ground_asnets_proposition_layer_t* l)
{
    if (l->perceptron != NULL) {
        pddlNNLayerFFDel(l->perceptron);
        l->perceptron = NULL;
    }
    if (l->pool != NULL) {
        pddlNNLayerPoolDel(l->pool);
        l->pool = NULL;
    }
    FREE(l);
}

pddl_ground_asnets_t*
pddlGroundASNetsNew(int layers)
{
    pddl_ground_asnets_t* a = ZALLOC(pddl_ground_asnets_t);
    a->layers = layers;
    a->proposition_layers =
        ZALLOC_ARR(pddl_ground_asnets_proposition_layer_t*, layers);
    a->action_layers = ZALLOC_ARR(pddl_nn_layer_feed_forward_t*, layers + 1);
    return a;
}

void
pddlGroundASNetsDel(pddl_ground_asnets_t* m)
{
    for (int i = m->layers - 1; i >= 0; --i) {
        if (m->proposition_layers[i] != NULL) {
            pddlGroundASNetsPLayerDel(m->proposition_layers[i]);
        }
        if (m->action_layers[i] != NULL) {
            pddlNNLayerFFDel(m->action_layers[i]);
        }
    }
    if (m->action_layers[m->layers] != NULL) {
        pddlNNLayerFFDel(m->action_layers[m->layers]);
    }
    FREE(m->proposition_layers);
    FREE(m->action_layers);
    FREE(m);
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
    for (int o = 0, k = 0; o < m->outputs; ++o, ++k) {
        fprintf(out, "%d\n", m->inputs[o]);
        for (int i = 0; i + 1 < m->inputs[o]; ++i, ++k) {
            fprintf(out, "%d ", m->indices[k]);
        }
        fprintf(out, "%d\n", m->indices[k]);
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

    fprintf(out, "// ASNets dump\n");
    fprintf(out, "%d\n", m->action_layers[0]->inputs);
    fprintf(out, "%d\n", m->layers);
    pddlDumpNNLayerFF(m->action_layers[0], out, err);
    for (int l = 0; l < m->layers; ++l) {
        pddlDumpNNLayerPool(m->proposition_layers[l]->pool, out, err);
        pddlDumpNNLayerFF(m->proposition_layers[l]->perceptron, out, err);
        pddlDumpNNLayerFF(m->action_layers[l + 1], out, err);
    }

    fclose(out);
}
