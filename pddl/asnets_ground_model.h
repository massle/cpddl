#ifndef _PDDL__ASNETS_GROUND_MODEL_H_
#define _PDDL__ASNETS_GROUND_MODEL_H_

#include <pddl/err.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
    float* weights;
    float* biases;
    int inputs;
    int outputs;
} pddl_nn_layer_feed_forward_t;

typedef struct {
    int* indices;
    int* inputs;
    int outputs;
} pddl_nn_layer_max_pool_t;

typedef struct {
    pddl_nn_layer_max_pool_t* pool;
    pddl_nn_layer_feed_forward_t* perceptron;
} pddl_ground_asnets_proposition_layer_t;

typedef struct {
    pddl_ground_asnets_proposition_layer_t** proposition_layers;
    pddl_nn_layer_feed_forward_t** action_layers;
    int layers;
} pddl_ground_asnets_t;

pddl_nn_layer_feed_forward_t* pddlNNLayerFFNew(int input, int output);

void pddlNNLayerFFDel(pddl_nn_layer_feed_forward_t* l);

pddl_nn_layer_max_pool_t* pddlNNLayerPoolNew(int num_indices, int num_outputs);

void pddlNNLayerPoolDel(pddl_nn_layer_max_pool_t* l);

pddl_ground_asnets_proposition_layer_t* pddlGroundASNetsPLayerNew();

void pddlGroundASNetsPLayerDel(pddl_ground_asnets_proposition_layer_t* l);

pddl_ground_asnets_t* pddlGroundASNetsNew(int layers);

void pddlGroundASNetsDel(pddl_ground_asnets_t* m);

void pddlDumpNNLayerFF(
    const pddl_nn_layer_feed_forward_t* m,
    FILE* out,
    pddl_err_t* err);

void pddlDumpNNLayerPool(
    const pddl_nn_layer_max_pool_t* m,
    FILE* out,
    pddl_err_t* err);

void pddlDumpGroundASNetsModel(
    const pddl_ground_asnets_t* m,
    const char* fn,
    pddl_err_t* err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
