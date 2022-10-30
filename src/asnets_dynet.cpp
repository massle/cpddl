
#include <dynet/expr.h>
#include "internal.h"

struct ActionModule {
    int hidden_dim;
    int related_props;
    int layer;
    int input_vec_size;
    int output_dim;
    dynet::Parameter W;
    dynet::Parameter bias;

    // TODO: landmarks/...
    ActionModule(int hidden_dimension,
                 int num_related_propositions,
                 int layer,
                 bool is_output,
                 dynet::ParameterCollection &model)
        : hidden_dim(hidden_dimension),
          related_props(num_related_propositions),
          layer(layer)
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
        dim_W[0] = input_vec_size;
        dim_W[1] = output_dim;
        W = model.add_parameters(dynet::Dim(dim_W));

        std::vector<long> dim_bias(1);
        dim_bias[0] = output_dim;
        bias = model.add_parameters(dynet::Dim(dim_bias));
    }

    dynet::Expression expr(dynet::ComputationGraph &cg,
                           const std::vector<dynet::Expression> &input)
    {
        dynet::Expression w = dynet::parameter(cg, W);
        dynet::Expression b = dynet::parameter(cg, bias);
        dynet::Expression u = dynet::concatenate(input);
        return dynet::elu((w * u) + b);
    }
};

struct PropositionModule {
    int hidden_dim;
    int related_acts;
    int layer;
    int input_vec_size;
    dynet::Parameter W;
    dynet::Parameter bias;

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
        // Skip connection
        input_vec_size += hidden_dim;

        std::vector<long> dim_W(2);
        dim_W[0] = input_vec_size;
        dim_W[1] = hidden_dim;
        W = model.add_parameters(dynet::Dim(dim_W));

        std::vector<long> dim_bias(1);
        dim_bias[0] = hidden_dim;
        bias = model.add_parameters(dynet::Dim(dim_bias));
    }

    dynet::Expression expr(dynet::ComputationGraph &cg,
                           const std::vector<std::vector<dynet::Expression>> &input)
    {
        std::vector<dynet::Expression> pooled_input(input.size());
        for (size_t i = 0; i < input.size(); ++i){
            pooled_input[i] = dynet::max(input[i]);
        }
        dynet::Expression w = dynet::parameter(cg, W);
        dynet::Expression b = dynet::parameter(cg, bias);
        dynet::Expression u = dynet::concatenate(pooled_input);
        return dynet::elu((w * u) + b);
    }
};

void dynetTest(void)
{
}
