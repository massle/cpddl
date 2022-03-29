#include <stdio.h>
#include <pddl/pddl.h>

int main(int argc, char *argv[])
{
    pddl_config_t cfg = PDDL_CONFIG_INIT;
    pddl_t pddl;
    pddl_err_t err = PDDL_ERR_INIT;

    if (argc != 3){
        fprintf(stderr, "Usage: %s domain.pddl problem.pddl\n", argv[0]);
        return -1;
    }

    pddlErrWarnEnable(&err, stderr);
    pddlErrInfoEnable(&err, stderr);
    cfg.force_adl = 1; // TODO: parametrize
    if (pddlInit(&pddl, argv[1], argv[2], &cfg, &err) != 0){
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }

    pddlNormalize(&pddl);
    pddlPrintDebug(&pddl, stdout);

    pddlFree(&pddl);
    return 0;
}


