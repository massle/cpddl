#include "pddl/pddl.h"

static pddl_err_t err = PDDL_ERR_INIT;

int main(int argc, char *argv[])
{
    pddlErrInfoEnable(&err, stderr);

    const char *domain_fn = argv[1];
    const char *problem_fn[argc - 2];
    for (int i = 0; i < argc - 2; ++i)
        problem_fn[i] = argv[i + 2];

    pddl_asnets_config_t cfg = PDDL_ASNETS_CONFIG_INIT;
    pddl_asnets_t *asnets = pddlASNetsNew(domain_fn, problem_fn, argc - 2,
                                          &cfg, &err);
    int ret = pddlASNetsTrain(asnets, &err);
    if (ret < 0)
        pddlErrPrint(&err, 1, stderr);
    pddlASNetsDel(asnets);
    return ret;
}
