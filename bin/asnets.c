#include "pddl/pddl.h"

static pddl_err_t err = PDDL_ERR_INIT;

int main(int argc, char *argv[])
{
    pddlErrInfoEnable(&err, stderr);

    pddl_asnets_config_t cfg;
    if (pddlASNetsConfigInitFromFile(&cfg, argv[1], &err) != 0){
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }

    pddl_asnets_t *asnets = pddlASNetsNew(&cfg, &err);
    if (asnets == NULL){
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }
    if (1)
    {
        int ret = pddlASNetsLoad(asnets, "model.asnets", &err);
        if (ret < 0)
            pddlErrPrint(&err, 1, stderr);
        return ret;
    }
    int ret = pddlASNetsTrain(asnets, &err);
    if (ret == 0){
        ret = pddlASNetsSave(asnets, "model.asnets", &err);
    }

    if (ret < 0)
        pddlErrPrint(&err, 1, stderr);

    pddlASNetsConfigFree(&cfg);
    pddlASNetsDel(asnets);
    return ret;
}
