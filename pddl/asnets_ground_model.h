#ifndef _PDDL__ASNETS_GROUND_MODEL_H_
#define _PDDL__ASNETS_GROUND_MODEL_H_

#include <pddl/asnets_task.h>
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
    pddl_nn_layer_max_pool_t pool;
    pddl_nn_layer_feed_forward_t perceptron;
} pddl_ground_asnets_proposition_layer_t;

typedef struct {
    pddl_nn_layer_feed_forward_t l0;
    pddl_nn_layer_feed_forward_t l1;
    pddl_nn_layer_feed_forward_t l2;
} pddl_ground_asnets_input_interface_t;

typedef struct {
    pddl_ground_asnets_input_interface_t input_interface;
    pddl_ground_asnets_proposition_layer_t* proposition_layers;
    pddl_nn_layer_feed_forward_t* action_layers;
    pddl_nn_layer_max_pool_t output_interface;
    int layers;
} pddl_ground_asnets_t;

void pddlNNLayerFFInit(pddl_nn_layer_feed_forward_t* l, int input, int output);

void pddlNNLayerFFFree(pddl_nn_layer_feed_forward_t* l);

void pddlNNLayerPoolInit(
    pddl_nn_layer_max_pool_t* l,
    int num_indices,
    int num_outputs);

void pddlNNLayerPoolFree(pddl_nn_layer_max_pool_t* l);

void pddlGroundASNetsPLayerFree(pddl_ground_asnets_proposition_layer_t* l);

void pddlGroundASNetsInit(pddl_ground_asnets_t* ga, int layers);

void pddlGroundASNetsFree(pddl_ground_asnets_t* m);

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

typedef struct {
    int* variable;
    int* value;
    int* label;
    int num_facts;
    int num_operators;
    int num_variables;
    int num_labels;
} pddl_ground_asnets_conf_t;

void pddlGroundASNetsConfInit(
    pddl_ground_asnets_conf_t* conf,
    int num_facts,
    int num_operators);

void pddlGroundASNetsConfLoad(
    pddl_ground_asnets_conf_t* conf,
    const char* fn,
    const pddl_asnets_ground_task_t* task);

void pddlGroundASNetsConfFree(pddl_ground_asnets_conf_t* conf);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
