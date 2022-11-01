
#include "internal.h"
#include "pddl/pddl_struct.h"
#include "pddl/lifted_mgroup_infer.h"
#include "pddl/strips.h"
#include "pddl/strips_ground_datalog.h"
#include "pddl/critical_path.h"
#include "pddl/fdr.h"
#include "pddl/asnets.h"
#include <dynet/dynet.h>
#include <dynet/expr.h>

struct Action {
    int action_id;
    std::vector<const pddl_fm_atom_t *> atom;
    std::string name;
};

struct ActionPos {
    int action_id;
    int pos;
    ActionPos(int action_id, int pos)
        : action_id(action_id), pos(pos)
    {}
};

struct Pred {
    int pred_id;
    std::vector<ActionPos> action;
};

struct LiftedTask {
    pddl_t pddl;
    std::vector<Action> action;
    std::vector<Pred> pred;

    LiftedTask(const LiftedTask&) = delete;

    LiftedTask(const char *domain_fn, const char *problem_fn, pddl_err_t *err)
    {
        pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
        pddl_cfg.force_adl = 1;
        pddl_cfg.normalize = 1;
        pddl_cfg.enforce_unit_cost = 1;
        if (pddlInit(&pddl, domain_fn, problem_fn, &pddl_cfg, err) != 0)
            return;

        action.resize(pddl.action.action_size);
        pred.resize(pddl.pred.pred_size);
        for (int pi = 0; pi < pddl.pred.pred_size; ++pi)
            pred[pi].pred_id = pi;

        for (int ai = 0; ai < pddl.action.action_size; ++ai){
            const pddl_action_t *a = pddl.action.action + ai;
            action[ai].action_id = ai;
            action[ai].name = a->name;

            const pddl_fm_atom_t *at;
            pddl_fm_const_it_atom_t it;
            PDDL_FM_FOR_EACH_ATOM(a->pre, &it, at){
                int pos;
                if ((pos = _addUniqueAtom(action[ai].atom, at)) >= 0)
                    pred[at->pred].action.push_back(ActionPos(ai, pos));
            }
            PDDL_FM_FOR_EACH_ATOM(a->eff, &it, at){
                int pos;
                if ((pos = _addUniqueAtom(action[ai].atom, at)) >= 0)
                    pred[at->pred].action.push_back(ActionPos(ai, pos));
            }
        }
    }

    ~LiftedTask()
    {
        pddlFree(&pddl);
    }

    int _addUniqueAtom(std::vector<const pddl_fm_atom_t *> &arr,
                       const pddl_fm_atom_t *atom)
    {
        size_t size = arr.size();
        for (size_t i = 0; i < size; ++i){
            const pddl_fm_atom_t *a = arr[i];
            if (a->pred == atom->pred){
                int eq = 1;
                for (int ai = 0; ai < atom->arg_size; ++ai){
                    if (a->arg[ai].obj != atom->arg[ai].obj
                            || a->arg[ai].param != atom->arg[ai].param){
                        eq = 0;
                        break;
                    }
                }
                if (eq)
                    return -1;
            }
        }
        arr.push_back(atom);
        return arr.size() - 1;
    }
};

struct Op {
    int op_id;
    const Action *action;
    std::vector<int> related_fact;
};

struct Fact {
    int fact_id;
    const Pred *pred;
    std::vector<std::vector<int>> related_op;
};

struct GroundTask {
    pddl_t pddl;
    pddl_strips_t strips;
    pddl_fdr_t fdr;

    std::vector<Op> op;
    std::vector<Fact> fact;

    GroundTask(const GroundTask&) = delete;

    GroundTask(const LiftedTask &p,
               const char *domain_fn,
               const char *problem_fn,
               pddl_err_t *err)
    {
        CTX(err, "asnets_ground_task", "ASNets-GroundTask");
        pddl_config_t pddl_cfg = PDDL_CONFIG_INIT;
        pddl_cfg.force_adl = 1;
        pddl_cfg.normalize = 1;
        pddl_cfg.enforce_unit_cost = 1;
        if (pddlInit(&pddl, domain_fn, problem_fn, &pddl_cfg, err) != 0)
            return;

        ASSERT_RUNTIME(pddl.action.action_size == p.action.size());
        ASSERT_RUNTIME(pddl.pred.pred_size == p.pred.size());

        pddl_lifted_mgroups_infer_limits_t lifted_mgroups_limits
            = PDDL_LIFTED_MGROUPS_INFER_LIMITS_INIT;
        pddl_lifted_mgroups_t lmg;
        pddlLiftedMGroupsInit(&lmg);
        pddlLiftedMGroupsInferFAMGroups(&pddl, &lifted_mgroups_limits, &lmg, err);

        pddl_ground_config_t ground_cfg = PDDL_GROUND_CONFIG_INIT;
        ground_cfg.prune_op_pre_mutex = 0;
        ground_cfg.prune_op_dead_end = 0;
        ground_cfg.remove_static_facts = 0;
        ground_cfg.keep_action_args = 1;
        ground_cfg.keep_all_static_facts = 1;
        if (pddlStripsGroundDatalog(&strips, &pddl, &ground_cfg, err) != 0){
            CTXEND(err);
            return;
        }

        pddl_mutex_pairs_t mutex;
        PDDL_ISET(unreachable_op);
        PDDL_ISET(unreachable_fact);
        pddlMutexPairsInitStrips(&mutex, &strips);
        pddlH2(&strips, &mutex, &unreachable_fact, &unreachable_op, -1., err);
        pddlStripsReduce(&strips, &unreachable_fact, &unreachable_op);
        pddlISetFree(&unreachable_op);
        pddlISetFree(&unreachable_fact);

        pddl_mgroups_t mgroups;
        pddlMGroupsInitEmpty(&mgroups);
        pddlMGroupsGround(&mgroups, &pddl, &lmg, &strips);

        pddlFDRInitFromStrips(&fdr, &strips, &mgroups, &mutex,
                              PDDL_FDR_VARS_LARGEST_FIRST, 0, err);
        ASSERT_RUNTIME(strips.op.op_size == fdr.op.op_size);

        pddlMGroupsFree(&mgroups);
        pddlMutexPairsFree(&mutex);
        pddlLiftedMGroupsFree(&lmg);

        _computeRelatedness(p, err);
        _check(p, err);

        CTXEND(err);
    }

    ~GroundTask()
    {
        pddlStripsFree(&strips);
        pddlFDRFree(&fdr);
        pddlFree(&pddl);
    }

    void _computeRelatedness(const LiftedTask &pddl, pddl_err_t *err)
    {
        CTX(err, "relatedness", "Relatedness");
        op.resize(strips.op.op_size);
        for (int i = 0; i < strips.op.op_size; ++i){
            op[i].op_id = i;
            ASSERT(strips.op.op[i]->pddl_action_id >= 0);
            op[i].action = &pddl.action[strips.op.op[i]->pddl_action_id];
            op[i].related_fact.resize(op[i].action->atom.size(), -1);
        }

        fact.resize(strips.fact.fact_size);
        for (int i = 0; i < strips.fact.fact_size; ++i){
            fact[i].fact_id = i;
            ASSERT(strips.fact.fact[i]->ground_atom != NULL);
            fact[i].pred = &pddl.pred[strips.fact.fact[i]->ground_atom->pred];
            fact[i].related_op.resize(fact[i].pred->action.size());
        }

        for (int op_id = 0; op_id < strips.op.op_size; ++op_id){
            const pddl_strips_op_t *so = strips.op.op[op_id];
            Op &o = op[op_id];
            const pddl_obj_id_t *oargs = so->action_args;

            // TODO: Conditional effects not supported yet
            ASSERT(so->cond_eff_size == 0);
            ASSERT(so->action_args != NULL);
            ASSERT(so->pddl_action_id >= 0);

            PDDL_ISET(facts);
            pddlISetUnion(&facts, &so->pre);
            pddlISetUnion(&facts, &so->add_eff);
            pddlISetUnion(&facts, &so->del_eff);
            int fact_id;
            PDDL_ISET_FOR_EACH(&facts, fact_id){
                const pddl_ground_atom_t *fatom = strips.fact.fact[fact_id]->ground_atom;
                for (size_t pos = 0; pos < o.action->atom.size(); ++pos){
                    const pddl_fm_atom_t *atom = o.action->atom[pos];
                    if (_atomEq(fatom, atom, oargs)){
                        ASSERT(o.related_fact[pos] < 0);
                        o.related_fact[pos] = fact_id;
                        _addRelatedOp(fact_id, op_id, pos);
                    }
                }
            }
            pddlISetFree(&facts);
        }
        CTXEND(err);
    }

    bool _atomEq(const pddl_ground_atom_t *a1,
                 const pddl_fm_atom_t *a2,
                 const pddl_obj_id_t *args)
    {
        if (a1->pred != a2->pred)
            return false;
        for (int argi = 0; argi < a1->arg_size; ++argi){
            pddl_obj_id_t obj2 = a2->arg[argi].obj;
            if (a2->arg[argi].param >= 0)
                obj2 = args[a2->arg[argi].param];
            if (a1->arg[argi] != obj2)
                return false;
        }
        return true;
    }

    void _addRelatedOp(int fact_id, int op_id, int pos)
    {
        const Action *action = op[op_id].action;
        const Pred *pred = fact[fact_id].pred;
        for (size_t i = 0; i < pred->action.size(); ++i){
            if (pred->action[i].action_id == action->action_id
                    && pred->action[i].pos == pos){
                fact[fact_id].related_op[i].push_back(op_id);
                return;
            }
        }
        FATAL2("Error: Cannot find the right action/pos related to a fact");
    }

    void _check(const LiftedTask &p, pddl_err_t *err)
    {
        // TODO: Replace asserts with reporting what exactly is wrong
        LOG2(err, "Checking everything is properly set up...");
        ASSERT_RUNTIME(fdr.op.op_size == strips.op.op_size);
        ASSERT_RUNTIME(strips.op.op_size == op.size());
        for (int op_id = 0; op_id < op.size(); ++op_id){
            ASSERT_RUNTIME(op[op_id].related_fact.size() == op[op_id].action->atom.size());
            for (int fact_id : op[op_id].related_fact)
                ASSERT_RUNTIME(fact_id >= 0);
        }

        ASSERT_RUNTIME(strips.fact.fact_size == fact.size());
        for (int fact_id = 0; fact_id < fact.size(); ++fact_id){
            ASSERT_RUNTIME(fact[fact_id].related_op.size() == fact[fact_id].pred->action.size());
            for (int i = 0; i < fact[fact_id].related_op.size(); ++i){
                const std::vector<int> &rop = fact[fact_id].related_op[i];
                if (rop.size() == 0){
                    LOG(err, "%s : %d/%d=(%d,%d)=(%s,%d)",
                        strips.fact.fact[fact_id]->name,
                        i, (int)fact[fact_id].related_op.size(),
                        fact[fact_id].pred->action[i].action_id,
                        fact[fact_id].pred->action[i].pos,
                        p.action[fact[fact_id].pred->action[i].action_id].name.c_str(),
                        fact[fact_id].pred->action[i].pos);
                }

                ASSERT_RUNTIME(rop.size() > 0);
            }
        }
        LOG2(err, "Check DONE.");
    }
};


struct ActionModule {
    int hidden_dim;
    int related_props;
    int layer;
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


struct ModelParameters {
    std::vector<std::vector<ActionModule *>> action;
    std::vector<std::vector<PropositionModule *>> prop;

    ModelParameters(int hidden_dimension,
                    int num_layers,
                    const LiftedTask &task,
                    dynet::ParameterCollection &model)
    {
        action.resize(num_layers + 1);
        prop.resize(num_layers);

        for (int layer = 0; layer < num_layers; ++layer){
            for (size_t aid = 0; aid < task.action.size(); ++aid){
                ActionModule *am = new ActionModule(hidden_dimension,
                                                    task.action[aid].atom.size(),
                                                    layer,
                                                    false,
                                                    model);
                action[layer].push_back(am);
            }

            for (size_t pid = 0; pid < task.pred.size(); ++pid){
                PropositionModule *pm = new PropositionModule(hidden_dimension,
                                                              task.pred[pid].action.size(),
                                                              layer,
                                                              model);
                prop[layer].push_back(pm);
            }
        }

        for (size_t aid = 0; aid < task.action.size(); ++aid){
            ActionModule *am = new ActionModule(hidden_dimension,
                                                task.action[aid].atom.size(),
                                                num_layers,
                                                true,
                                                model);
            action[num_layers].push_back(am);
        }
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

int pddlASNetsTrain(const char *domain_fn,
                    const char **problem_fn,
                    int problem_fn_size,
                    pddl_err_t *err)
{
    CTX(err, "asnets_train", "ASNets-Train");

    LiftedTask lifted_task(domain_fn, problem_fn[0], err);
    if (pddlErrIsSet(err)){
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    std::vector<GroundTask *> ground_task;
    for (int i = 0; i < problem_fn_size; ++i){
        GroundTask *gt = new GroundTask(lifted_task, domain_fn, problem_fn[i], err);
        if (pddlErrIsSet(err)){
            CTXEND(err);
            TRACE_RET(err, -1);
        }
        ground_task.push_back(gt);
    }

    int hidden_dimension = 16;
    int num_layers = 2;

    dynet::DynetParams dynet_params;
    //dynet_params.autobatch = true;
    dynet_params.mem_descriptor = "4096";
    dynet_params.profiling = 10;
    dynet_params.random_seed = 123;
    dynet_params.shared_parameters = true;
    //dynet_params.weight_decay = 1E-6;
    dynet::initialize(dynet_params);

    dynet::ParameterCollection model;
    ModelParameters params(hidden_dimension, num_layers, lifted_task, model);


    for (size_t i = 0; i < ground_task.size(); ++i){
        delete ground_task[i];
    }

    dynet::cleanup();
    CTXEND(err);
    return 0;
}
