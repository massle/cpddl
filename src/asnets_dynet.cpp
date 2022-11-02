#include "internal.h"
#include "pddl/asnets.h"
#include "pddl/asnets_task.h"

#include <dynet/dynet.h>
#include <dynet/expr.h>
#include <dynet/training.h>
#include <dynet/param-init.h>

static const float SMALL_CONST = 1E-20f;

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
    dynet::Expression min_sum = dynet::constant(cg, dynet::Dim({1}), SMALL_CONST);
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
    e = dynet::mean_batches(e);
    e = -e;
    // TODO: L2 regularization?
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

    ModelParameters(const ModelParameters &) = delete;

    ModelParameters(int hidden_dimension,
                    int num_layers,
                    const pddl_asnets_lifted_task_t *task,
                    dynet::ParameterCollection &model)
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

/*
struct ASNetsPolicy {
    std::vector<float> state;
    std::vector<float> goal;
    std::vector<float> applicable_ops;
    const GroundTask &task;
    const ModelParameters &params;

    dynet::ComputationGraph cg;
    dynet::Expression e_state;
    dynet::Expression e_goal;
    dynet::Expression e_applicable_ops;
    dynet::Expression e_output;

    ASNetsPolicy(const GroundTask &task, const ModelParameters &params)
        : state(task.strips.fact.fact_size, 0),
          goal(task.strips.fact.fact_size, 0),
          applicable_ops(task.strips.op.op_size, 0),
          task(task),
          params(params)
    {
        std::vector<long> dim(1);
        dim[0] = state.size();
        e_state = dynet::input(cg, dynet::Dim(dim), &state);
        e_goal = dynet::input(cg, dynet::Dim(dim), &goal);
        dim[0] = applicable_ops.size();
        e_applicable_ops = dynet::input(cg, dynet::Dim(dim), &applicable_ops);
        e_output = task.expr(params, cg, e_state, e_goal, e_applicable_ops, -1);
    }

    void setState(const pddl_iset_t *s)
    {
        for (int i = 0; i < state.size(); ++i)
            state[i] = 0;
        int fact_id;
        PDDL_ISET_FOR_EACH(s, fact_id)
            state[fact_id] = 1;
    }

    void setGoal(const pddl_iset_t *g)
    {
        for (int i = 0; i < goal.size(); ++i)
            goal[i] = 0;
        int fact_id;
        PDDL_ISET_FOR_EACH(g, fact_id)
            goal[fact_id] = 1;
    }

    void setApplicableOpsInState(const pddl_iset_t *state)
    {
        for (int op_id = 0; op_id < task.strips.op.op_size; ++op_id){
            if (pddlISetIsSubset(&task.strips.op.op[op_id]->pre, state)){
                applicable_ops[op_id] = 1;
            }else{
                applicable_ops[op_id] = 0;
            }
        }
    }

    void setApplicableOps(const pddl_iset_t *ops)
    {
        for (int i = 0; i < applicable_ops.size(); ++i)
            applicable_ops[i] = 0;
        int op_id;
        PDDL_ISET_FOR_EACH(ops, op_id)
            applicable_ops[op_id] = 1;
    }

    int run()
    {
        std::vector<float> out = dynet::as_vector(cg.forward(e_output));
        ASSERT_RUNTIME(out.size() == task.strips.op.op_size);

        int best_op_id = -1;
        float best_value = -1;
        for (int op_id = 0; op_id < out.size(); ++op_id){
            ASSERT(out[op_id] >= 0.f);
            if (out[op_id] > best_value){
                best_op_id = op_id;
                best_value = out[op_id];
            }
        }

        return best_op_id;
    }
};
*/

int pddlASNetsTrain(const char *domain_fn,
                    const char **problem_fn,
                    int problem_fn_size,
                    pddl_err_t *err)
{
    if (problem_fn_size <= 0)
        ERR_RET2(err, -1, "At least one problem file must be provided.");

    CTX(err, "asnets_train", "ASNets-Train");

    int st;

    pddl_asnets_lifted_task_t lifted_task;
    st = pddlASNetsLiftedTaskInit(&lifted_task, domain_fn, problem_fn[0], err);
    if (st < 0){
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    pddl_asnets_ground_task_t *ground_task;
    ground_task = ALLOC_ARR(pddl_asnets_ground_task, problem_fn_size);
    for (int probi = 0; probi < problem_fn_size; ++probi){
        st = pddlASNetsGroundTaskInit(&ground_task[probi],
                                      &lifted_task,
                                      domain_fn,
                                      problem_fn[probi],
                                      err);
        if (st < 0){
            pddlASNetsLiftedTaskFree(&lifted_task);
            for (int i = 0; i < probi; ++i)
                pddlASNetsGroundTaskFree(&ground_task[i]);
            FREE(ground_task);
            CTXEND(err);
            TRACE_RET(err, -1);
        }
    }

    int hidden_dimension = 16;
    int num_layers = 2;

    // TODO: Parametrize
    dynet::DynetParams dynet_params;
    //dynet_params.autobatch = true;
    //dynet_params.mem_descriptor = "4096";
    //dynet_params.profiling = 10;
    dynet_params.random_seed = 1234;
    //dynet_params.shared_parameters = true;
    dynet_params.weight_decay = 2E-4;
    dynet::initialize(dynet_params);

    dynet::ParameterCollection model;

    ModelParameters params(hidden_dimension, num_layers, &lifted_task, model);

    {
    dynet::AdamTrainer trainer(model);

    dynet::ComputationGraph cg;
    // TODO: For debugging
    cg.set_check_validity(true);
    cg.set_immediate_compute(true);

    std::vector<float> state(ground_task[0].fact_size, 0);
    std::vector<float> goal(ground_task[0].fact_size, 0);
    std::vector<float> op_appl(ground_task[0].op_size, 0);
    std::vector<float> output(ground_task[0].op_size, 0);

    int fact_id;
    PDDL_ISET_FOR_EACH(&ground_task[0].strips.init, fact_id)
        state[fact_id] = 1;
    PDDL_ISET_FOR_EACH(&ground_task[0].strips.goal, fact_id)
        goal[fact_id] = 1;
    for (int op_id = 0; op_id < ground_task[0].strips.op.op_size; ++op_id){
        int assigned = false;
        if (pddlISetIsSubset(&ground_task[0].strips.op.op[op_id]->pre,
                             &ground_task[0].strips.init)){
            op_appl[op_id] = 1;
            if (!assigned){
                output[op_id] = 1;
                assigned = true;
            }
        }
    }

    std::vector<long> dim(1);
    dim[0] = state.size();
    dynet::Expression e_input_state = dynet::input(cg, dynet::Dim(dim), state);
    dynet::Expression e_input_goal = dynet::input(cg, dynet::Dim(dim), goal);
    dim[0] = op_appl.size();
    dynet::Expression e_input_op_appl = dynet::input(cg, dynet::Dim(dim), op_appl);
    dynet::Expression e_output = dynet::input(cg, dynet::Dim(dim), output);

    dynet::Expression e = asnetsExpr(ground_task + 0,
                                     params, cg, e_input_state,
                                     e_input_goal,
                                     e_input_op_appl, 0.1);
    {
    std::vector<float> val = dynet::as_vector(cg.forward(e));
    for (int i = 0; i < val.size(); ++i){
        LOG(err, "%d: %f", i, val[i]);
    }
    }
    dynet::Expression e_loss = crossEntropyLoss(cg, e, e_output);

    float loss_val = dynet::as_scalar(cg.forward(e_loss));
    cg.backward(e_loss);
    trainer.update();
    LOG(err, "loss: %f", loss_val);
    loss_val = dynet::as_scalar(cg.forward(e_loss));
    LOG(err, "loss: %f", loss_val);
    }

    //ASNetsPolicy policy(*ground_task[0], params);

    /*
    loss_val = dynet::as_scalar(cg.forward(e_loss));
    cg.backward(e_loss);
    trainer.update();
    LOG(err, "loss: %f", loss_val);
    */

    dynet::cleanup();

    pddlASNetsLiftedTaskFree(&lifted_task);
    for (int i = 0; i < problem_fn_size; ++i)
        pddlASNetsGroundTaskFree(&ground_task[i]);
    FREE(ground_task);
    CTXEND(err);
    return 0;
}
