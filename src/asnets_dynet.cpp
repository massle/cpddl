/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/asnets.h"
#include "pddl/asnets_task.h"
#include "pddl/asnets_train_data.h"

#ifdef PDDL_DYNET
#include <dynet/dynet.h>
#include <dynet/expr.h>
#include <dynet/training.h>
#include <dynet/param-init.h>

static const float SMALL_CONST = 1E-6f;

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
        ASSERT_RUNTIME(layer == 0);
        std::vector<dynet::Expression> input;
        input.insert(input.end(), input_state.begin(), input_state.end());
        input.insert(input.end(), input_goal.begin(), input_goal.end());
        input.push_back(input_applicable);
        return expr(cg, input);
    }

    void saveWeights()
    {
        // TODO
        // SQL format:
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
        for (size_t i = 0; i < input.size(); ++i){
            pooled_input[i] = input[i][0];
            for (int j = 1; j < input[i].size(); ++j){
                pooled_input[i] = dynet::max(pooled_input[i], input[i][j]);
            }
        }
        dynet::Expression w = dynet::parameter(cg, W);
        dynet::Expression b = dynet::parameter(cg, bias);
        dynet::Expression u = dynet::concatenate(pooled_input);
        return dynet::elu((w * u) + b);
    }
};

struct ModelParameters {
    int num_layers;
    std::vector<std::vector<ActionModule *>> action;
    std::vector<std::vector<PropositionModule *>> prop;
    dynet::ParameterCollection model;

    ModelParameters(const ModelParameters &) = delete;

    ModelParameters(int hidden_dimension,
                    int num_layers,
                    const pddl_asnets_lifted_task_t *task)
        : num_layers(num_layers)
    {
        action.resize(num_layers + 1);
        prop.resize(num_layers);

        for (int layer = 0; layer < num_layers; ++layer){
            for (size_t aid = 0; aid < task->action_size; ++aid){
                ActionModule *am;
                am = new ActionModule(hidden_dimension,
                                      task->action[aid].related_atom_size,
                                      layer, false, model);
                action[layer].push_back(am);
            }

            for (size_t pid = 0; pid < task->pred_size; ++pid){
                PropositionModule *pm;
                pm = new PropositionModule(hidden_dimension,
                                           task->pred[pid].related_action_size,
                                           layer, model);
                prop[layer].push_back(pm);
            }
        }

        for (size_t aid = 0; aid < task->action_size; ++aid){
            ActionModule *am;
            am = new ActionModule(hidden_dimension,
                                  task->action[aid].related_atom_size,
                                  num_layers, true, model);
            action[num_layers].push_back(am);
        }

        ASSERT_RUNTIME(num_layers == action.size() - 1);
        ASSERT_RUNTIME(num_layers == prop.size());
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
};


static void _actionLayer(const pddl_asnets_ground_task_t *g,
                         const ModelParameters &model,
                         dynet::ComputationGraph &cg,
                         int layer,
                         const std::vector<dynet::Expression> &prop_layer,
                         const std::vector<dynet::Expression> &prev_action_layer,
                         std::vector<dynet::Expression> &action_layer,
                         float dropout_rate)
{
    for (int op_id = 0; op_id < g->op_size; ++op_id){
        std::vector<dynet::Expression> in;
        for (int i = 0; i < g->op[op_id].related_fact_size; ++i){
            int fact_id = g->op[op_id].related_fact[i];
            in.push_back(prop_layer[fact_id]);
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
    for (int fact_id = 0; fact_id < g->fact_size; ++fact_id){
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
        }
        if (prev_prop_layer != NULL)
            input[input_size - 1].push_back((*prev_prop_layer)[fact_id]);

        int pred_id = g->fact[fact_id].pred->pred_id;
        PropositionModule *pm = model.prop[layer][pred_id];
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
    for (int op_id = 0; op_id < g->op_size; ++op_id){
        std::vector<dynet::Expression> in_state;
        std::vector<dynet::Expression> in_goal;
        dynet::Expression in_applicable;
        for (int i = 0; i < g->op[op_id].related_fact_size; ++i){
            int fact_id = g->op[op_id].related_fact[i];
            in_state.push_back(dynet::pick(input_state, fact_id));
            in_goal.push_back(dynet::pick(input_goal_condition, fact_id));
            in_applicable = dynet::pick(input_applicable_ops, op_id);
        }
        int action_id = g->op[op_id].action->action_id;
        ActionModule *am = model.action[layer][action_id];
        dynet::Expression e = am->exprInput(cg, in_state, in_goal, in_applicable);
        action_layer[layer].push_back(e);
    }

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
    for (int i = 0; i < applicable_ops.size(); ++i)
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
    for (int i = 0; i < state.size(); ++i)
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
    for (int i = 0; i < goal.size(); ++i)
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
                     int *out_state)
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
    dynet::Expression e_output = asnetsExpr(task, params, cg, e_state, e_goal,
                                            e_applicable_ops, -1);

    std::vector<float> out = dynet::as_vector(cg.forward(e_output));
    ASSERT_RUNTIME(out.size() == task->strips.op.op_size);

    int best_op_id = -1;
    float best_value = -1;
    for (int op_id = 0; op_id < out.size(); ++op_id){
        ASSERT(out[op_id] >= 0.f);
        if (applicable_ops[op_id] < .5)
            continue;
        if (out[op_id] > best_value){
            best_op_id = op_id;
            best_value = out[op_id];
        }
    }

    if (out_state != NULL)
        pddlASNetsGroundTaskFDRApplyOp(task, in_state, best_op_id, out_state);

    return best_op_id;
}


struct pddl_asnets {
    pddl_asnets_config_t cfg;
    pddl_asnets_lifted_task_t lifted_task;
    dynet::ComputationGraph *cg;
    dynet::Trainer *trainer;
    ModelParameters *params;
    pddl_asnets_ground_task_t *ground_task;
    int ground_task_size;
};

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
        ASSERT_RUNTIME(state.size() == size * fact_size);
        ASSERT_RUNTIME(applicable_ops.size() == size * op_size);
        ASSERT_RUNTIME(selected_op.size() == size);
        ASSERT_RUNTIME(goal.size() == fact_size);

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
        for (int task_id = 0; task_id < batch.size(); ++task_id){
            if (batch[task_id].size == 0)
                continue;
            batch[task_id].createInputs(cg);
        }
    }
};



pddl_asnets_t *pddlASNetsNew(const char *domain_fn,
                             const char **problem_fn,
                             int problem_fn_size,
                             const pddl_asnets_config_t *cfg,
                             pddl_err_t *err)
{
    if (problem_fn_size <= 0)
        ERR_RET2(err, NULL, "ASNets: At least one problem file is required.");

    CTX(err, "asnets", "ASNets");
    pddl_asnets_t *a = ZALLOC(pddl_asnets_t);
    a->cfg = *cfg;
    
    int st;
    st = pddlASNetsLiftedTaskInit(&a->lifted_task, domain_fn, problem_fn[0], err);
    if (st < 0){
        CTXEND(err);
        TRACE_RET(err, NULL);
    }

    a->ground_task_size = problem_fn_size;
    a->ground_task = ALLOC_ARR(pddl_asnets_ground_task, a->ground_task_size);
    for (int probi = 0; probi < problem_fn_size; ++probi){
        st = pddlASNetsGroundTaskInit(&a->ground_task[probi],
                                      &a->lifted_task,
                                      domain_fn,
                                      problem_fn[probi],
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
    dynet::cleanup();
}

//void pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err);

void pddlASNetsSaveWeights(const pddl_asnets_t *a, const char *fn)
{
    // TODO
}

void pddlASNetsLoadWeights(pddl_asnets_t *a, const char *fn)
{
    // TODO
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

    ASSERT_RUNTIME(nets.size() > 0);
    // Compute mean over all losses
    dynet::Expression e_loss = dynet::sum(nets) / batch_size;
    return e_loss;
}


static int trainStep(pddl_asnets_t *a,
                     int epoch,
                     int train_step,
                     pddl_asnets_train_data_t *data,
                     pddl_asnets_train_stats_t *stats,
                     pddl_err_t *err)
{
    //LOG(err, "epoch: %{epoch}d/%d, step: %{step}d/%d",
    //    epoch, a->cfg.max_train_epochs,
    //    train_step, a->cfg.train_cycles);
    stats->train_step = train_step + 1;

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
        " | minibatch loss: %{batch_loss}f, size: %{batch_size}d",
        stats->epoch, stats->max_epochs,
        stats->train_step, stats->max_train_steps,
        stats->overall_loss, stats->success_rate, stats->num_samples,
        stats->consecutive_successful_epochs,
        loss_val, a->cfg.batch_size);

    return 0;
}

static int trainPolicyStatePool(pddl_asnets_t *a,
                                int ground_task_id,
                                pddl_fdr_state_pool_t *states,
                                pddl_err_t *err)
{
    int ret = 0;
    const pddl_asnets_ground_task_t *task = a->ground_task + ground_task_id;
    int *state = ALLOC_ARR(int, task->fdr.var.var_size);
    int *state2 = ALLOC_ARR(int, task->fdr.var.var_size);

    // Start in the initial state
    pddl_state_id_t state_id = pddlFDRStatePoolInsert(states, task->fdr.init);
    for (int step = 0; step < a->cfg.policy_rollout_limit; ++step){
        // get the last reached state
        pddlFDRStatePoolGet(states, state_id, state);
        if (pddlFDRPartStateIsConsistentWithState(&task->fdr.goal, state)){
            ret = 1;
            break;
        }

        // Apply policy. If we get -1, it means the state is dead-end,
        // because there are no applicable operators
        int op_id = runPolicy(task, *a->params, *a->cg, state, state2);
        if (op_id < 0){
            break;
        }

        // Insert current state
        pddl_state_id_t prev_state_id = state_id;
        state_id = pddlFDRStatePoolInsert(states, state2);
        // If the new state was already in the pool, then we got a cycle
        if (state_id <= prev_state_id){
            break;
        }
    }

    FREE(state);
    FREE(state2);
    return ret;
}

static int trainExploration(pddl_asnets_t *a,
                            int epoch,
                            int ground_task_id,
                            pddl_asnets_train_data_t *data,
                            pddl_err_t *err)
{
    const pddl_asnets_ground_task_t *task = a->ground_task + ground_task_id;

    pddl_fdr_state_pool_t states;
    pddlFDRStatePoolInit(&states, &task->fdr.var, err);

    // Collect states from the policy rollout
    int reached_goal = trainPolicyStatePool(a, ground_task_id, &states, err);
    LOG(err, "Policy rollout: %{policy_rollout_states}d states,"
        " reached goal: %{reached_goal}d",
        states.num_states, reached_goal);

    // TODO: Here we can add also states from random walks.
    //       Maybe for the for the first epoch?

    // Extend training data with teacher rollouts
    int *state = ALLOC_ARR(int, task->fdr.var.var_size);
    for (pddl_state_id_t state_id = 0; state_id < states.num_states; ++state_id){
        pddlFDRStatePoolGet(&states, state_id, state);
        // TODO: Parametrize
        int ret;
        ret = pddlASNetsTrainDataRolloutAStarLMCut(data, ground_task_id, state,
                                                   &task->fdr,
                                                   a->cfg.teacher_timeout, err);
        if (ret < 0){
            FREE(state);
            pddlFDRStatePoolFree(&states);
            TRACE_RET(err, -1);
        }
    }
    FREE(state);

    pddlFDRStatePoolFree(&states);
    return 0;
}

static float overallLoss(pddl_asnets_t *a,
                         pddl_asnets_train_data_t *data)
{
    dynet::Expression e_loss = asnetsTrainExpr(a, data, -1, *a->cg);
    float loss = dynet::as_scalar(a->cg->forward(e_loss));
    return loss;
}

static float successRate(pddl_asnets_t *a)
{
    int num_solved = 0;
    for (int task_id = 0; task_id < a->ground_task_size; ++task_id){
        const pddl_asnets_ground_task_t *task = a->ground_task + task_id;
        pddl_fdr_state_pool_t states;
        pddlFDRStatePoolInit(&states, &task->fdr.var, NULL);
        if (trainPolicyStatePool(a, task_id, &states, NULL))
            num_solved += 1;
        pddlFDRStatePoolFree(&states);
    }

    return num_solved / (float)a->ground_task_size;
}

static int trainEpoch(pddl_asnets_t *a,
                      int epoch,
                      pddl_asnets_train_data_t *data,
                      pddl_asnets_train_stats_t *stats,
                      pddl_err_t *err)
{
    LOG(err, "epoch: %{epoch}d/%d", epoch, a->cfg.max_train_epochs);
    stats->epoch = epoch + 1;

    // Exploration phase
    for (int ground_task = 0; ground_task < a->ground_task_size; ++ground_task){
        int ret;
        if ((ret = trainExploration(a, epoch, ground_task, data, err)) != 0){
            if (ret < 0)
                TRACE_RET(err, ret);
            return ret;
        }
    }
    stats->num_samples = data->sample_size;

    // Training phase
    int num_steps = a->cfg.train_steps;
    //num_steps = PDDL_MIN(num_steps, data->sample_size / a->cfg.batch_size);
    //num_steps = PDDL_MAX(num_steps, 1);
    LOG(err, "num training steps: %{training_steps}d", num_steps);
    for (int train_step = 0; train_step < num_steps; ++train_step){
        int ret;
        if ((ret = trainStep(a, epoch, train_step, data, stats, err)) != 0){
            if (ret < 0)
                TRACE_RET(err, ret);
            return ret;
        }
    }

    CTX(err, "success_rate", "Success Rate");
    stats->success_rate = successRate(a);
    LOG(err, "Success rate: %{success_rate}f", stats->success_rate);
    CTXEND(err);
    CTX(err, "overall_loss", "Overall Loss");
    stats->overall_loss = overallLoss(a, data);
    LOG(err, "Overall loss: %{overall_loss}f", stats->overall_loss);
    CTXEND(err);
    LOG(err, "Train samples: %{train_samples}d", stats->num_samples);
    LOG(err, "epoch %d/%d, step: %d/%d, loss: %.3f, succ: %.2f, samples: %d,"
        " succ epochs: %d",
        stats->epoch, stats->max_epochs,
        stats->train_step, stats->max_train_steps,
        stats->overall_loss, stats->success_rate, stats->num_samples,
        stats->consecutive_successful_epochs);
    return 0;
}

int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err)
{
    CTX(err, "asnets_train", "ASNets-Train");
    pddl_asnets_train_data_t data;
    pddlASNetsTrainDataInit(&data);

    pddl_asnets_train_stats_t stats;
    ZEROIZE(&stats);
    stats.max_epochs = a->cfg.max_train_epochs;
    stats.max_train_steps = a->cfg.train_steps;
    stats.success_rate = successRate(a);
    stats.overall_loss = -1.f;

    for (int epoch = 0; epoch < a->cfg.max_train_epochs; ++epoch){
        if (a->cfg.double_batch_size_every_epoch > 0
                && epoch > 0
                && epoch % a->cfg.double_batch_size_every_epoch == 0){
            a->cfg.batch_size *= 2;
        }

        int ret;
        if ((ret = trainEpoch(a, epoch, &data, &stats, err)) != 0){
            pddlASNetsTrainDataFree(&data);
            CTXEND(err);
            if (ret < 0)
                TRACE_RET(err, ret);
            return ret;
        }

        if (stats.success_rate >= a->cfg.early_termination_success_rate){
            stats.consecutive_successful_epochs += 1;
        }else{
            stats.consecutive_successful_epochs = 0;
        }

        LOG(err, "Consecutive successful epochs: %d",
            stats.consecutive_successful_epochs);
        if (stats.consecutive_successful_epochs >= a->cfg.early_termination_epochs){
            LOG(err, "Reached %d/%d consecutive successful epochs.",
                stats.consecutive_successful_epochs,
                a->cfg.early_termination_epochs);
            LOG2(err, "Terminating training.");
        }
    }
    LOG(err, "epoch %d/%d, step: %d/%d, loss: %.3f, succ: %.2f, samples: %d,"
        " succ epochs: %d",
        stats.epoch, stats.max_epochs,
        stats.train_step, stats.max_train_steps,
        stats.overall_loss, stats.success_rate, stats.num_samples,
        stats.consecutive_successful_epochs);

    pddlASNetsTrainDataFree(&data);
    CTXEND(err);
    return 0;
}

#else /* PDDL_DYNET */

pddl_asnets_t *pddlASNetsNew(const char *domain_fn,
                             const char **problem_fn,
                             int problem_fn_size,
                             const pddl_asnets_config_t *cfg,
                             pddl_err_t *err)
{
    FATAL("This module requires dynet library.");
    return NULL;
}

void pddlASNetsDel(pddl_asnets_t *a)
{
    FATAL("This module requires dynet library.");
}

void pddlASNetsSaveWeights(const pddl_asnets_t *a, const char *fn)
{
    FATAL("This module requires dynet library.");
}

void pddlASNetsLoadWeights(pddl_asnets_t *a, const char *fn)
{
    FATAL("This module requires dynet library.");
}

int pddlASNetsTrain(pddl_asnets_t *a, pddl_err_t *err)
{
    FATAL("This module requires dynet library.");
    return -1;
}

#endif /* PDDL_DYNET */
